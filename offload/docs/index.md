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
directx-demo
directx-sycl
:::

# Experimental DirectX plugin

The opt-in `directx` plugin supports liboffload device discovery and single-device
context creation, queries, and destruction, explicit device-buffer allocation,
allocation queries and freeing, compute queues with fence-based synchronization,
explicit buffer copies, validated single-entry DXIL program loading/unloading,
asynchronous compute dispatch on explicit queues, ordered host callbacks,
pattern fills, and synchronization/profiling events.
It enumerates DXCore adapters supporting `D3D12_GRAPHICS` and requests a D3D12
device with minimum feature level 11.0 for each adapter. Device names, LUID-based UIDs,
and native `ID3D12Device` handles are available. Compute-only adapters and
host/managed allocations are not supported yet.
This is not an OpenMP target.

The [standalone C++ vector-add demo](directx-demo.md) uses an experimental
resource-backed DirectX C++ kernel ABI with compiler-generated bindings and
launch metadata. It supports indexed 32-bit buffer accesses and a subset of
`gpuintrin.h` and 32-bit scalar arguments, without application-authored HLSL
or manifests. Its build compiles host and device code from one source, packages
with `llvm-offload-binary`, and embeds the image with Clang's `#embed`. This is
build-system single-source compilation; automatic driver/linker integration,
general pointers, and OpenMP DeviceRTL integration remain future work.

A restricted SYCL capture-record proof also generates field-layout metadata
from Clang and executes captured buffer/scalar arguments through liboffload.
It preserves the separate-argument ABI.
See the [capture validation test](directx-demo.md#validate-sycl-generated-captures-separately).

The [parallel SYCL demo](directx-sycl.md) uses the in-tree libsycl with
DirectX device USM and compiler-generated image registration. Its standard
`-fsycl` pipeline links separate device inputs, splits per kernel, signs DXIL,
and generates packaged manifests without custom device build commands. It executes a
captured C++ lambda through fixed-workgroup `sycl::queue::parallel_for`.
Its reusable NDRange ABI supports multidimensional IDs/ranges and 64-bit
global offsets. OpenCL and dynamic workgroup specialization are not supported yet.

The [experimental launch manifest](directx-manifest.md) defines versioned JSON
metadata for packaged shaders. Its parser and LLVM-native DXIL reflection/ABI
validation helper are implemented, including explicit scalar field layouts and
feature-declaration checks. Loading checks the device shader model and supported
optional features before retaining owned bytecode and a manifest. It serializes
the canonical root-signature 1.0 layout through the D3D12 runtime and retains a
native root signature per program. Embedded root signatures are still rejected.
Kernel lookup/enumeration expose the manifest's exact entry and lazily create a
compute pipeline, owned by its program. Missing entries return `NOT_FOUND`.
Static `groupshared` scalar/array storage is reflected from executable DXIL,
bounded to 32 KiB per group, and reported in kernel metadata. Group barriers
execute as compiled shader instructions.
Internal helpers prepare bounded raw-buffer descriptors, pack scalar constants,
and record canonical root bindings, retaining the native resources and a
shader-visible descriptor heap. They enforce 16-byte raw-view alignment and
exclude allocation padding and pool capacity. Launches resolve pointers only in
the program's context, require exact compiled thread-group dimensions, transition
resources to their SRV/UAV states and back to `COMMON`, and retain all native
objects through fence completion. Null queues block; explicit queues submit
asynchronously and require queue/event synchronization before observing
readbacks or freeing allocations. Mixed
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
Null-queue copies block until completion. Explicit-queue copies are asynchronous;
their staging buffers are retained until execution and host readbacks complete.

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
tests can also run, including profiling-event creation, ordered host callbacks,
and device-buffer pattern fills. Tests requiring host/managed allocations or
general-purpose device binaries are not supported yet.
The DirectX-specific tests exercise packaged compute shaders.
Device information reports DXCore vendor/driver identity and memory capacity,
D3D12 group and per-axis dispatch limits, and native floating-point support.
Memory capacity describes shared system memory for integrated adapters and
dedicated memory for discrete adapters, falling back to shared memory when no
dedicated memory exists. It is not a guarantee that an allocation of that size
will succeed.
Static shared-memory usage is supported in the experimental launch ABI; dynamic
shared memory remains unsupported. Unknown compute-unit counts, hardware clock rates,
floating-point mode flags, variable wave widths, and the unrepresentable
32-bit aggregate work-item limit remain explicitly unsupported rather than
being guessed. The full generic device-information suite is therefore not yet
supported.
Native tests cover event-driven retirement, dependency failure propagation,
callback ordering, profiling, and asynchronous lifetime/concurrency behavior.
Portable emulators do not validate native asynchronous execution or Windows
cross-queue clock calibration.

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
Explicit-queue copies and dispatches are asynchronous; synchronize the queue or
an event before reading host results or reusing allocations. Null-queue operations
remain blocking and do not implicitly synchronize other queues.
Zero-group launches still validate arguments, but do not issue a native empty
`Dispatch` or wait for explicit-queue work.
Native validation is necessary: Nacelle's resource barriers are currently no-ops
and its execution is synchronous, so Linux success alone cannot establish
Windows resource-state or submission-lifetime correctness.
Tests that deliberately gate pending GPU work run only with the default native
Windows runtime. They are skipped on Linux and with runtime-library overrides;
portable discovery, execution, event, and ABI tests still run with Nacelle.

# Indices and tables

- {ref}`genindex`
- {ref}`modindex`
- {ref}`search`

