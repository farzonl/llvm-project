# Standalone DirectX vector-add demo

This example demonstrates the experimental **liboffload** DirectX plugin,
not OpenMP `target` integration. It compiles HLSL with upstream Clang, signs
the DXIL with `dxv`, and packages the shader and launch manifest using LLVM's
`OffloadBinary` writer. The host executable links only to `LLVMOffload`, not
LLVM or DXC. Neither Clang nor a DXC library is needed to run the built demo.

The demo selects a DirectX adapter, uploads two float arrays on an explicit
queue, dispatches vector addition asynchronously, and enqueues readback.
A completion event makes the host output visible before CPU verification.
Profiling events bracket dispatch, excluding uploads and readback. The shader
uses its bounded raw-buffer view to determine the element count, avoiding scalar
reflection and correctly handling a partially filled final thread group.

## Build on Windows

Use an x64 Visual Studio developer command prompt with CMake and Ninja.
First build the DirectX runtime as described in the
[native Windows instructions](index.md#native-windows-build-and-validation).
Also provide an upstream Clang build with the DirectX target enabled:
`-DLLVM_TARGETS_TO_BUILD="Native;DirectX" -DLLVM_ENABLE_PROJECTS=clang`.
The runtime-only LLVM build shown in those instructions does not build Clang
or the DirectX target.

Set these paths to your existing builds, then run from the repository root:

```bat
set LLVM_BUILD=D:\builds\llvm-hlsl
set RUNTIME_BUILD=D:\builds\llvm-directx\runtimes\runtimes-bins
set DXV=D:\tools\dxc\bin\dxv.exe

cmake -S offload\examples\directx-vector-add -B build-directx-demo -G Ninja ^
  -DCMAKE_BUILD_TYPE=Release ^
  -DCMAKE_C_COMPILER=clang-cl -DCMAKE_CXX_COMPILER=clang-cl ^
  "-DLLVM_DIR=%LLVM_BUILD%\lib\cmake\llvm" ^
  "-DOFFLOAD_RUNTIME_BUILD=%RUNTIME_BUILD%" ^
  "-DHLSL_CLANG=%LLVM_BUILD%\bin\clang.exe" ^
  "-DDXIL_VALIDATOR=%DXV%"
cmake --build build-directx-demo
```

`OFFLOAD_RUNTIME_BUILD` must contain the generated
`offload\liboffload\API\OffloadAPI.h` and `lib\LLVMOffload.lib` / `LLVMOffload.dll`.
Use a compatible x64 LLVM build for the small build-time packager.
The build copies the runtime DLL beside the demo executable.
`dxv` requires its matching DXC validator DLLs beside it when signing; this is
a build-time dependency only. Use a validator that supports shader model 6.0.

Build outputs include `directx-vector-add.exe`, `LLVMOffload.dll`, and
`vector-add.bin`. Those three files suffice to run on another compatible Windows
machine with D3D12/DXCore and shader-model-6.0 support.

## Run and verify

From PowerShell:

```powershell
& .\build-directx-demo\directx-vector-add.exe --list
& .\build-directx-demo\directx-vector-add.exe `
  .\build-directx-demo\vector-add.bin 0 1048589
```

The positional arguments are the package path, optional zero-based DirectX
device index (default `0`), and optional element count (default `1048589`).
Select the NVIDIA, AMD, and Intel indices reported by `--list` and repeat the
second command for each. Enumeration may also include software adapters;
choose the named hardware adapters for a native demonstration.

Success prints the selected adapter, the verified element count, and GPU
dispatch milliseconds. Every output element is compared with its CPU sum.
Inputs use exactly representable binary fractions so exact float comparison is
intentional. API errors, invalid arguments, missing/malformed packages,
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
