from typing import Any, Dict, List, NamedTuple, Optional, Tuple

import torch
import torch.distributed as dist
import torch.nn as nn

from torch.distributed.fsdp._common_utils import _named_parameters_with_duplicates
from torch.utils._pytree import tree_flatten, tree_unflatten
from ._fsdp_collectives import (
    AllGatherResult,
    foreach_all_gather,
    foreach_all_gather_copy_out,
    foreach_reduce_scatter,
)
from ._fsdp_common import FSDPMeshInfo, HSDPMeshInfo, TrainingState
from ._fsdp_param import FSDPParam, ParamModuleInfo, ShardedState


"""
[Note: Overlapping all-gather copy-in and all-gather]
For implicit forward prefetching, we want to overlap the next copy-in with the
current all-gather. We do so using a separate copy-in stream. However, since
we have the all-gather input as a view into the output, we must make sure to
copy into different memory from the current all-gather's output. Thus, we keep
a reference to the current all-gather's output and have the next FSDP parameter
group free it after its copy-in. Finally, we have the last FSDP state flush the
reference to avoid holding onto memory after forward.
"""


class FSDPCommContext:
    """This has the communication state shared across FSDP states/parameter groups."""

    def init(self):
        # Setting the all-gather/reduce-scatter streams to be higher priority
        # can help avoid some issues where their copies in/out are delayed and
        # block computation
        high_priority = -1
        # All-gather state and copy-in stream allow overlapping the next
        # copy-in with the current all-gather in forward; copy-in overlaps with
        # reduce-scatter in backward without the separate copy-in stream
        self.all_gather_copy_in_stream = torch.cuda.Stream(priority=high_priority)
        self.all_gather_state: Optional[AllGatherState] = None
        # All-gather stream allows overlapping next all-gather with current
        # forward compute
        self.all_gather_stream = torch.cuda.Stream(priority=high_priority)
        # Reduce-scatter stream gives separate execution "thread" for post-
        # backward logic like pre/post-gradient division and reduce-scatter
        self.reduce_scatter_stream = torch.cuda.Stream(priority=high_priority)

    def get_all_gather_streams(
        self, training_state: TrainingState
    ) -> Tuple[torch.cuda.Stream, torch.cuda.Stream]:
        if training_state in (TrainingState.FORWARD, TrainingState.PRE_BACKWARD):
            # Use separate streams for implicit prefetching
            return self.all_gather_copy_in_stream, self.all_gather_stream
        current_stream = torch.cuda.current_stream()
        return current_stream, current_stream


# See [Note: Overlapping all-gather copy-in and all-gather]
class AllGatherState(NamedTuple):
    all_gather_result: AllGatherResult
    event: torch.cuda.Event  # all-gather copy-out


class FSDPParamGroup:
    """This class represents a parameter group to communicate together."""

    def __init__(
        self,
        params: List[nn.Parameter],
        module: nn.Module,
        mesh_info: FSDPMeshInfo,
        device: torch.device,
    ):
        self.module = module  # permit ref cycle because 1:1 lifetime
        param_module_infos = _get_param_module_infos(params, module)
        self.fsdp_params = [
            FSDPParam(param, module_info, mesh_info, device)
            for param, module_info in zip(params, param_module_infos)
        ]
        self.mesh_info = mesh_info
        self.device = device
        self._training_state = TrainingState.IDLE
        # Group's sharded state always matches its parameters' sharded states
        self._sharded_state = ShardedState.SHARDED
        self._init_mp_dtypes()
        self._module_fqn: Optional[str] = None  # prefixed from root module

        # - Communication and communication/computation overlap
        self.comm_ctx = FSDPCommContext()
        self._init_grad_divide_factors()

        # - CUDA events for stream synchronization
        # Holds the all-gather output buffer, sync objects, and metadata
        self._all_gather_result: Optional[AllGatherResult] = None
        # Holds the reduce-scatter view-out CUDA event that marks the end of
        # the group's post-backward (e.g. reduce-scatter and div), which should
        # be waited on at the end of backward
        self._reduce_scatter_view_out_event: Optional[torch.cuda.Event] = None

    # Initialization #
    def _init_mp_dtypes(self) -> None:
        orig_dtypes = {fsdp_param.orig_dtype for fsdp_param in self.fsdp_params}
        if len(orig_dtypes) != 1:
            # This can be relaxed if we copy-out for the reduce-scatter
            raise AssertionError(
                f"FSDP expects uniform original parameter dtype but got {orig_dtypes}"
            )
        self._orig_dtype = next(iter(orig_dtypes))
        self._param_dtype = self._orig_dtype

    def _init_grad_divide_factors(self):
        """
        For N data parallel workers, each worker computes g_i, and they
        collectively reduce to compute (g_1 + ... + g_N) / N. To avoid overflow
        and underflow, we divide by ~sqrt(N) before and after the reduction.
        """
        data_parallel_world_size = 1
        data_parallel_world_size *= self.mesh_info.shard_mesh_size
        if isinstance(self.mesh_info, HSDPMeshInfo):
            data_parallel_world_size *= self.mesh_info.replicate_mesh_size
        factor: int = 1
        while (
            data_parallel_world_size % factor == 0
            and data_parallel_world_size / factor > factor
        ):
            factor *= 2
        self._grad_predivide_factor: float = float(factor)
        self._grad_postdivide_factor: float = (
            data_parallel_world_size / self._grad_predivide_factor
        )

    # Runtime #
    def unshard(self, async_op: bool = False):
        if self._all_gather_result is not None:  # already called, pending wait
            return
        if self._sharded_state == ShardedState.UNSHARDED:
            return  # no-op
        self._all_gather_result = foreach_all_gather(
            self.fsdp_params,
            self._all_gather_process_group,
            async_op,
            *self.comm_ctx.get_all_gather_streams(self._training_state),
            self.device,
            self._param_dtype,
        )

    def wait_for_unshard(self):
        """
        1. In forward with implict prefetching, to overlap the current copy-out
        with the next all-gather, we save a reference to the current all-gather
        result to free after the next copy-out.
        2. Otherwise (explicit prefetching or in backward), we free the
        all-gather result immediately after the current copy-out since we can
        already overlap the current copy-out with the previous reduce-scatter.
        """
        if not self._all_gather_result:
            return  # no preceding unshard
        if self._training_state == TrainingState.FORWARD:  # implicit prefetch
            if prev_all_gather_state := self.comm_ctx.all_gather_state:
                self._wait_all_gather_streams_on_event(prev_all_gather_state.event)
                self.comm_ctx.all_gather_state = None  # free the all-gather result
        foreach_all_gather_copy_out(
            self._all_gather_result, self.fsdp_params, self._all_gather_process_group
        )
        for fsdp_param in self.fsdp_params:
            fsdp_param.init_unsharded_param()  # no-op after 1st call
        self._to_unsharded()
        all_gather_copy_out_event = torch.cuda.Event()
        all_gather_copy_out_event.record()
        if self._training_state == TrainingState.FORWARD:
            self.comm_ctx.all_gather_state = AllGatherState(
                self._all_gather_result, all_gather_copy_out_event
            )
        else:
            self._wait_all_gather_streams_on_event(all_gather_copy_out_event)
        self._all_gather_result = None  # free unless saved in `all_gather_state`

    def _wait_all_gather_streams_on_event(self, event: torch.cuda.Event):
        self.comm_ctx.all_gather_copy_in_stream.wait_event(event)
        self.comm_ctx.all_gather_stream.wait_event(event)

    def reshard(self):
        self._to_sharded()

    def pre_forward(
        self, module: nn.Module, args: Tuple[Any, ...], kwargs: Dict[str, Any]
    ) -> Tuple[Tuple[Any, ...], Dict[str, Any]]:
        with torch.profiler.record_function("FSDP::pre_forward"):
            self._training_state = TrainingState.FORWARD
            self.unshard()
            self.wait_for_unshard()
            args, kwargs = self._register_post_backward_hook(args, kwargs)
            return args, kwargs

    def post_forward(self, module: nn.Module, input: Any, output: Any):
        with torch.profiler.record_function("FSDP::post_forward"):
            self.reshard()
            self._training_state = TrainingState.IDLE
            return output

    def pre_backward(self, *unused: Any):
        with torch.profiler.record_function("FSDP::pre_backward"):
            self._training_state = TrainingState.PRE_BACKWARD
            self.unshard()  # no-op if prefetched
            self.wait_for_unshard()

    def _post_backward(self, *unused: Any):
        self._training_state = TrainingState.POST_BACKWARD
        with torch.profiler.record_function("FSDP::post_backward_reshard"):
            # Save the autograd-computed gradients before resharding to only
            # access the unsharded parameters when their data is present
            fsdp_params_with_grad: List[FSDPParam] = []
            unsharded_grads: List[torch.Tensor] = []
            for fsdp_param in self.fsdp_params:
                if fsdp_param.unsharded_param.grad is not None:
                    fsdp_params_with_grad.append(fsdp_param)
                    unsharded_grads.append(fsdp_param.unsharded_grad_data)
                    fsdp_param.unsharded_param.grad = None
            self.reshard()
        if len(fsdp_params_with_grad) == 0:
            return
        with torch.profiler.record_function("FSDP::post_backward_reduce"):
            self._reduce_scatter_view_out_event = foreach_reduce_scatter(
                fsdp_params_with_grad,
                unsharded_grads,
                self._reduce_scatter_process_group,
                self.comm_ctx.reduce_scatter_stream,
                self._orig_dtype,
                self._orig_dtype,
                self.device,
                self._grad_predivide_factor,
                self._grad_postdivide_factor,
            )

    def finalize_backward(self):
        if self._sharded_state == ShardedState.UNSHARDED:
            # Run post-backward here since the forward inputs did not require
            # gradient, so the post-backward hook did not run
            self._post_backward()
        if self._reduce_scatter_view_out_event is not None:
            torch.cuda.current_stream().wait_event(self._reduce_scatter_view_out_event)
            self._reduce_scatter_view_out_event = None
        self._training_state = TrainingState.IDLE

    # Utilities #
    def _to_sharded(self):
        if self._sharded_state != ShardedState.SHARDED:
            for fsdp_param in self.fsdp_params:
                fsdp_param.to_sharded()
            self._sharded_state = ShardedState.SHARDED

    def _to_unsharded(self):
        if self._sharded_state != ShardedState.UNSHARDED:
            for fsdp_param in self.fsdp_params:
                fsdp_param.to_unsharded()
            self._sharded_state = ShardedState.UNSHARDED

    # Hook Registration #
    def _register_post_backward_hook(
        self, args: Tuple[Any, ...], kwargs: Dict[str, Any]
    ) -> Tuple[Tuple[Any, ...], Dict[str, Any]]:
        if not torch.is_grad_enabled():
            return args, kwargs
        args_list, args_spec = tree_flatten(args)
        kwargs_list, kwargs_spec = tree_flatten(kwargs)
        args_kwargs_list = list(args_list) + list(kwargs_list)
        inp_tensor_indices: List[int] = []
        inp_tensors: List[torch.Tensor] = []
        for i, obj in enumerate(args_kwargs_list):
            if torch.is_tensor(obj) and obj.requires_grad:
                inp_tensor_indices.append(i)
                inp_tensors.append(obj)
        if len(inp_tensors) == 0:
            return args, kwargs  # no tensors that require gradients
        inp_tensors = RegisterPostBackwardFunction.apply(self, *inp_tensors)
        for inp_tensor_idx, inp_tensor in zip(inp_tensor_indices, inp_tensors):
            args_kwargs_list[inp_tensor_idx] = inp_tensor
        args_list = args_kwargs_list[: len(args_list)]
        kwargs_list = args_kwargs_list[len(args_list) :]
        args = tree_unflatten(args_list, args_spec)
        kwargs = tree_unflatten(kwargs_list, kwargs_spec)
        return args, kwargs

    # Properties #
    @property
    def _all_gather_process_group(self) -> dist.ProcessGroup:
        mesh_info = self.mesh_info
        assert isinstance(mesh_info, FSDPMeshInfo)
        return mesh_info.shard_process_group

    @property
    def _reduce_scatter_process_group(self) -> dist.ProcessGroup:
        mesh_info = self.mesh_info
        assert isinstance(mesh_info, FSDPMeshInfo)
        return mesh_info.shard_process_group


def _get_param_module_infos(
    params: List[nn.Parameter], module: nn.Module
) -> List[ParamModuleInfo]:
    """
    Shared parameter: lin1.weight = lin2.weight
    Shared module: mlp.lin1 = mlp.lin2
    We do not remove duplicates when traversing both modules and parameters to
    find shared modules' parameters and shared parameters within a module.
    """
    params_set = set(params)
    param_to_module_info: Dict[nn.Parameter, ParamModuleInfo] = {}
    for _, submodule in module.named_modules(remove_duplicate=False):
        for param_name, param in _named_parameters_with_duplicates(
            submodule, recurse=False
        ):
            if param in params_set:
                if param not in param_to_module_info:
                    param_to_module_info[param] = ParamModuleInfo(submodule, param_name)
                else:
                    param_to_module_info[param].shared_modules.append(submodule)
                    param_to_module_info[param].shared_param_names.append(param_name)
    if len(param_to_module_info) != len(params):
        raise AssertionError(f"Some parameters are not in the module tree of {module}")
    return [param_to_module_info[param] for param in params]


class RegisterPostBackwardFunction(torch.autograd.Function):
    @staticmethod
    def forward(ctx, param_group: FSDPParamGroup, *inputs: torch.Tensor):
        # All tensors in `inputs` should require gradient
        ctx.param_group = param_group
        return inputs

    @staticmethod
    def backward(ctx, *grads: torch.Tensor):
        ctx.param_group._post_backward()
        return (None,) + grads
