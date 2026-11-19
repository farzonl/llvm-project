% Offload documentation master file, created by
% sphinx-quickstart on Fri Jul  4 14:59:13 2025.
% You can adapt this file completely to your liking, but it should at least
% contain the root `toctree` directive.

# Welcome to Offload's documentation!

:::{toctree}
:caption: 'Contents:'
:maxdepth: 2

offload-api
directx-manifest
:::

# Experimental DirectX plugin

The opt-in `directx` plugin supports liboffload device discovery and single-device
context creation, queries, and destruction, explicit device-buffer allocation,
allocation queries and freeing, compute queues with fence-based synchronization,
explicit buffer copies, validated single-entry DXIL program loading/unloading,
and synchronous compute dispatch.
It enumerates DXCore adapters supporting `D3D12_GRAPHICS` and requests a D3D12
device with minimum feature level 11.0 for each adapter. Device names, LUID-based UIDs,
and native `ID3D12Device` handles are available. Compute-only adapters, events,
host callbacks, host/managed allocations, and asynchronous shader
execution are not supported yet.
This is not an OpenMP target.

The [experimental launch manifest](directx-manifest.md) defines versioned JSON
metadata for packaged shaders. Its parser and LLVM-native DXIL reflection/ABI
validation helper are implemented, including explicit scalar field layouts and
feature-declaration checks. Loading checks the device shader model and supported
optional features before retaining owned bytecode and a manifest. It serializes
the canonical root-signature 1.0 layout through the D3D12 runtime and retains a
native root signature per program. Embedded root signatures are still rejected.
Kernel lookup/enumeration expose the manifest's exact entry and lazily create a
compute pipeline, owned by its program. Missing entries return `NOT_FOUND`.
Static shared-memory reflection is deferred, so shaders using `groupshared`
storage are currently rejected rather than reporting incorrect kernel metadata.
Internal helpers prepare bounded raw-buffer descriptors, pack scalar constants,
and record canonical root bindings, retaining the native resources and a
shader-visible descriptor heap. They enforce 16-byte raw-view alignment and
exclude allocation padding and pool capacity. Launches resolve pointers only in
the program's context, require exact compiled thread-group dimensions, transition
resources to their SRV/UAV states and back to `COMMON`, and retain all native
objects through fence completion. Both null and explicit queues block. Mixed
SRV/UAV views of one resource, cooperative dispatch, and dynamic shared memory
remain unsupported.
Programs currently require exactly one entry in an uncompressed, version-3
OffloadBinary package; raw DXIL/bitcode and multi-entry packages are not accepted.

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

Host-to-device copies use upload heaps; device-to-host copies use readback heaps.
Both use native command lists and `CopyBufferRegion`, with default-heap buffers
returned to `COMMON` state after each copy. Same-device buffer copies are also
supported, including copies between regions of one buffer through a temporary
resource. Host-to-host copies on a queue wait for preceding queue work.
Copies currently block until completion even when a queue is supplied; events
and asynchronous staging-buffer retirement remain deferred.

Copy ranges are checked against live, requested allocation sizes, excluding pool
capacity and alignment padding. Resource offsets include both the requested byte
offset and any alignment adjustment to the native GPU virtual address.
Queued copies require allocations from the queue's context. Null-queue copies
use a device-owned synchronous queue and resolve live allocations across that
device's contexts. There is no native peer-to-peer copy support.
Native interop callers must return plugin buffers to `COMMON` before using them
in a liboffload copy, and synchronize work on other queues themselves.

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
enable real-device discovery, context, buffer, queue and program lifecycle, allocation
isolation, alignment, real fence waiting, exact buffer copy results and range
checks, and repeated initialization/shutdown tests. Nacelle currently submits
queue work synchronously; its tests do not
validate native Windows asynchronous submission.
The missing-runtime test does not require a DirectX runtime or GPU.
Manifest, shader validation, and feature-query unit tests also run without a
DirectX runtime or GPU.
Their checked-in DXIL fixtures are generated from the accompanying HLSL with
the documented Clang or DXC commands; running the tests does not require either
compiler.
The OffloadAPI initialization, platform, context, basic queue, and buffer copy
tests can also run. Tests requiring events, host callbacks, host/managed
allocations, or general-purpose device binaries are not supported yet.
The DirectX-specific tests exercise packaged compute shaders.
The full generic device-information suite is not yet supported: queries such as
vendor, driver version, and per-dimension work limits remain unimplemented.
Some generic queue and copy tests also require host callbacks even when their
names do not mention callbacks; those are outside this synchronous prototype.

When checking a statically linked Nacelle build with LeakSanitizer, preload its
shared library with `LD_PRELOAD` as well as setting the two runtime path
variables. This keeps its LLVM registration storage visible to the leak checker
through process teardown, rather than reporting allocations from an already
unloaded library. Keep `ASAN_OPTIONS=detect_leaks=1`; no leak suppressions are
needed.

## Native Windows build and validation

Use an x64 Visual Studio developer command prompt with a recent Windows SDK,
Clang, CMake, Ninja, and Python on `PATH`. Enable the Windows **Graphics Tools**
optional feature to install the D3D12 debug layers. From the repository root:

```bat
cmake -S llvm -B build-directx -G Ninja ^
  -DCMAKE_BUILD_TYPE=Release ^
  -DCMAKE_C_COMPILER=clang-cl -DCMAKE_CXX_COMPILER=clang-cl ^
  -DLLVM_TARGETS_TO_BUILD=Native -DLLVM_ENABLE_ASSERTIONS=ON ^
  -DLLVM_ENABLE_RUNTIMES=offload ^
  "-DRUNTIMES_CMAKE_ARGS=-DCMAKE_C_COMPILER=clang-cl;-DCMAKE_CXX_COMPILER=clang-cl" ^
  -DLIBOMPTARGET_PLUGINS_TO_BUILD=directx -DOFFLOAD_INCLUDE_TESTS=ON
cmake --build build-directx --target runtimes
cmake --build build-directx\runtimes\runtimes-bins --target LLVMOffload directx.unittests
```

Set both the LLVM and runtime compilers explicitly: a runtime sub-build can
otherwise select MSVC independently of the compiler used for LLVM.
No shader compiler or DXC library is needed to run the checked-in fixtures.
The plugin's shared RPC header is named `OffloadRPC.h` to avoid shadowing the
Windows SDK's `rpc.h` on case-insensitive filesystems. SDK GUID definitions
remain in `DirectXGuids.cpp`.

Run the test executable from PowerShell, adding its runtime DLL directory to
`PATH`:

```powershell
$runtime = (Resolve-Path .\build-directx\runtimes\runtimes-bins).Path
$env:PATH = "$runtime\lib;$env:PATH"
$env:OFFLOAD_DIRECTX_TEST_GPU_VALIDATION = "1"
$env:OFFLOAD_DIRECTX_TEST_DEVICE = "0"
& "$runtime\offload\unittests\Plugins\DirectX\directx.unittests.exe" `
  --gtest_output=xml:directx-device0.xml
```

`OFFLOAD_DIRECTX_TEST_GPU_VALIDATION` enables the debug layer, GPU-based
validation, and synchronized command-queue validation **before** device creation.
Device tests fail on stored D3D12 warnings, errors, or corruption messages,
including messages generated during teardown. Missing debug interfaces fail
the tests rather than silently disabling validation. These are test-harness
settings, not plugin runtime options.

`OFFLOAD_DIRECTX_TEST_DEVICE` selects a zero-based DirectX device index (default
`0`). Repeat the run for each hardware adapter, using separate XML output files;
each device test records its selected product name in the XML. An invalid or
unavailable index fails instead of falling back to another device.

The suite covers guarded buffer and scalar dispatches, explicit and null queues,
copy ranges and GPU virtual-address offsets, logical descriptor bounds, context
isolation, concurrent copies and launches, and repeated initialization/shutdown.
Zero-group launches still validate arguments and synchronize preceding queue
work, but do not issue a native empty `Dispatch`.
Native validation is necessary: Nacelle's resource barriers are currently no-ops
and its execution is synchronous, so Linux success alone cannot establish
Windows resource-state or submission-lifetime correctness.

# Indices and tables

- {ref}`genindex`
- {ref}`modindex`
- {ref}`search`

