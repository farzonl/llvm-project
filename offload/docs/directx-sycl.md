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

## Conformance baseline

Working examples are not evidence of full SYCL 2020 conformance. The DirectX
compiler/backend and the in-tree libsycl API have separate gaps. The CTS adapter
at `libsycl/test/cts` measures unchanged upstream Khronos tests with a distinct
`LibSYCL` implementation identity. It does not select another implementation's
compatibility macros, remove assertions, or provide replacement SYCL APIs.

The upstream CTS implementation registry needs the small accompanying patch.
Its compiler/runtime discovery modules are supplied through `CMAKE_MODULE_PATH`;
the CTS checkout remains outside this repository.

From an x64 Visual Studio developer command prompt, using the build variables
above:

```bat
set CTS_SOURCE=D:\repos\SYCL-CTS
set CTS_BUILD=D:\builds\directx-sycl-cts
set LLVM_SOURCE=D:\repos\llvm-project

git clone https://github.com/KhronosGroup/SYCL-CTS.git "%CTS_SOURCE%"
git -C "%CTS_SOURCE%" checkout b74823b4179358851d7949d816e41e6f1a7e105a
git -C "%CTS_SOURCE%" submodule update --init
git -C "%CTS_SOURCE%" apply "%LLVM_SOURCE%\libsycl\test\cts\register-libsycl.patch"

cmake -S "%CTS_SOURCE%" -B "%CTS_BUILD%" -G Ninja ^
  -DCMAKE_BUILD_TYPE=Release ^
  "-DCMAKE_C_COMPILER=%COMPILER_BUILD%\bin\clang-cl.exe" ^
  "-DCMAKE_CXX_COMPILER=%COMPILER_BUILD%\bin\clang-cl.exe" ^
  "-DCMAKE_MODULE_PATH:PATH=%LLVM_SOURCE%\libsycl\test\cts" ^
  -DCMAKE_POLICY_VERSION_MINIMUM=3.5 ^
  -DSYCL_IMPLEMENTATION=LibSYCL ^
  -DSYCL_CTS_ENABLE_FULL_CONFORMANCE=ON ^
  -DSYCL_CTS_ENABLE_DEPRECATED_FEATURES_TESTS=ON ^
  -DSYCL_CTS_ENABLE_HALF_TESTS=ON ^
  -DSYCL_CTS_ENABLE_DOUBLE_TESTS=ON ^
  -DSYCL_CTS_ENABLE_EXT_ONEAPI_TESTS=OFF ^
  -DSYCL_CTS_ENABLE_KHR_TESTS=OFF ^
  -DSYCL_CTS_SYCL_NEXT_TESTS=OFF ^
  -DSYCL_CTS_ENABLE_OPENCL_INTEROP_TESTS=OFF ^
  "-DOFFLOAD_RUNTIME_BUILD=%RUNTIME_BUILD%" ^
  "-DSYCL_INCLUDE_DIR=%LLVM_LIB_BUILD%\include" ^
  "-DSYCL_RUNTIME_DLL=%LLVM_LIB_BUILD%\bin\LLVMSYCL.dll" ^
  "-DDXIL_VALIDATOR=%DXV%" ^
  -DSYCL_CTS_CTEST_DEVICE=NVIDIA

python "%LLVM_SOURCE%\libsycl\test\cts\measure.py" ^
  --build-dir "%CTS_BUILD%" --report-dir "%CTS_BUILD%\baseline" ^
  --devices NVIDIA AMD Intel --jobs 1
```

The policy minimum is needed by the CTS's older OpenCL-Headers submodule with
current CMake. Specify both host compilers: that submodule enables C even though
the CTS top-level project initially enables only C++. Type `CMAKE_MODULE_PATH`
as `PATH` so CMake normalizes Windows backslashes in nested compiler checks.
For a repeatable measurement, record the CTS revision and use the same test
configuration. OpenCL interoperability is disabled because this is a DirectX
backend, not because those tests pass. Half/double test compilation remains
enabled; unsupported optional aspects must be handled by the tests themselves.

The generated `libsycl-cts-targets.json` lists actual category executables and
object-library targets even when CTest omits commands for unbuilt executables.
The measurement script:

- Builds each category's objects first, then its executable.
- Never runs an old executable after a compilation or link failure.
- Executes successfully built categories with each supplied CTS device-name
  filter, retaining the CTS's own selection and assertions.
- Writes `baseline.json` and separate category-compilation, executable-build,
  and device logs. The executable build includes shared CTS utility compilation
  as well as host/device linking; its failure is not automatically a linker bug.
- Records the CTS revision, selected/configured target counts, and full,
  deprecated-feature, and interoperability test settings.
- Returns nonzero for measured failures and a separate infrastructure-error
  exit status. A report containing compilation failures is not a passing CTS run.

Use `--targets test_range test_nd_range`, for example, for a targeted
remeasurement; omitted categories are not counted as passing. Device filters
must match the machine's adapter names. The runtime timeout applies to individual
test executables, not compiler builds.

### Initial measured baseline

The November 22, 2026 inventory used CTS revision
`b74823b4179358851d7949d816e41e6f1a7e105a`, full conformance and deprecated
tests enabled, and the configuration above:

| Measurement | Result |
|---|---|
| Configured and attempted category executables | 97 |
| Category object builds that succeeded | 0 |
| Category object builds that failed | 97 |
| CTS executables run on any GPU | 0 |

These are category-build counts, not individual test-case results or a
conformance percentage. Missing public libsycl APIs prevent CTS headers from
compiling before DXIL generation. Observed prerequisites include `sycl::access`,
`memory_order`, `target`, `access_mode`, `kernel`, `half`, and function objects.
Missing `half` also prevents the CTS's own floating-point traits from forming;
those traits are not additional public SYCL APIs.

The first inventory also exposed an adapter identity-registration omission in
five categories. The registration patch now recognizes LibSYCL in the CTS's
implementation-selection helper without claiming another implementation or
disabling any tests. All five affected categories were remeasured: the identity
error disappeared, but all five still failed on missing library APIs. The
original inventory and targeted follow-up are separate reports; neither is a
passing CTS run.

The next conformance prerequisite is implementing the shared public API/header
requirements, with their real semantics, then remeasuring simple header and
geometry categories. Empty compatibility stubs would not establish support.
Workgroup/local-memory specialization remains a separate compiler/runtime
milestone described below.

### API foundation follow-up

The first implementation increment adds the access/address-space/decorated
enumerations and legacy aliases, memory-order enumerations and aliases, and
all nine scalar function objects with typed and deduced operand forms.
These declarations do not enable accessors, atomics, or new device aspects.
Vector and `marray` function-object support still requires those types.

Focused tests validate host-side promotion and return types, logical
short-circuiting, and first-argument preservation for equivalent minimum/maximum
operands. Compiler-generated integer and floating-point kernels validate all
nine operators, including signed-zero selection, on NVIDIA, AMD, and Intel.
As in the existing examples, submission helpers are compiled for both sides
while host-only I/O and verification are excluded from device compilation.
This does not establish support for unrestricted host library code in device
translation units.

The header, range, NDRange, and function-object CTS categories were remeasured
without changing their assertions or configuration. All four still fail to
compile and execute no CTS tests, but the missing enumeration errors are gone.
The newly visible shared prerequisites include `kernel`, `half`, `vec`,
`marray`, vector aliases, and deprecated compiler/linker availability queries.
Linking a kernel-free executable with the DirectX `-fsycl` pipeline is also
currently rejected; host-only function-object tests do not use device linking.
These are remaining implementation tasks, not passing or skipped CTS results.

### Adapter validation

The adapter has a separate smoke test using the existing two-kernel integration
probe. This validates device compilation, COFF linking, signing, registration,
and DLL staging independently of missing CTS library APIs. It is **not a CTS
conformance test**:

```bat
python -m unittest discover -s "%LLVM_SOURCE%\libsycl\test\cts" -p test_measure.py

cmake -S "%LLVM_SOURCE%\libsycl\test\cts\Inputs\smoke" ^
  -B "%CTS_BUILD%-adapter-smoke" -G Ninja ^
  -DCMAKE_BUILD_TYPE=Release ^
  "-DCMAKE_CXX_COMPILER=%COMPILER_BUILD%\bin\clang-cl.exe" ^
  "-DOFFLOAD_RUNTIME_BUILD=%RUNTIME_BUILD%" ^
  "-DSYCL_INCLUDE_DIR=%LLVM_LIB_BUILD%\include" ^
  "-DSYCL_RUNTIME_DLL=%LLVM_LIB_BUILD%\bin\LLVMSYCL.dll" ^
  "-DDXIL_VALIDATOR=%DXV%"
cmake --build "%CTS_BUILD%-adapter-smoke"
ctest --test-dir "%CTS_BUILD%-adapter-smoke" --output-on-failure
```

The smoke executable takes the same positional device index as the
multi-kernel probe. Run it on each hardware adapter to validate the transport,
without reporting those results as CTS passes.

## Workgroup and local-memory specialization requirements

This section describes the proposed next compiler/runtime contract, **not an
implemented package ABI or supported feature**.

### Preserve fixed AOT execution

An explicitly fixed workgroup kernel must continue to use its signed AOT DXIL
without compilation at execution time. A conflicting host local size must remain
an error; the runtime must not silently reshape the group. Existing packages
and their three-file deployment remain supported.

For kernels without a fixed-size attribute, general SYCL local-size selection
cannot be implemented merely by changing root constants. DXIL's `numthreads`
and static shared-storage declarations are compiled into the image. A finite
set of demo-specific variants would leave legal submissions unsupported.

The proposed general path uses a specialization-capable kernel representation
and a runtime compilation/signing service, with reusable AOT variants as an
optimization. A different approach, such as virtualizing logical workgroups
inside a fixed physical shape, would first need proven barrier and memory
semantics; simply masking extra threads is not sufficient.
The proposed service must not require an installed compiler, `dxv.exe`,
or a developer prompt on the execution machine. Its library dependencies and
packaging must be established before promising the same DLL count as fixed AOT
execution. Shared/host USM or `online_compiler` aspects must not be advertised
merely because internal specialization exists.

### Logical kernels and local allocations

The compiler must distinguish the logical kernel identity from concrete
executable variants. Versioned package metadata needs to describe fixed versus
specializable workgroup dimensions, the specialization input, and the supported
target requirements. Non-executable templates must never be passed to the
current executable-only program loader.

Local accessors and future OpenCL local-memory arguments need stable
compiler-assigned allocation slots. A slot carries requested bytes and required
alignment; the compiler determines its device-local layout. Host accessor
implementation pointers must not be mistaken for device allocation tokens or
UAV bindings. All slots must be disjoint, aligned, scoped to one workgroup, and
included in the resulting executable's reflected static-memory total.

The existing dispatch ABI still transports group counts and 64-bit offsets.
Those values are launch data, not specialization inputs.

### Variant selection and caching

A specialization key must include:

- The logical kernel/template digest and compiler/package ABI version.
- Shader target, relevant device capabilities, and code-generation options.
- The exact native x/y/z local shape.
- The ordered local-allocation sizes and alignments.
- Applicable specialization-constant values.

It must not include ordinary buffer allocation tokens, scalar launch values,
group counts, or global offsets. Repeated submissions with those values changed
should reuse the same executable variant. Binary artifact caching and
context/device-owned native pipeline lifetimes must remain separate.

Concurrent requests for one key must not compile duplicate variants or expose
partially initialized programs. Compiler, validator, cache, and native pipeline
failures must be reported explicitly. An unavailable variant is not permission
to fall back to a one-thread kernel.

### Validation gates

Before specialization becomes a supported feature, validate:

- Attribute-free 1D/2D/3D submissions and correct SYCL/native dimension ordering.
- Multiple local shapes for one logical kernel, without changing source.
- Multiple local allocations with differing sizes, alignment, and guard checks.
- Group barriers and memory visibility in tiled cooperative computations.
- Device limits, overflow, incompatible fixed sizes, and malformed metadata.
- Variant reuse across changing ranges/offsets and concurrent submissions.
- Signing failures, cache invalidation, context teardown, and standalone
  deployment on NVIDIA, AMD, and Intel.

The compiler/backend and packaging changes must share this contract with
libsycl. OpenCL can reuse the geometry and allocation specialization machinery;
its frontend and host API remain separate work.

## Roadmap beyond specialization

The destination is applicable SYCL 2020 conformance, not arbitrary device C++ or
every optional vendor extension. Maintain the CTS failure inventory throughout
these phases:

| Phase | Required work |
|---|---|
| CTS baseline | Compiler adapter, unchanged test builds, categorized failures, and reproducible reports. |
| Device memory and language | Aggregate captures, resource-backed pointers stored in device data, private arrays/objects, narrow and 64-bit accesses, vectors, supported math, permitted constants, and non-inlined/cross-TU device functions. |
| Workgroup computation | Local-size specialization, local accessors, barriers/fences, supported atomic operations/orders/scopes, subgroups, group algorithms, and submission reductions. |
| Ordinary SYCL applications | Buffers, host/device/placeholder accessors, dependency graphs, host tasks, contexts, selectors, properties, information queries, events, asynchronous errors, and USM API completion. |
| Program/library completeness | Kernel objects/IDs/queries, executable bundles, applicable input/object/build/link operations, specialization constants, pointer wrappers, traits, stream output, and required legacy interfaces. |
| Conformance and production | Full applicable CTS, correctly advertised optional aspects, multi-vendor/deployment regression tests, and allocation/transfer/descriptor/pipeline optimizations. |

Optional images, FP16/FP64, 64-bit atomics, profiling, online compilation/linking,
and additional USM capabilities must be tested when advertised. Their absence
must not hide missing mandatory APIs or be mislabeled as a passing optional test.
