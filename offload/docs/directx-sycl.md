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
The backend is exposed as `sycl::backend::ext_llvm_directx`. Device USM and
restricted, eagerly coherent shared USM are supported. Host and atomic-shared
USM aspects remain unavailable. Shared allocations use
[paired host/GPU storage](directx-manifest.md#sharedmanaged-allocations);
operations involving them currently complete synchronously. Device-only
dispatch and transfers remain asynchronous.

This is **restricted parallel SYCL support**, not full SYCL or OpenCL support.
Vector addition uses `nd_range<1>` with 64 work-items per workgroup and one
element per work-item. Work-item IDs, local/global ranges, group IDs/ranges,
linear IDs, and 64-bit global offsets use a reusable target/runtime
[NDRange ABI](directx-manifest.md#ndrange-dispatch-abi), including 2D/3D
dimension ordering. Subgroup builtins remain explicitly unsupported.
The captured kernel accepts buffer pointers, 32/64-bit integers, and 32-bit
floats, including loaded fields of nested records, nonvirtual bases, and
fixed-size arrays, subject to the
[capture ABI restrictions](directx-manifest.md#restricted-capture-record-abi).
The GNU `reqd_work_group_size(x,y,z)` attribute on a kernel lambda or functor's
call operator fixes its native workgroup shape. Host local sizes must match.
Without the attribute, the compiler packages signed one-thread AOT DXIL and an
optional prepared-IR recipe. Other uniform local shapes are now regenerated,
signed, validated, and cached by the runtime through
[compiler-backed workgroup variants](directx-manifest.md#compiler-backed-sycl-workgroup-variants).
First use of a new shape requires compiler/validator tools and blocks enqueue;
the default shape and cached variants need no tool invocation. Local-accessor
allocation and general local-memory specialization remain future work.
Implicit `parallel_for(range)` kernels retain their logical extents in the
capture record and cover them with overflow-safe GPU strided loops. Oversized
ranges select a 64-thread variant along their largest axis and cap native
dispatch counts at 65,535 groups per axis. Small ranges keep the default AOT
shape. This does not widen explicit `nd_range` group counts or change its
workgroup/barrier geometry; large implicit ranges require an eligible
specializable kernel and the tools needed for its first shape variant.

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
tools are needed on the destination machine for this fixed-shape example.
Applications requesting new attribute-free workgroup shapes currently also
need this branch's compiler tools and DXIL validator at execution time.

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
- Records Catch2 test-case/assertion totals for each executed vendor run and
  aggregates compiled, executable, fully passing, and unmeasured categories.
  Missing or incomplete Catch2 totals are explicitly `null`; aggregate counts
  identify runs without totals and are not claimed complete.
- Returns nonzero for measured failures and a separate infrastructure-error
  exit status. A report containing compilation failures is not a passing CTS run.

Use `--targets test_range test_nd_range`, for example, for a targeted
remeasurement; omitted categories are not counted as passing. Device filters
must match the machine's adapter names. The runtime timeout applies to individual
test executables, not compiler builds.

Category pass counts alone hide progress inside a still-failing category.
For example, uniform workgroup support increased Range's passing assertions
from 126 to 8,946 (8,820 additional checks), without changing its 4/5 case
result. Shared allocation then exposed the large-range limit before another
assertion could complete. Report those changes alongside the category count.
At the shared-USM checkpoint, three confirmed passing categories out of 97
configured executables was a lower-bound inventory, not a freshly measured
full-suite conformance rate.
Remeasure additional affected categories rather than assuming the other 93
remain unchanged or counting them as passing.

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
Complete vector and `marray` function-object support remains a separate step
from the scalar operator validation.

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
At that measurement, linking a kernel-free executable with the DirectX
`-fsycl` pipeline was also rejected; host-only function-object tests do not
use device linking. These were implementation gaps, not skipped CTS results.

### Numeric value-type checkpoint

The next prerequisite increment supplies binary16 `half` storage/conversion
and numeric limits, `bit_cast`, and core vector/marray value operations and
aliases. Binary16 conversion rounds directly from double inputs to avoid
double-rounding, preserves signed zero, and handles subnormals, infinities,
and NaNs. Vector comparisons produce signed all-bits-set masks, whereas marray
comparisons produce boolean elements; three-element vectors use four-element
storage alignment.

Focused host tests cover binary16 round trips, midpoint/overflow/subnormal
boundaries, composed constructors, arithmetic, masks, and bit reinterpretation.
The runtime rebuild and existing scalar GPU kernels also pass on all three
vendors. These checks do not establish device-side half/vector storage,
complete vector/marray APIs, or a new FP16 aspect.

At the numeric checkpoint, the unchanged CTS error category failed to compile,
with the numeric header errors removed. Its first remaining diagnostics were
the missing kernel
type and the deprecated compiler/linker availability queries. No CTS tests
had executed at that checkpoint; executable kernel/bundle support and
host-only DirectX compiler/linking fixes remained prerequisites.

### First unchanged CTS passes

The next increment supplies registered kernel identities, runtime-backed
executable bundles and kernel objects, platform selection, and legacy
compiler/linker availability queries. Availability reflects existing aspects;
this does not introduce an online compiler or linker. Input/object bundles
explicitly report unsupported functionality. Executable bundles validate context
devices and image compatibility and prepare kernels through existing native
program caches.

This is the initial executable-bundle API surface, not complete kernel/program
or device-information conformance.

DirectX device compilation now keeps host pointer widths in intermediate SYCL
IR, then restores native DXIL layout during capture ABI lowering. Destructor
cleanup retains its convergence token, and resource GEP indices are normalized
to DXIL's address width. Kernel-free links produce a valid empty offload package,
not a placeholder shader; runtime registration accepts that package.

With the same upstream CTS revision and configuration, four unchanged
categories were remeasured:

| Category | Build | Result with each NVIDIA/AMD/Intel device filter |
|---|---|---|
| `test_error` | Passed | 1 test case passed |
| `test_header` | Passed | 2 test cases and 2 assertions passed |
| `test_range` | Failed | No execution; buffers/accessors are missing |
| `test_nd_range` | Failed | No execution; default construction and buffers/accessors are missing |

These first two categories check host/header APIs, **not GPU kernel
conformance**. The error test performs a class-type check but has no Catch2
assertion counter; it is not a zero-test executable. No CTS assertions or test
sources were weakened. The other 93 configured categories were not remeasured,
and these results do not establish a conformance percentage.

Separate compiler-generated GPU tests validate executable-bundle lookup,
rejected bundle requests, captured scalar/buffer arguments, and integer and
floating-point kernels on all three vendors. The standalone vector-add example
also verifies 65,537 elements on each vendor after the pointer-layout changes.
Native device-version queries
report discovered shader models: 6.8 on the tested NVIDIA/AMD devices and 6.7
on Intel. No additional FP16, FP64, subgroup, or online capabilities are
advertised.

Reproduce the targeted measurement after rebuilding the compiler and runtime:

```bat
python "%LLVM_SOURCE%\libsycl\test\cts\measure.py" ^
  --build-dir "%CTS_BUILD%" --report-dir "%CTS_BUILD%\first-passes" ^
  --targets test_error test_header test_range test_nd_range ^
  --devices NVIDIA AMD Intel --jobs 2
```

The command returned nonzero because the two geometry categories still failed.
Their next major prerequisite at that checkpoint was buffer/accessor storage, scheduling,
capture lowering, and lifetime/writeback semantics; compatibility declarations
alone would not satisfy their device-execution tests.

### Initial buffer/accessor execution

One-dimensional, host-pointer-backed buffers now have real native storage and
device accessors with `read`, `write`, and `read_write` modes. Command groups
retain their requirements and use buffer events for dependencies across queues.
The last buffer reference waits and writes results back even if accessor copies
still retain the underlying allocation. Transfers between contexts or devices
synchronize and migrate through owned host storage.

The initial scheduler serializes host command-group construction/submission
with a runtime-wide lock; GPU dispatch remains asynchronous. Migration and
buffer destruction may block. Destroying the last buffer inside a command group
before deferred submission is explicitly unsupported and reports an asynchronous
error without retaining the caller's expired host pointer.

Accessor capture records are flattened recursively, including nontrivial host
callables. Unused host ownership fields are not exposed as device arguments.
Accessor pointers use UAV views, including read-mode accessors, so aliases do
not require incompatible simultaneous SRV/UAV resource states. Read-mode C++
references remain const. Ordinary USM capture inference is unchanged.
The module splitter also excludes DirectX kernels from possible indirect-call
targets, as it already did for kernels with dedicated calling conventions.

GPU tests on NVIDIA, AMD, and Intel verify initial upload, read-only input
preservation, same-buffer accessor aliases, cross-queue ordering, concurrent
host submissions, context migration, and last-buffer writeback while an
accessor is retained. NDRange default construction and subsequent assignment
are tested in all three dimensions.

Rebuild the compiler and runtime before running the buffer test from a
Visual Studio developer prompt with the variables defined above:

```bat
"%COMPILER_BUILD%\bin\clang-cl.exe" /nologo /std:c++17 /MD /EHsc /clang:-fsycl ^
  /clang:--offload-targets=dxil-pc-shadermodel6.0-compute ^
  /I "%LLVM_LIB_BUILD%\include" ^
  /clang:-Xoffload-linker "/clang:--dxil-validator=%DXV%" ^
  "%LLVM_SOURCE%\libsycl\test\basic\buffer.cpp" ^
  "%RUNTIME_BUILD%\lib\LLVMSYCL.lib" ^
  /Fe:"%CTS_BUILD%\buffer-foundation.exe" /Fo:"%CTS_BUILD%\buffer-foundation.obj"
set "PATH=%LLVM_LIB_BUILD%\bin;%RUNTIME_BUILD%\lib;%PATH%"
for %D in (0 1 2) do "%CTS_BUILD%\buffer-foundation.exe" %D
```

The indices above are the three tested hardware adapters; use the existing
demo's `--list` output to check the mapping on another machine.

At this checkpoint, the unchanged range and NDRange CTS categories compiled
their objects but failed device linking on byte-sized `bool` accesses:
DirectX buffer lowering accepted only 32-bit integers and floats. Neither category had run,
and neither is a CTS pass. The previously passing header/error categories are
retained separately.

The final measurement repeats all four unchanged categories:

```bat
python "%LLVM_SOURCE%\libsycl\test\cts\measure.py" ^
  --build-dir "%CTS_BUILD%" --report-dir "%CTS_BUILD%\buffer-verified" ^
  --targets test_error test_header test_range test_nd_range ^
  --devices NVIDIA AMD Intel --jobs 2
```

This command returns nonzero for the two device-link failures and records zero
executions for them; it does not run stale executables.

Remaining steps at that checkpoint included real subword storage semantics (without substituting
32-bit boolean elements), wide captured scalars used by accessor size queries,
multidimensional/subrange accessors, host/placeholder accessors, buffer
properties and complete reference/API conformance. Only nonempty host-pointer
buffers with the standard allocator are implemented in this increment.
This does not advertise shared USM, atomics, FP16/FP64, or general workgroup
specialization.

### Byte-sized buffer execution

Scalar `bool`, signed-byte, and unsigned-byte buffer loads/stores now lower
without changing element layout. Byte loads extract from aligned words.
Byte stores replace only the addressed byte with an atomic compare-exchange
retry loop, preserving neighboring bytes even when adjacent work-items write
concurrently. These internal operations do not advertise public SYCL atomic
support.

DirectX buffer allocations round up only their native backing size to a
multiple of four. Upload and writeback still transfer exactly the logical byte
count. The buffer test above now checks mixed boolean values, signed/unsigned
byte arithmetic, one-byte and five-byte buffers, untouched neighboring bytes,
host sentinels, and 16 repeated parallel updates over 1,025 byte elements on
NVIDIA, AMD, and Intel. Byte vectors and 16-bit buffer accesses remain outside
this increment.

The same four unchanged CTS categories were remeasured. Header/error still
pass; range/NDRange now get beyond byte-access validation but fail device
linking when the ABI restores the native 32-bit pointer layout. Split modules
retain unrelated host-library functions and global constructors; host-width
`ptrtoaddr` results then violate the device layout. Neither geometry category
executes, and no new CTS pass is claimed.

Reproduce this measurement with the command above, using a fresh
`--report-dir "%CTS_BUILD%\byte-verified"`. The next compiler task is to isolate
the actual device program from host-only initialization/dependencies without
discarding initialization that a kernel genuinely requires.

### Kernel dependency isolation and first geometry execution

DirectX split images now start from their kernel's dependencies instead of
rooting every non-discardable global imported by single-source compilation.
Constructors/destructors that can affect reachable globals, associated data,
and transitive initialization dependencies remain in the image. Opaque effects
are retained conservatively when mutable globals are reachable. Required
dynamic device initialization/destruction is explicitly rejected; it is not
discarded to make a shader compile. Other targets retain their existing global
root policy.

Each split image is then prepared for the restricted ABI using standard
inlining, scalar replacement, instruction/CFG simplification, and dead-code
cleanup. The recognized accessor-pointer annotation is removed before this
step so private accessor copies can be scalarized. Explicit `noinline` and
unsupported-device-call attributes are not overridden. This handles ordinary
inlineable device helpers, not general out-of-line device calls.

The unchanged range category now builds, signs, links, and executes on NVIDIA,
AMD, and Intel: **4 of 5 test cases and 126 of 127 assertions pass** on each.
The remaining `range_api` case requests a local shape that does not match the
compiled `numthreads`, and fails with the real group-size diagnostic. It is
not a passing category and does not use a forced one-thread fallback.

At this checkpoint, header/error passed on all three filters. NDRange still
failed device linking on aggregate captures containing arrays of 64-bit range
components and had no executions. The fully passing category count was two,
although genuine geometry test cases could execute.

Reproduce the four-category measurement with the existing command, using
`--report-dir "%CTS_BUILD%\isolation-verified"`. The next prerequisites are
array/64-bit capture transport and correct workgroup specialization, without
weakening the CTS or changing only launch metadata. Native byte buffers,
executable bundles, and vector-add remain regression-tested on all three
hardware vendors.

### Scalar array captures and first full GPU CTS category

Clang now records scalar leaves of fixed-size arrays and nonvirtual bases,
including the base-class arrays used by SYCL ranges. Capture lowering validates
those offsets against the LLVM record and transports used 64-bit integers
without truncation. Eight-byte root constants have eight-byte alignment;
explicit `dx.Layout` size/offsets preserve gaps and the 252-byte root budget.
The existing runtime already copies eight-byte scalar values without
conversion. Byte-PHI legalization now also handles boolean merges and loop
backedges using legal integer carriers, without widening buffer element
storage.

Optimized-away captures remain valid: a nonempty capture record can have zero
used bindings. It still supplies one correctly sized host argument. This
allows genuinely generated constructor kernels whose checks optimize away to
remain executable; it does not substitute a dummy shader or alter CTS checks.

The unchanged **NDRange category passes all six cases and 558 assertions** on
NVIDIA, AMD, and Intel, including real GPU execution of captured range
comparisons and member checks. Header/error continue to pass. This brings
confirmed fully passing categories to **3 of 97 configured categories
(about 3.1%)**; the other 93 categories were not remeasured in this increment.
The selected four-category measurement passes three categories, not 75% of
the full CTS. Range still executes with 4/5 cases and 126/127 assertions passing
per vendor; its remaining failure requires genuine workgroup specialization.

Rebuild device objects after changing the compiler's capture metadata, and
refresh the CTS executable directory's runtime DLL before measuring:

```bat
cmake -E copy_if_different "%RUNTIME_BUILD%\lib\LLVMOffload.dll" "%CTS_BUILD%\bin"
python "%LLVM_SOURCE%\libsycl\test\cts\measure.py" ^
  --build-dir "%CTS_BUILD%" ^
  --report-dir "%CTS_BUILD%\array-capture-verified" ^
  --targets test_error test_header test_range test_nd_range ^
  --devices NVIDIA AMD Intel --jobs 2
```

Native regressions also check distinct upper/lower 64-bit words, negative
signed values, mixed scalar alignment, nested arrays, unused captures, and
unchanged byte-buffer layout on all three vendors. Dynamic array indexing,
general device aggregates, other scalar types, and runtime local-size
specialization remain outside this checkpoint.

### Compiler-backed uniform workgroup shapes

Attribute-free SYCL kernels now execute real regenerated DXIL for requested
uniform local shapes. The native test at `libsycl/test/basic/workgroup.cpp`
checks one-thread AOT execution without compiler tools, non-power-of-two
1D shapes 3 and 5, 2D `{2,3}`, and 3D `{2,3,2}`, including global/local/group
linear IDs and local ranges on NVIDIA, AMD, and Intel. Changed global ranges
reuse a cached shape with an intentionally invalid compiler directory.
An uncached shape with that directory fails explicitly; it does not use a
dummy shader or one-thread fallback. Fixed-shape kernels retain AOT execution
and reject shape overrides.

Use the Windows build variables above:

```bat
set "OFFLOAD_DIRECTX_COMPILER_PATH=%COMPILER_BUILD%\bin"
set "OFFLOAD_DIRECTX_DXIL_VALIDATOR=%DXV%"
"%COMPILER_BUILD%\bin\clang-cl.exe" /nologo /std:c++17 /MD /EHsc /W4 /WX ^
  /clang:-fsycl /clang:--offload-targets=dxil-pc-shadermodel6.0-compute ^
  "/I%LLVM_LIB_BUILD%\include" ^
  /clang:-Xoffload-linker "/clang:--dxil-validator=%DXV%" ^
  "%LLVM_SOURCE%\libsycl\test\basic\workgroup.cpp" ^
  "%RUNTIME_BUILD%\lib\LLVMSYCL.lib" ^
  /Fe:"%CTS_BUILD%\workgroup-specialization.exe" ^
  /Fo:"%CTS_BUILD%\workgroup-specialization.obj"
set "PATH=%RUNTIME_BUILD%\lib;%LLVM_LIB_BUILD%\bin;%PATH%"
for %D in (0 1 2) do "%CTS_BUILD%\workgroup-specialization.exe" %D
```

Rebuild the CTS range/NDRange device objects after changing compiler-produced
recipe metadata and refresh the runtime DLL. With the two tool variables set:

```bat
cmake -E copy_if_different "%RUNTIME_BUILD%\lib\LLVMOffload.dll" "%CTS_BUILD%\bin"
python "%LLVM_SOURCE%\libsycl\test\cts\measure.py" ^
  --build-dir "%CTS_BUILD%" ^
  --report-dir "%CTS_BUILD%\workgroup-verified" ^
  --targets test_error test_header test_range test_nd_range ^
  --devices NVIDIA AMD Intel --jobs 2
```

The unchanged Range CTS now passes **8,946 of 8,947 assertions** on each
hardware vendor, up from 126 of 127. It still has **4 of 5 passing cases**:
after the workgroup checks succeed, `range_api` requests shared USM and fails
with the actual unsupported-allocation diagnostic. Shared USM is not
advertised or substituted with host-executed work. Range is therefore still
not a passing category.

Header/error and all six NDRange cases (558 assertions) continue to pass.
Confirmed fully passing categories remain **3 of 97 (about 3.1%)**; only four
categories were remeasured, not the other 93. This increment removes the
workgroup-size blocker, not the next shared-USM prerequisite or the remaining
local-accessor/barrier work.

### Eagerly coherent shared USM

`malloc_shared` and `aligned_alloc_shared` now return real CPU-accessible
allocation pointers backed by context-owned GPU resources. Bound shared
allocations are uploaded before real GPU execution and read back before enqueue
returns. Shared copies/fills use the same explicit coherence policy, including
device/shared staging transfers. This conservative implementation serializes
managed operations per device; it does not claim page-fault migration,
concurrent CPU/GPU access, atomic shared-USM support, or nonblocking shared
enqueue.

The native test at `libsycl/test/basic/shared-usm.cpp` checks aligned allocations,
CPU initialization, two dependent GPU updates, subsequent CPU edits, another
queue's GPU update, host/shared/device copy directions, interior fills, and
prefix/suffix guards on NVIDIA, AMD, and Intel. Raw shader bindings retain
their existing 16-byte offset alignment; smaller offsets are explicitly
rejected rather than silently rounded. Runtime tests also validate shared
allocation kind, requested size, interior-pointer metadata, alignment,
context ownership, and lifetime.

```bat
"%COMPILER_BUILD%\bin\clang-cl.exe" /nologo /std:c++17 /MD /EHsc /W4 /WX ^
  /clang:-fsycl /clang:--offload-targets=dxil-pc-shadermodel6.0-compute ^
  "/I%LLVM_LIB_BUILD%\include" ^
  /clang:-Xoffload-linker "/clang:--dxil-validator=%DXV%" ^
  "%LLVM_SOURCE%\libsycl\test\basic\shared-usm.cpp" ^
  "%RUNTIME_BUILD%\lib\LLVMSYCL.lib" ^
  /Fe:"%CTS_BUILD%\shared-usm.exe" /Fo:"%CTS_BUILD%\shared-usm.obj"
set "PATH=%RUNTIME_BUILD%\lib;%LLVM_LIB_BUILD%\bin;%PATH%"
for %D in (0 1 2) do "%CTS_BUILD%\shared-usm.exe" %D
```

Refresh both runtime DLLs before remeasuring CTS. The unchanged Range category
now gets past shared allocation but still passes **4/5 cases and
8,946/8,947 assertions** per vendor: its first full-conformance large-range
submission (`UINT32_MAX` work-items) exceeds the native per-axis dispatch limit.
Later requested ranges include more than 32-bit work-items and approximately
8.7 trillion work-items. They are not truncated, skipped, or replaced with a
single dummy dispatch. Large logical-range execution requires a separate
compiler/runtime geometry contract; it is not implemented by shared migration.

```bat
cmake -E copy_if_different "%RUNTIME_BUILD%\lib\LLVMOffload.dll" "%CTS_BUILD%\bin"
cmake -E copy_if_different "%LLVM_LIB_BUILD%\bin\LLVMSYCL.dll" "%CTS_BUILD%\bin"
set "OFFLOAD_DIRECTX_COMPILER_PATH=%COMPILER_BUILD%\bin"
set "OFFLOAD_DIRECTX_DXIL_VALIDATOR=%DXV%"
python "%LLVM_SOURCE%\libsycl\test\cts\measure.py" ^
  --build-dir "%CTS_BUILD%" --report-dir "%CTS_BUILD%\shared-usm-verified" ^
  --targets test_error test_header test_range test_nd_range ^
  --devices NVIDIA AMD Intel --jobs 2
```

Header/error and the six NDRange cases (558 assertions) remain passing.
Confirmed fully passing categories remain **3/97 (about 3.1%)**, not a new
full-suite pass rate; the other 93 categories were not remeasured. Large-range
execution, local accessors/barriers, unrestricted resource-backed pointers,
and complete USM/public-library APIs remain separate prerequisites.

### Large logical ranges and granular CTS progress

Implicit range kernels now capture the original logical extent alongside the
callable, subject to the existing capture ABI limits. Native lanes iterate
over logical IDs using the actual physical grid as their stride and construct
fresh `item` arguments with the original extent and offset. The loop checks
remaining distance before incrementing to avoid `size_t` overflow. It neither
truncates work nor substitutes a dummy kernel. Ordinary compiler optimizations
remain applicable to the user body.
Rebuild applications against the updated headers and runtime together: the
implicit range capture record now includes that logical extent.

Above 65,535 work-items on any axis, the runtime requests an existing
compiler-backed 64-thread workgroup variant along the largest axis, rounds
that physical axis up, and bounds native group counts. Logical bounds prevent
padding lanes from invoking the user body. This avoids the severe
single-lane workgroup underutilization that caused the first wide-ID test to
time out on Intel. Small-range signed AOT execution remains compiler-free.
Explicit `nd_range` submissions retain their geometry and existing count
limits; this is not a wide-group-ID or cross-dispatch barrier implementation.

The native test `libsycl/test/basic/large-range.cpp` verifies every output and
a suffix guard for a 1D extent larger than the entire bounded physical grid,
as well as 2D/3D extents beyond the per-axis limit. A sparse-output kernel
checks the high and low words of a logical ID, extent, and linear ID above
`UINT32_MAX`; it uses supported 32-bit buffer stores, not new 64-bit buffer
access support. Queue and handler submission paths are covered.

```bat
"%COMPILER_BUILD%\bin\clang-cl.exe" /nologo /std:c++17 /MD /EHsc /O2 /W4 /WX ^
  /clang:-fsycl /clang:--offload-targets=dxil-pc-shadermodel6.0-compute ^
  "/I%LLVM_LIB_BUILD%\include" ^
  /clang:-Xoffload-linker "/clang:--dxil-validator=%DXV%" ^
  "%LLVM_SOURCE%\libsycl\test\basic\large-range.cpp" ^
  "%RUNTIME_BUILD%\lib\LLVMSYCL.lib" ^
  /Fe:"%CTS_BUILD%\large-range.exe" /Fo:"%CTS_BUILD%\large-range.obj"
set "PATH=%RUNTIME_BUILD%\lib;%LLVM_LIB_BUILD%\bin;%PATH%"
set "OFFLOAD_DIRECTX_COMPILER_PATH=%COMPILER_BUILD%\bin"
set "OFFLOAD_DIRECTX_DXIL_VALIDATOR=%DXV%"
for %D in (0 1 2) do "%CTS_BUILD%\large-range.exe" %D
```

The unchanged full-conformance Range category now passes **5/5 cases and
8,949/8,949 assertions on NVIDIA, AMD, and Intel**, including its more-than-32-bit
and approximately 8.7-trillion-work-item submissions. Together with header,
error, and NDRange, the targeted measurement reports **14/14 cases and
9,509/9,509 assertions per vendor**. The confirmed category count advances
from **3/97 to 4/97**; this is still not a full-suite conformance rate.

An additional eight-category build measurement explains why earlier commits
did not unlock more categories:

| Categories | First outstanding compilation prerequisites |
|---|---|
| `test_id` | Buffer constructors, access tags, and `host_accessor` |
| `test_item` | Multidimensional buffers/accessors and `get_access` |
| `test_group`, `test_nd_item` | `multi_ptr` and workgroup-copy APIs |
| `test_usm` | `usm_allocator` and remaining public USM APIs |
| Three `test_get_kernel_ids_*` categories | Legacy image target names referenced by shared CTS utilities; declarations alone would not establish image support or category passes |

Those categories executed no tests; their failures must not be counted as
passing assertions. Another 85 configured categories remain unmeasured after
the twelve-category breadth check. The JSON report now distinguishes passing
categories, compilation failures, execution/infrastructure failures, and
known versus missing case/assertion totals. Earlier commits added 8,820 passing
Range assertions while leaving its all-or-nothing category status failed;
shared USM then uncovered the large-range blocker. Both granular and category
metrics are needed to show that progression honestly.

Refresh the paired DLLs as above and reproduce the current passing subset:

```bat
python "%LLVM_SOURCE%\libsycl\test\cts\measure.py" ^
  --build-dir "%CTS_BUILD%" --report-dir "%CTS_BUILD%\large-range-final" ^
  --targets test_error test_header test_range test_nd_range ^
  --devices NVIDIA AMD Intel --jobs 2
```

The existing buffer, executable-bundle, shared-USM, uniform-workgroup, and
65,537-element vector-add tests were rebuilt and passed on all three vendors.
The full queue unit suite and focused measurement-reporting tests also pass.

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

Uniform local-shape regeneration is implemented by the bounded prepared-IR
recipe above. This section describes the remaining general local-memory and
deployment contract, **not a claim that all specialization features work**.

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
The production service should remove the current requirement for installed
compiler/validator tools on first use of a new shape, without requiring a
developer prompt on the execution machine. Its library dependencies and
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

Before general local-memory specialization becomes a supported feature, validate:

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
