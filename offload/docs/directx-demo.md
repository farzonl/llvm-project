# Single-source DirectX vector-add demo

This example demonstrates the experimental **liboffload** DirectX plugin,
not OpenMP `target` integration. The device computation is written in C++ using
`gpuintrin.h`. Clang generates resource bindings and launch metadata, emits
DXIL, and the build signs it with `dxv` and packages it using LLVM's
`llvm-offload-binary`. Clang's `#embed` embeds the package in the host executable.
No application-authored HLSL or JSON, example-specific packager, or runtime
package path is required.
The host executable links only to `LLVMOffload`, not
LLVM or DXC. Neither Clang nor a DXC library is needed to run the built demo.

The demo selects a DirectX adapter, uploads two float arrays on an explicit
queue, dispatches vector addition asynchronously, and enqueues readback.
A completion event makes the host output visible before CPU verification.
Profiling events bracket dispatch, excluding uploads and readback. A scalar
element-count argument bounds the kernel's accesses in a partial final group.
Allocations and verification cover exactly the requested element count; no
thread-group padding is needed.

Both host and device code live in `vector-add.cpp`. The build compiles it twice:
`__DIRECTX__` selects the GPU body, and the host compilation sees the same kernel
signature plus host code. Host argument marshalling derives from that signature
through a typed launcher, rather than handwritten argument-size arrays.
This is **build-system single-source compilation**, not yet automatic
Clang-driver splitting, compiler-generated host registration, or a C++ AMP,
CUDA, SYCL, or OpenMP frontend. Program creation and submission still use
liboffload.

Kernels require a fixed `reqd_work_group_size`, return `void`, and accept buffer
pointers with 32-bit integer/float accesses and 32-bit integer/float scalars.
Scalar arguments are packed consecutively into root constants at `b0, space0`,
up to 63 scalars (252 bytes). Const pointees use SRVs; writable pointees use UAVs.
Unused arguments, pointer escapes, general pointer chasing, atomic/volatile
buffer accesses, and non-inlined helper calls are rejected. Supported GPU
intrinsics expose thread/group IDs, fixed group dimensions, and group barriers;
grid-size, wave, and pointer-query operations are not supported yet.

Launch metadata is retained in intermediate kernel bitcode for the packager.
It is deliberately removed before final DXIL emission, since the DXIL validator
rejects unknown named metadata. Scalar cbuffer type annotations are retained
in executable DXIL, allowing validation without a `STAT` reflection part.
The packager adds required feature declarations
from the final container's compiler-generated shader feature flags.
The build runs Clang, `opt -dxil-kernel-abi`,
`llc`, `dxv`, and `llvm-offload-binary`, then embeds the package during the host
compile. Driver/linker-wrapper integration remains future work.

## Build on Windows

Use an x64 Visual Studio developer command prompt with CMake and Ninja.
First build the DirectX runtime as described in the
[native Windows instructions](index.md#native-windows-build-and-validation).
Also provide an upstream LLVM/Clang build with the DirectX target enabled,
including the `clang`, `opt`, `llc`, `llvm-offload-binary`, and
`clang-resource-headers` targets:
`-DLLVM_TARGETS_TO_BUILD="Native;DirectX" -DLLVM_ENABLE_PROJECTS=clang`.
The runtime-only LLVM build shown in those instructions does not build Clang
or the DirectX target.

Set these paths to your existing builds, then run from the repository root:

```bat
set LLVM_BUILD=D:\builds\llvm-directx-cpp
set RUNTIME_BUILD=D:\builds\llvm-directx\runtimes\runtimes-bins
set DXV=D:\tools\dxc\bin\dxv.exe

cmake -S offload\examples\directx-vector-add -B build-directx-demo -G Ninja ^
  -DCMAKE_BUILD_TYPE=Release ^
  -DCMAKE_C_COMPILER=clang-cl -DCMAKE_CXX_COMPILER=clang-cl ^
  "-DLLVM_DIR=%LLVM_BUILD%\lib\cmake\llvm" ^
  "-DOFFLOAD_RUNTIME_BUILD=%RUNTIME_BUILD%" ^
  "-DDEVICE_CLANG=%LLVM_BUILD%\bin\clang.exe" ^
  "-DDXIL_VALIDATOR=%DXV%"
cmake --build build-directx-demo
```

`OFFLOAD_RUNTIME_BUILD` must contain the generated
`offload\liboffload\API\OffloadAPI.h` and `lib\LLVMOffload.lib` / `LLVMOffload.dll`.
The host compiler must be Clang with `#embed` support; this example intentionally
uses it as a C++ extension. The host does not link LLVM libraries, so its build
configuration need not match the LLVM tools' build configuration.
The build copies the runtime DLL beside the demo executable.
`dxv` requires its matching DXC validator DLLs beside it when signing; this is
a build-time dependency only. Use validator version 1.8 or newer, supporting
shader model 6.0.

Build outputs include `directx-vector-add.exe`, `LLVMOffload.dll`, and an
intermediate `vector-add.bin`. Only the executable and DLL are needed on another
compatible Windows machine with D3D12/DXCore and shader-model-6.0 support.

## Run and verify

From PowerShell:

```powershell
& .\build-directx-demo\directx-vector-add.exe --list
& .\build-directx-demo\directx-vector-add.exe 0 1048589
```

The positional arguments are the optional zero-based DirectX device index
(default `0`) and optional element count (default `1048589`).
Select the NVIDIA, AMD, and Intel indices reported by `--list` and repeat the
second command for each. Enumeration may also include software adapters;
choose the named hardware adapters for a native demonstration.

Success prints the selected adapter, the verified element count, and GPU
dispatch milliseconds. Every output element is compared with its CPU sum.
Inputs use exactly representable binary fractions so exact float comparison is
intentional. API errors, invalid arguments, invalid embedded packages,
verification failures, invalid timing, and cleanup failures produce a nonzero
exit status rather than falling back to a different adapter.

Try counts `1`, `63`, `64`, `65`, and `1048589` to exercise small dispatches and
partial groups. Counts must be between `1` and `4194240`, the maximum supported
by the fixed 64-thread group and the D3D12 per-axis dispatch limit.
This is a correctness demo, not a benchmark: first-use pipeline compilation,
power state, and driver scheduling can affect timing.

## Validate shared memory separately

Vector addition does not use `groupshared`. The DirectX unit test
`DirectXProgramTest.ExecutesGroupSharedMemoryAndBarriers` verifies a separate
Clang-generated shader with 128 threads per group and 512 bytes of shared
storage. Its reductions and read/write phases use three group barriers, span
multiple native waves, and verify group isolation across 32 groups. It runs
with both blocking null queues and asynchronous explicit queues.

Enable `OFFLOAD_DIRECTX_TEST_GPU_VALIDATION=1`, select each hardware adapter
with `OFFLOAD_DIRECTX_TEST_DEVICE`, and run `directx.unittests` as described in
the native Windows instructions. The debug-layer tests also fail on stored
D3D12 warnings or errors. Reflection tests separately check the exact 32 KiB
boundary, oversized storage, legacy scalar padding, and kernel metadata.
