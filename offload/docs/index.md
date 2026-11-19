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
context creation, queries, and destruction.
It enumerates DXCore adapters supporting `D3D12_GRAPHICS` and requests a D3D12
device with minimum feature level 11.0 for each adapter. Device names, LUID-based UIDs,
and native `ID3D12Device` handles are available. Compute-only adapters, queues,
memory operations, image loading, and shader execution are not supported yet.
This is not an OpenMP target.

Each context accepts exactly one device entry and retains a COM reference to its
`ID3D12Device`. Contexts for the same device are distinct liboffload objects but
share the native device; `OL_CONTEXT_INFO_NATIVE_HANDLE` returns that borrowed
`ID3D12Device` pointer. Destroying a context releases its reference without
invalidating other contexts or the device.

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
enable the real-device discovery, context lifecycle, and repeated
initialization/shutdown test.
The missing-runtime test does not require a DirectX runtime or GPU.
The OffloadAPI initialization, platform, and context tests can also run. Tests
requiring queues, memory operations, or shader execution are not supported yet.

# Indices and tables

- {ref}`genindex`
- {ref}`modindex`
- {ref}`search`

