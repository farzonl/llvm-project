% Offload documentation master file, created by
% sphinx-quickstart on Fri Jul  4 14:59:13 2025.
% You can adapt this file completely to your liking, but it should at least
% contain the root `toctree` directive.

# Welcome to Offload's documentation!

:::{toctree}
:caption: 'Contents:'
:maxdepth: 2

offload-api
:::

# Experimental DirectX plugin

The opt-in `directx` plugin supports liboffload device discovery and single-device
context creation, queries, and destruction, explicit device-buffer allocation,
allocation queries and freeing, and compute queues with fence-based synchronization.
It enumerates DXCore adapters supporting `D3D12_GRAPHICS` and requests a D3D12
device with minimum feature level 11.0 for each adapter. Device names, LUID-based UIDs,
and native `ID3D12Device` handles are available. Compute-only adapters, events,
host callbacks, host/managed allocations, copies, image loading, and shader
execution are not supported yet.
This is not an OpenMP target.

Each context accepts exactly one device entry and retains a COM reference to its
`ID3D12Device`. Contexts for the same device are distinct liboffload objects but
share the native device; `OL_CONTEXT_INFO_NATIVE_HANDLE` returns that borrowed
`ID3D12Device` pointer. Destroying a context releases its reference without
invalidating other contexts or the device.

Device allocations use committed, default-heap D3D12 buffers suitable for raw
SRV/UAV views. Returned pointers encode GPU virtual addresses; they must not be
dereferenced on the host or treated as arbitrary HLSL pointers. Interior pointers
are accepted by allocation queries, but freeing requires the allocation base.
Each context tracks its own live allocations separately from cached backing
resources in the common memory pool. Queries report the requested size, not
alignment padding or pooled capacity. Allocations honor the requested power-of-two
alignment.
Context teardown releases any remaining buffers.

Each queue owns a compute `ID3D12CommandQueue` and a completion fence.
`olSyncQueue` signals and waits for a new fence value without replacing the queue.
Waiting uses `ID3D12Fence::SetEventOnCompletion` with a null event to block the
calling thread rather than polling. Queue queries inspect the last signaled
plugin fence value without submitting new work.
`OL_QUEUE_INFO_NATIVE_HANDLE` returns the borrowed native command queue, valid
until the liboffload queue is destroyed. Native submissions are included in the
next `olSyncQueue`, but completion queries cannot observe native work submitted
after the last plugin fence marker. Callers must synchronize such work before
relying on a completion query.
Fence failures, device removal, and exhausted fence timelines produce errors.

Select it with `-DLIBOMPTARGET_PLUGINS_TO_BUILD=directx`; it is not included in
`all`. Windows builds use the Windows SDK headers and load `dxcore.dll` and
`d3d12.dll`. Linux builds require DirectX-Headers; set
`LIBOMPTARGET_DEP_DIRECTX_INCLUDE_DIR` to its `include` directory if necessary.
Linux runtime testing uses [Nacelle](https://github.com/farzonl/Nacelle).

`OFFLOAD_DIRECTX_DXCORE_LIBRARY` and `OFFLOAD_DIRECTX_D3D12_LIBRARY` override the
runtime library paths. On Linux, both default to `libnacelle_d3d12.so`. Missing
libraries, missing entry points, and failed driver calls produce errors rather
than being reported as successful discovery with no devices. Runtime libraries
remain loaded until process exit.

Build `directx.unittests` and run
`llvm-lit --filter='directx[.]unittests' <runtime-build>/offload/test/unit`.
On Linux, set both runtime path variables to the Nacelle shared library to
enable real-device discovery, context, buffer and queue lifecycle, allocation
isolation, alignment, real fence waiting, and repeated initialization/shutdown
tests. Nacelle currently submits queue work synchronously; its tests do not
validate native Windows asynchronous submission.
The missing-runtime test does not require a DirectX runtime or GPU.
The OffloadAPI initialization, platform, context, and basic queue tests can also
run. Queue tests requiring events or host callbacks, and tests requiring
host/managed allocations, copies, or shader execution, are not supported yet.

# Indices and tables

- {ref}`genindex`
- {ref}`modindex`
- {ref}`search`

