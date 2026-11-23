# Experimental parallel SYCL DirectX demo

`offload/examples/directx-sycl/vector-add.cpp` is a real single-source SYCL
application using LLVM's in-tree libsycl and the DirectX liboffload plugin.
The host uses an in-order `sycl::queue`, device USM, queued `memcpy`,
`parallel_for`, and `wait_and_throw`. It contains no liboffload calls, handwritten shaders,
manifests, binary registration, or `#embed`.

Clang's `-fsycl --offload-targets=dxil-pc-shadermodel6.0-compute` pipeline
generates the lambda capture layout and device bitcode. `clang-linker-wrapper`
links device inputs through `clang-sycl-linker`, which splits the module per
kernel, lowers the resource-backed ABI, emits and signs DXIL, and packages each
kernel's launch manifest and SYCL symbol table. The host linker embeds and
registers the resulting images. libsycl preserves each
DXIL image's manifest when validating and loading it through liboffload.
The backend is exposed as `sycl::backend::ext_llvm_directx`. Only device USM is
supported; host and shared USM aspects are reported as unavailable.

This is **restricted parallel SYCL support**, not full SYCL or OpenCL support.
Vector addition uses `nd_range<1>` with 64 work-items per workgroup and one
element per work-item. Work-item IDs, local/global ranges, group IDs/ranges,
linear IDs, and 64-bit global offsets use a reusable target/runtime
[NDRange ABI](directx-manifest.md#ndrange-dispatch-abi), including 2D/3D
dimension ordering. Subgroup builtins remain explicitly unsupported.
The captured kernel accepts only flat buffer
pointers and 32-bit integer/float scalars, subject to the
[capture ABI restrictions](directx-manifest.md#restricted-capture-record-abi).
The GNU `reqd_work_group_size(x,y,z)` attribute on a kernel lambda or functor's
call operator fixes its native workgroup shape. Host local sizes must match;
this is not runtime workgroup specialization. Without the attribute, the
compiler currently uses one thread per workgroup, allowing `single_task`
and range submissions within native dispatch limits. General local-size
selection and specialization remain future work.

SYCL registration can contain multiple images, but each DirectX image describes
one kernel, and libsycl loads each as a separate program. Default relocatable
device compilation supports separate COFF objects and translation units with
multiple kernels or no kernels. The final device link must contain at least
one kernel. `-fno-gpu-rdc` finalizes each translation unit during compilation,
so each such unit must contain a kernel. DirectX currently requires per-kernel
splitting; explicit `translation_unit` and `link_unit` split modes are rejected.

The example uses standard compiler options rather than custom device build
commands. This does not add an OpenCL host API, OpenCL C kernel support, or
OpenMP integration.

## Build on Windows

Use an x64 Visual Studio developer command prompt with CMake and Ninja.
Provide a modified compiler build with the DirectX target and `clang`, `opt`,
`llc`, `llvm-offload-binary`, `clang-sycl-linker`, `clang-linker-wrapper`, and
`clang-resource-headers` built, as described
for the [C++ demo](directx-demo.md#build-on-windows).
Also provide the Release LLVM support-library build used by the
[native runtime instructions](index.md#native-windows-build-and-validation).
These may be the same LLVM build. The runtime and host application must use
the release DLL CRT (`/MD`) and matching SYCL headers.

From the repository root:

```bat
set COMPILER_BUILD=D:\builds\llvm-directx-cpp
set LLVM_LIB_BUILD=D:\builds\llvm-directx-release
set RUNTIME_BUILD=D:\builds\directx-runtimes
set DXV=D:\tools\dxc\bin\dxv.exe

cmake -S runtimes -B "%RUNTIME_BUILD%" -G Ninja ^
  -DCMAKE_BUILD_TYPE=Release ^
  -DCMAKE_C_COMPILER=clang-cl -DCMAKE_CXX_COMPILER=clang-cl ^
  "-DLLVM_DIR=%LLVM_LIB_BUILD%\lib\cmake\llvm" ^
  "-DLLVM_BINARY_DIR=%LLVM_LIB_BUILD%" ^
  "-DLLVM_ENABLE_RUNTIMES=offload;libsycl" ^
  -DBUILD_LIBOMPTARGET=OFF -DLIBOMPTARGET_PLUGINS_TO_BUILD=directx
cmake --build "%RUNTIME_BUILD%" --target LLVMSYCL sycl-headers sycl-ls

cmake -S offload\examples\directx-sycl -B build-directx-sycl -G Ninja ^
  -DCMAKE_BUILD_TYPE=Release ^
  "-DCMAKE_CXX_COMPILER=%COMPILER_BUILD%\bin\clang-cl.exe" ^
  "-DOFFLOAD_RUNTIME_BUILD=%RUNTIME_BUILD%" ^
  "-DSYCL_INCLUDE_DIR=%LLVM_LIB_BUILD%\include" ^
  "-DSYCL_RUNTIME_DLL=%LLVM_LIB_BUILD%\bin\LLVMSYCL.dll" ^
  "-DDXIL_VALIDATOR=%DXV%"
cmake --build build-directx-sycl
```

Device compilation uses `-O2` to inline the SYCL index/range builders and
scalarize their temporary objects before the restricted DirectX kernel ABI
pass. Out-of-line device helper calls and arbitrary device object storage
remain unsupported; a successful C++ parse alone does not establish DXIL
compatibility. Every generated shader is checked by the DXIL validator.

The driver discovers the Windows SDK and MSVC headers for both compilations.
The host compiler must contain this branch's SYCL/DirectX changes, not an
unmodified installed Clang. Use DXIL validator 1.8 or newer.

The same application can be compiled directly, without CMake:

```bat
"%COMPILER_BUILD%\bin\clang-cl.exe" /nologo /MD /EHsc ^
  /clang:-fsycl /clang:--offload-targets=dxil-pc-shadermodel6.0-compute ^
  "/I%LLVM_LIB_BUILD%\include" ^
  /clang:-Xoffload-linker "/clang:--dxil-validator=%DXV%" ^
  offload\examples\directx-sycl\vector-add.cpp /Fedirectx-sycl-vector-add.exe ^
  /link "/libpath:%RUNTIME_BUILD%\lib"
copy "%LLVM_LIB_BUILD%\bin\LLVMSYCL.dll" .
copy "%RUNTIME_BUILD%\lib\LLVMOffload.dll" .
```

Without `--dxil-validator`, `clang-sycl-linker` searches for `dxv` beside itself
and then on `PATH`. Missing tools and validator failures fail the build.
For separate compilation, use the same SYCL target options with `-c /Fo...`
and again when linking the resulting objects. Do not link offload objects
directly with `link.exe` or `lld-link.exe`: the compiler driver must run the
device linker and registration wrapper.

Only `directx-sycl-vector-add.exe`, `LLVMSYCL.dll`, and `LLVMOffload.dll` are
needed at execution time, in addition to the Visual C++ runtime and a compatible
Windows D3D12/DXCore installation. The build copies both LLVM runtime DLLs
beside the executable. Neither the intermediate package nor compiler/validator
tools are needed on the destination machine.

## Run and verify

```powershell
& .\build-directx-sycl\directx-sycl-vector-add.exe --list
& .\build-directx-sycl\directx-sycl-vector-add.exe 0 65537
```

The optional positional arguments select the zero-based DirectX adapter
(default `0`) and element count (default `65537`, range `1` through `65537`).
Run on the NVIDIA, AMD, and Intel indices printed by `--list`.
Software adapters may also appear and are not hardware demonstrations.

Success reports the chosen device and `PASS` after comparing every result
against the CPU sum. Try counts `1`, `65`, and `65537`.
Invalid input, SYCL errors, verification failures, and cleanup failures produce
a nonzero exit status. Host arrays stay alive until queued transfers complete.
The host rounds the global range to a multiple of 64 and the kernel checks
the original element count, so partial final groups cannot overrun allocations.
This is a correctness example, not a benchmark; GPU event profiling in libsycl
remains future work. The existing C++/liboffload demo reports GPU timing.

On Windows, runtime shutdown releases programs and cached contexts before
DLL detachment. DirectX driver teardown must not occur under the loader lock.

## Multidimensional and multi-kernel execution tests

The native geometry probe at
`offload/unittests/Plugins/DirectX/Inputs/sycl-ndrange.cpp` builds through the
same compiler/signing/embedding pipeline as the application:

```bat
cmake -S offload\examples\directx-sycl -B build-directx-sycl ^
  -DDIRECTX_SYCL_BUILD_TESTS=ON
cmake --build build-directx-sycl
ctest --test-dir build-directx-sycl --output-on-failure
```

CTest uses device index 0. To exercise each selected hardware adapter:

```powershell
foreach ($device in 0, 1, 2) {
  foreach ($dims in 1, 2, 3) {
    & ".\build-directx-sycl\directx-sycl-ndrange-$dims.exe" $device
    if ($LASTEXITCODE -ne 0) { throw "SYCL geometry test failed" }
  }
  & .\build-directx-sycl\directx-sycl-multi-kernel.exe $device
  if ($LASTEXITCODE -ne 0) { throw "SYCL multi-kernel test failed" }
}
```

Use indices appropriate to the machine. These tests verify global/local/group
IDs and ranges, all three linear index forms, changed offsets above 32 bits,
in-order queued upload/dispatch/readback, and trailing allocation guards.
The liboffload native geometry test separately verifies changing group counts,
blocking versus asynchronous submissions, and property snapshots.
The multi-kernel probe separately compiles a two-kernel translation unit and
a host-only translation unit, then verifies ordered initialization/scaling
and trailing guards after compiler-managed device linking and image registration.

## Next steps

Implement workgroup specialization and broaden the device memory model.
OpenCL C address spaces, builtins and NDRange support are a separate frontend
milestone that can reuse the target NDRange ABI; standard OpenCL applications
additionally need a host runtime. General pointers, aggregates, local accessors,
subgroups, atomics, reductions, and cross-image device calls still need
compiler/runtime support before this can claim full SYCL or OpenCL compliance.
