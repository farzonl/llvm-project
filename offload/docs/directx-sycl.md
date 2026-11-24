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
First use of a new shape requires compiler/validator tools and blocks enqueue.
Bounded local-accessor allocations also specialize the prepared IR, including
for fixed workgroup shapes. Allocation-bearing kernels always require a matching
variant; their minimally sized packaged image is never dispatched. Default AOT
images without local allocations and matching cached variants need no tool
invocation. General local-memory specialization remains incomplete.
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

### Owned buffers, host access, and 64-bit results

One-dimensional buffers can now allocate their own storage from a range,
without requiring a caller-owned host pointer. `read_only`, `write_only`, and
`read_write` tags select device and host accessor modes, including deduction
of the corresponding accessor type.

Host accessor construction waits for the buffer's latest submitted event and
reads back GPU writes. Writable host access marks the storage for upload before
subsequent GPU execution. Copies share one host-access lifetime; independently
acquired read accessors may coexist. Device submissions wait until all host
accessors for that buffer are released. This is a conservative blocking
submission policy, not a general nonblocking scheduler: release a host accessor
before submitting dependent work on the same thread.

A host accessor keeps owned storage alive after the last buffer object is
destroyed. For caller-owned storage, final writeback is deferred until that
remaining host access and pending command-group requirements end, so the caller's
memory must remain valid until then. Unlike the previous explicit rejection,
destroying the last buffer object inside an unsubmitted command group now defers
final writeback until submission completes. Buffer-owner and last-reference
cleanup do not acquire the global submission lock, allowing owners and host
leases to be released while another submission waits for them.
Acquiring the first host lease while its buffer has an unsubmitted command-group
requirement is explicitly rejected rather than exposing incomplete device work.

Resource-backed 64-bit integer loads/stores now transport two 32-bit words per
element without changing buffer layout. Compiler-generated vectors of these
integers use the same word transport. Constants are split with scalar
operations before DXIL emission, avoiding residual vector constant expressions
that caused the initial ID-result shader to write zeros on AMD.
This does not introduce FP64 or atomic buffer support.

Aggregate zero initialization uses byte-store lowering for resource-backed
`memset`. Copies of captured aggregates transport complete scalar fields,
loading all fields before storing to preserve `memmove` semantics.
Narrow integer captures retain their one-/two-byte host widths in the manifest
and are promoted into 32-bit root-constant slots without reading host padding.
Generated code restores the original width before signed or unsigned arithmetic.
The DirectX SYCL frontend also inherits the host's default `wchar_t` type,
preserving Windows' two-byte unsigned representation and capture field offsets.
Explicit compiler options overriding `wchar_t` remain honored.
After capture lowering, executable DXIL restores the native `wchar_size`
module flag alongside its pointer layout. The actual integer types and host
capture widths are unchanged. This prevents NVIDIA's 64-bit arithmetic shader
linking from conflicting with the intermediate Windows language ABI.
Shaders that retain 16-bit operations require a successful native
`MinimumPrecision` feature check; this does not change advertised SYCL half
capabilities.
Runtime-indexed captured scalar arrays are initialized in private kernel
storage from their actual capture fields. The same lowering supports nested
scalar arrays; the required fields still count against the 252-byte root limit.
Writes and pointer escapes through these captured arrays remain rejected.

The extended `libsycl/test/basic/buffer.cpp` checks GPU-to-host visibility,
host edits followed by another queue's GPU update, copied and independent
read-access lifetimes, blocked submissions, last-owner storage/writeback,
distinct high/low words and negative integers, constant/captured/zero
three-component ID results, runtime ID and nested-array indexing, and
signed/unsigned byte, short, boolean, and `wchar_t` captures with neighboring
sentinel bytes.
Existing byte-buffer, dependency, migration, and sentinel checks remain
included. Buffers/accessors remain one-dimensional with the standard allocator;
over-aligned elements, subranges, placeholders, and full buffer/accessor API
conformance remain outside this increment.

The unchanged ID CTS now passes **89/89 cases and 2,960/2,960 assertions**
on each of NVIDIA, AMD, and Intel. Error, header, range, and NDRange remain
fully passing. The combined measurement reports **103/103 cases and
12,469/12,469 assertions per vendor**, advancing the confirmed fully passing
category count from **4/97 to 5/97**. The other 92 configured categories were
not measured by this run; this is not a full-suite conformance rate.

The final measurement uses freshly relinked DXIL after restoring native shader
ABI metadata. The initial Windows-width correction passed AMD and Intel but
exposed NVIDIA's shader-linking conflict; that failed run is not counted as
an all-vendor pass. Rebuild device objects after frontend capture-layout
changes, and relink executables after backend ABI changes.

```bat
set "OFFLOAD_DIRECTX_COMPILER_PATH=%COMPILER_BUILD%\bin"
set "OFFLOAD_DIRECTX_DXIL_VALIDATOR=%DXV%"
copy /y "%LLVM_LIB_BUILD%\bin\LLVMSYCL.dll" "%CTS_BUILD%\bin\LLVMSYCL.dll"
copy /y "%RUNTIME_BUILD%\lib\LLVMOffload.dll" "%CTS_BUILD%\bin\LLVMOffload.dll"
python "%LLVM_SOURCE%\libsycl\test\cts\measure.py" ^
  --build-dir "%CTS_BUILD%" ^
  --report-dir "%CTS_BUILD%\host-access-id-native-final" ^
  --targets test_id test_error test_header test_range test_nd_range ^
  --devices NVIDIA AMD Intel --jobs 2 --timeout 900
```

The extended native buffer test also passes on all three vendors, including
64-bit division with a captured Windows `wchar_t`. Thirteen focused compiler
tests, the 28-test queue unit suite, and 35 focused DirectX
argument/manifest/workgroup/feature tests pass.

### Multidimensional buffer/accessor execution

Buffers and host/device accessors now support one, two, or three dimensions.
They retain the full range and use the shared row-major linearization helper
for `id` indexing. Chained subscripts (`accessor[i][j][k]`) and `item` indexing
address the same storage, preserving read-only reference types. Device capture
records retain the actual range fields; existing one-dimensional accessor
layout is unchanged.

Buffer construction checks each extent before multiplication, rejecting empty
dimensions and element-count/byte-size overflow before allocation. Host leases,
cross-queue dependencies, uploads, readback, and deferred writeback use the same
runtime paths as one-dimensional buffers. Standard allocators and mutable,
trivially copyable elements remain required. The aligned-buffer extension below
removes the initial normal-alignment restriction. Subranges,
placeholders, local accessors, and full buffer/accessor conformance remain
outside this increment.

The native buffer test adds non-square 3x5 and 2x3x5 cases, covering host
initialization, device `id` reads and chained writes, const host references,
row-major readback, caller-owned writeback, and invalid extent rejection on
NVIDIA, AMD, and Intel. Unchanged `test_item` also passes **6/6 cases and
93/93 assertions per vendor**, including item API, construction, equality,
and static members.

After rebuilding the previous passing categories against the updated header,
the six-category measurement passes **109/109 cases and 12,562/12,562 assertions
per vendor**. The confirmed fully passing category count advances from
**5/97 to 6/97**. The other 91 configured categories were not measured by this
run; this is not a full-suite conformance rate. All seven measurement-reporting
unit tests also pass.

Reproduce the combined measurement after rebuilding device objects affected
by the public-header change:

```bat
python "%LLVM_SOURCE%\libsycl\test\cts\measure.py" ^
  --build-dir "%CTS_BUILD%" ^
  --report-dir "%CTS_BUILD%\multidimensional-item-final" ^
  --targets test_item test_id test_error test_header test_range test_nd_range ^
  --devices NVIDIA AMD Intel --jobs 2 --timeout 900
```

### Typed pointer and workgroup-barrier foundation

The modern non-void `multi_ptr` interface now supports explicitly decorated or
undecorated global, local, private, and generic pointers. Device compilation
uses actual Clang SYCL address-space types, not interchangeable host pointers.
Supported operations include dereferencing, indexing, arithmetic, same-type
comparisons, const-preserving conversions, and named-space-to-generic
conversions. Device accessor construction and
`get_multi_ptr<access::decorated::yes/no>()` preserve read-only element
constness. Host representations remain ordinary pointers.

The DirectX kernel ABI validates and normalizes default/global/generic buffer
casts while retaining the correct resource access mode. This is not arbitrary
device pointer transport: surviving resource-pointer comparisons, escapes,
and casts to local memory are rejected. The legacy, void, and constant-space
interfaces, checked address-space casts, prefetch, and complete pointer
conformance remain unimplemented.

DirectX `group_barrier` now synchronizes workgroup execution and fences both
groupshared and global resource memory. The existing shared-memory-only DirectX
builtin retains its original semantics; a separate all-memory builtin supplies
the SYCL fence. Supported fence scopes must resolve during compilation to
`work_item` or `work_group`, including the default workgroup scope. Other
fence scopes and subgroup execution are explicitly rejected. The SPIR-V path
is unchanged.

The native pointer test checks decorated/raw/generic resource access, private
arrays, and cross-lane groupshared and global scratch reads after a barrier,
using two independent 32-thread workgroups. It passes on NVIDIA, AMD, and Intel,
as does the rebuilt buffer regression. This uses static groupshared storage;
it does not implement dynamic local-accessor allocation or asynchronous
workgroup copies.

```bat
"%COMPILER_BUILD%\bin\clang-cl.exe" /nologo /std:c++17 /MD /EHsc /O2 /W4 /WX ^
  /clang:-fsycl /clang:--offload-targets=dxil-pc-shadermodel6.0-compute ^
  "/I%LLVM_LIB_BUILD%\include" ^
  /clang:-Xoffload-linker "/clang:--dxil-validator=%DXV%" ^
  "%LLVM_SOURCE%\libsycl\test\basic\multi_ptr.cpp" ^
  "%RUNTIME_BUILD%\lib\LLVMSYCL.lib" ^
  /Fe:"%CTS_BUILD%\multi-ptr.exe" /Fo:"%CTS_BUILD%\multi-ptr.obj"
set "PATH=%RUNTIME_BUILD%\lib;%LLVM_LIB_BUILD%\bin;%PATH%"
for %D in (0 1 2) do "%CTS_BUILD%\multi-ptr.exe" %D
```

Rebuild the earlier passing categories and remeasure the unchanged group and
ND-item prerequisites without excluding their copy tests:

```bat
python "%LLVM_SOURCE%\libsycl\test\cts\measure.py" ^
  --build-dir "%CTS_BUILD%" ^
  --report-dir "%CTS_BUILD%\pointer-barrier-final" ^
  --targets test_group test_nd_item test_item test_id ^
    test_error test_header test_range test_nd_range ^
  --devices NVIDIA AMD Intel --jobs 2 --timeout 900
```

The eight-category remeasurement preserves all six previously passing
categories: **109/109 cases and 12,562/12,562 assertions per vendor**.
Group and ND-item still fail compilation and execute no tests. Their recorded
diagnostics now include missing `sycl::byte`, missing `local_accessor`, and
unsupported over-aligned vector buffer elements. Their complete copy tests
also require device events, asynchronous group-copy operations, and group
execution APIs; no CTS assertions or test sources were changed.

Confirmed fully passing categories remain **6/97**, not eight passing
categories or a full-suite conformance result. The other 89 configured
categories were not remeasured. Eight focused compiler tests, the SPIR-V and
DirectX barrier IR checks, an actual unsupported-scope DXIL-link rejection,
and seven measurement-reporting unit tests pass. The next workgroup milestone
is real local-accessor allocation and its compiler/runtime ABI, followed by
copy/event and scheduling support.

### Runtime-sized local-accessor execution

One-, two-, and three-dimensional `local_accessor` objects now carry checked
ranges and opaque allocation tokens. Device captures retain true local-address-
space pointers. Compiler-assigned capture slots transport requested bytes and
element alignment separately from buffer resources and root constants.
Copies alias the same allocation; independent accessors remain disjoint.
Host dereferencing is explicitly rejected.

The existing prepared-IR specialization path now also compiles actual requested
local allocations into static groupshared arrays. Its cache key includes
workgroup shape and the ordered allocation sizes/identities. Every new variant
is compiled, signed, reflected, and checked against the portable 32 KiB limit,
including static storage already in the shader. Local slots always require a
matching variant; the minimally sized packaged image is never a fallback for
an allocation-bearing submission. Fixed workgroup kernels can specialize local
storage without allowing their workgroup dimensions to change.

Supported element types initially were mutable trivial types aligned from 4
through 16 bytes; supported memory operations were nonvolatile, non-atomic 32-bit
integer/float scalar accesses, including fields of trivial aggregates. The
wide-local extension below also supports 64-bit integer scalar accesses.
The vector and alignment extensions below add SYCL value arrays and alignment
through 128 bytes. The packed-byte extension below adds scalar 8-bit transfers.
Sixteen-bit local operations, 64-bit floating-point operations,
nontrivial construction, the
zero-dimensional API, properties, and complete local-accessor conformance
remain outside this increment. Submissions require `nd_range`; single-task
and implicit-range kernels are rejected. This allocation implementation is
currently DirectX-only and does not change other backends.

The native test exercises 1D/2D/3D storage, independent integer/float/16-byte-
aligned aggregate allocations, copied-accessor aliasing, cross-lane barriers,
multiple workgroups, changing local sizes and storage sizes, and fixed-shape
kernels. It checks the exact 32768-byte boundary, empty/overflowing/oversized
allocations, aggregate limits, invalid single-task use, cached launches with
compiler tools deliberately unavailable, and explicit failure for an uncached
allocation with those tools unavailable.

Rebuild the compiler, public headers, and paired runtime DLLs together, then
use the Windows build variables above:

```bat
set "OFFLOAD_DIRECTX_COMPILER_PATH=%COMPILER_BUILD%\bin"
set "OFFLOAD_DIRECTX_DXIL_VALIDATOR=%DXV%"
"%COMPILER_BUILD%\bin\clang-cl.exe" /nologo /std:c++17 /MD /EHsc /O2 /W4 /WX ^
  /clang:-fsycl /clang:--offload-targets=dxil-pc-shadermodel6.0-compute ^
  "/I%LLVM_LIB_BUILD%\include" ^
  /clang:-Xoffload-linker "/clang:--dxil-validator=%DXV%" ^
  "%LLVM_SOURCE%\libsycl\test\basic\local_accessor.cpp" ^
  "%RUNTIME_BUILD%\lib\LLVMSYCL.lib" ^
  /Fe:"%CTS_BUILD%\local-accessor.exe" /Fo:"%CTS_BUILD%\local-accessor.obj"
set "PATH=%RUNTIME_BUILD%\lib;%LLVM_LIB_BUILD%\bin;%PATH%"
for %D in (0 1 2) do "%CTS_BUILD%\local-accessor.exe" %D
```

The next workgroup tasks are device events, asynchronous group copies and waits,
and hierarchical group execution. Their CTS assertions are not replaced with
smoke tests or excluded to claim a category pass.

The native local-accessor regression passes on NVIDIA, AMD, and Intel, as do
the existing buffer, pointer/barrier, and workgroup-specialization regressions.
Nine focused compiler tests, 50 DirectX argument/manifest/shader/recipe unit
tests, 28 queue unit tests, and seven measurement-reporting unit tests pass.

The unchanged eight-category CTS rebuild preserves **109/109 cases and
12,562/12,562 assertions per vendor** across the six previously passing
categories. Group and ND-item still fail compilation and execute no tests.
Their diagnostics now reach missing `async_work_group_copy`, `wait_for`,
`sycl::byte`, and, for group tests, `parallel_for_work_group`.
The confirmed fully passing category count remains **6/97**; the other 89
configured categories were not measured in this run.

Refresh the paired DLLs and reproduce this measurement with:

```bat
cmake -E copy_if_different "%RUNTIME_BUILD%\lib\LLVMOffload.dll" "%CTS_BUILD%\bin"
cmake -E copy_if_different "%LLVM_LIB_BUILD%\bin\LLVMSYCL.dll" "%CTS_BUILD%\bin"
python "%LLVM_SOURCE%\libsycl\test\cts\measure.py" ^
  --build-dir "%CTS_BUILD%" ^
  --report-dir "%CTS_BUILD%\local-accessor-final" ^
  --targets test_group test_nd_item test_item test_id ^
    test_error test_header test_range test_nd_range ^
  --devices NVIDIA AMD Intel --jobs 2 --timeout 900
```

### Cooperative copies and device events

DirectX `group` and `nd_item` now provide the modern decorated-pointer
`async_work_group_copy` overloads for global-to-local and local-to-global
transfers, including source/destination strides. Source pointers can preserve
constness; destination elements must be mutable and match the source element
type. Supported local operations are nonvolatile, non-atomic 32/64-bit
integer or 32-bit float scalar accesses. This does not enable legacy pointer
overloads, general vector/narrow transfers, 64-bit floating-point transfers,
or hierarchical group execution.
Host calls and other device backends are explicitly unsupported.

Work-items cooperatively transfer elements according to their local linear IDs.
An all-memory workgroup synchronization precedes and follows each copy. The
implementation deliberately completes the entire copy before returning its
`device_event`: this is **eager completion, not hardware DMA or copy/compute
overlap**. `device_event::wait()` and variadic `wait_for` therefore need no
additional synchronization. An individual lane may wait on an already-completed
event without introducing an unsafe collective.

Two compiler fixes support these transfers. Floating-point local accesses use
bitcasts around the underlying 32-bit storage, including accesses at offset
zero. The non-dispatchable packaged image reserves enough placeholder storage
for statically known local accesses to pass DXIL validation; actual allocation
sizes still come from the runtime specialization inputs. Constant accesses
beyond a requested allocation are rejected rather than silently increasing its
size.

The native regression covers both entrypoints and directions, contiguous and
strided transfers, const sources, empty/single-element/non-multiple counts,
multiple workgroups, 1D/2D/3D shapes, integer/float data, unchanged input,
allocation guards and untouched stride gaps. It also checks multiple returned
events, variadic waits, and a wait performed by just one lane.

```bat
"%COMPILER_BUILD%\bin\clang-cl.exe" /nologo /std:c++17 /MD /EHsc /O2 /W4 /WX ^
  /clang:-fsycl /clang:--offload-targets=dxil-pc-shadermodel6.0-compute ^
  "/I%LLVM_LIB_BUILD%\include" ^
  /clang:-Xoffload-linker "/clang:--dxil-validator=%DXV%" ^
  "%LLVM_SOURCE%\libsycl\test\basic\workgroup_copy.cpp" ^
  "%RUNTIME_BUILD%\lib\LLVMSYCL.lib" ^
  /Fe:"%CTS_BUILD%\workgroup-copy.exe" /Fo:"%CTS_BUILD%\workgroup-copy.obj"
set "PATH=%RUNTIME_BUILD%\lib;%LLVM_LIB_BUILD%\bin;%PATH%"
set "OFFLOAD_DIRECTX_COMPILER_PATH=%COMPILER_BUILD%\bin"
set "OFFLOAD_DIRECTX_DXIL_VALIDATOR=%DXV%"
for %D in (0 1 2) do "%CTS_BUILD%\workgroup-copy.exe" %D
```

The copy regression passes on NVIDIA, AMD, and Intel, as do the local-accessor,
pointer/barrier, buffer, and workgroup-specialization regressions. Eleven focused
compiler tests, copy barrier-count/host-rejection checks, 50 DirectX unit tests,
28 queue unit tests, and seven measurement-reporting unit tests pass.

The unchanged `test_device_event` category now passes on all three vendors:
**1/1 test case per vendor**. Its original success path reports zero Catch2
assertions and uses `FAIL` if the copied sample is incorrect; no CTS source or
assertion was changed.

The nine-category rebuild preserves all earlier passing categories and advances
the confirmed count from **6/97 to 7/97**, with **110/110 cases and
12,562/12,562 assertions per vendor**. Group and ND-item still fail compilation
and execute no tests. Their remaining blockers include `sycl::byte`, broader
vector/local-accessor support, over-aligned buffers, and hierarchical group
execution. The other 88 configured categories were not measured; this is not
a full-suite conformance result.

Rebuild the earlier passing categories and remeasure the complete group/ND-item
prerequisites without excluding their broader copy tests:

```bat
cmake -E copy_if_different "%RUNTIME_BUILD%\lib\LLVMOffload.dll" "%CTS_BUILD%\bin"
cmake -E copy_if_different "%LLVM_LIB_BUILD%\bin\LLVMSYCL.dll" "%CTS_BUILD%\bin"
set "OFFLOAD_DIRECTX_COMPILER_PATH=%COMPILER_BUILD%\bin"
set "OFFLOAD_DIRECTX_DXIL_VALIDATOR=%DXV%"
python "%LLVM_SOURCE%\libsycl\test\cts\measure.py" ^
  --build-dir "%CTS_BUILD%" ^
  --report-dir "%CTS_BUILD%\cooperative-copy-final" ^
  --targets test_device_event test_group test_nd_item test_item test_id ^
    test_error test_header test_range test_nd_range ^
  --devices NVIDIA AMD Intel --jobs 2 --timeout 900
```

### 64-bit integer local storage and copies

The local-memory ABI now accepts scalar 64-bit integer loads and stores,
including the accesses used by cooperative copies. Values are split into two
32-bit groupshared words and reconstructed without losing either sign or high
bits. This does not require or implement 64-bit shared-memory atomics:
concurrent accesses still need ordinary SYCL data-race avoidance and barriers.
Narrow local operations, volatile/atomic local operations, and 64-bit
floating-point operations remain unsupported. Limited vector local support is
described below.

Each split access retains valid word alignment, memory alias metadata, and debug
locations. Whole-value range metadata is not copied onto the individual words.
Constant-access bounds and packaged placeholder storage account for the full
eight-byte access; undersized runtime allocations remain errors. Neither the
manifest format nor the allocation-token/cache-key ABI changes.

The existing `workgroup_copy.cpp` build/run command above now also tests signed
64-bit data with nonzero high words, low words whose sign bit is set, both
positive and negative values, neighbor-lane reads, both copy directions,
strides, guards, and completed-event waits. Its largest wide case uses two
2048-element allocations, totaling exactly **32768 bytes** of groupshared
storage, including guards.

The rebuilt copy test and the existing local-accessor, pointer/barrier, buffer,
and workgroup-specialization regressions pass on NVIDIA, AMD, and Intel.
Twelve focused compiler tests pass, including word-pair lowering, alignment,
range-metadata handling, and insufficient-allocation rejection.

The focused unchanged `test_device_event` remeasurement also passes its one
test case per vendor, with the original zero-assertion success path. Reproduce
it after rebuilding `opt` and configuring the specialization tools:

```bat
cmake --build "%COMPILER_BUILD%" --target opt
set "OFFLOAD_DIRECTX_COMPILER_PATH=%COMPILER_BUILD%\bin"
set "OFFLOAD_DIRECTX_DXIL_VALIDATOR=%DXV%"
python "%LLVM_SOURCE%\libsycl\test\cts\measure.py" ^
  --build-dir "%CTS_BUILD%" ^
  --report-dir "%CTS_BUILD%\wide-local-final" ^
  --targets test_device_event --devices NVIDIA AMD Intel --jobs 2 --timeout 900
```

This focused run measures one category, not the entire earlier baseline.
The confirmed category count remains **7/97** from the previous combined
measurement; no additional full-category CTS pass is claimed. Full group and
ND-item execution still requires the remaining type/storage and hierarchical
execution prerequisites.

### Aligned buffer storage

Buffers now transport their element alignment to the runtime instead of
rejecting over-aligned trivially copyable types. Host shadow storage uses a
checked, aligned subspan of its owning allocation. Allocation padding is kept
separate from the logical byte count: uploads, readback, device allocation, and
caller-owned writeback use only the requested element bytes, with the existing
DirectX final-word padding rule unchanged. Device storage requests the element
alignment through liboffload's aligned allocation API when it exceeds normal
alignment; backend alignment errors are surfaced. Invalid alignment and
storage-size overflow produce explicit SYCL exceptions.

The original two-argument buffer factory remains available for existing
normally aligned binaries. New alignment-aware callers require the matching
rebuilt headers and `LLVMSYCL.dll`. Buffer and host-accessor ownership,
cross-queue/context migration, and deferred writeback retain their existing
semantics.

This removes a storage prerequisite, **not all vector-memory restrictions**.
The native test executes scalar component accesses in buffers of
`vec<int, 4>` and `vec<float, 16>`, plus an `alignas(256)` aggregate. It does
not claim general whole-vector operations or cooperative vector-copy
conformance. The subsequent local-vector increment is described below.

Rebuild the runtime/public headers together and use the Windows variables above:

```bat
cmake --build "%RUNTIME_BUILD%" --target LLVMSYCL
"%COMPILER_BUILD%\bin\clang-cl.exe" /nologo /std:c++17 /MD /EHsc /O2 /W4 /WX ^
  /clang:-fsycl /clang:--offload-targets=dxil-pc-shadermodel6.0-compute ^
  "/I%LLVM_LIB_BUILD%\include" ^
  /clang:-Xoffload-linker "/clang:--dxil-validator=%DXV%" ^
  "%LLVM_SOURCE%\libsycl\test\basic\buffer_alignment.cpp" ^
  "%RUNTIME_BUILD%\lib\LLVMSYCL.lib" ^
  /Fe:"%CTS_BUILD%\buffer-alignment.exe" /Fo:"%CTS_BUILD%\buffer-alignment.obj"
set "PATH=%RUNTIME_BUILD%\lib;%LLVM_LIB_BUILD%\bin;%PATH%"
for %D in (0 1 2) do "%CTS_BUILD%\buffer-alignment.exe" %D
```

The regression checks actual **16-, 64-, and 256-byte pointer alignment**,
owned and caller-owned 2D buffers, GPU updates across two contexts, host edits
and re-upload, retained host-accessor lifetime, and untouched neighboring
objects. It changes external guard objects after buffer construction so that
overlong writeback cannot pass by restoring their original values. Invalid
internal alignment and allocation-padding overflow are also rejected.

It passes on NVIDIA, AMD, and Intel. The pre-existing buffer,
cooperative-copy, local-accessor, pointer/barrier, and workgroup-specialization
binaries also pass against the rebuilt runtime without recompilation.

After refreshing both DLLs in the CTS executable directory, the unchanged
header, error, and device-event categories pass **4/4 cases and 2/2 assertions
per vendor**:

```bat
cmake -E copy_if_different "%RUNTIME_BUILD%\lib\LLVMOffload.dll" "%CTS_BUILD%\bin"
cmake -E copy_if_different "%LLVM_LIB_BUILD%\bin\LLVMSYCL.dll" "%CTS_BUILD%\bin"
set "OFFLOAD_DIRECTX_COMPILER_PATH=%COMPILER_BUILD%\bin"
set "OFFLOAD_DIRECTX_DXIL_VALIDATOR=%DXV%"
python "%LLVM_SOURCE%\libsycl\test\cts\measure.py" ^
  --build-dir "%CTS_BUILD%" ^
  --report-dir "%CTS_BUILD%\aligned-buffer-final" ^
  --targets test_device_event test_header test_error ^
  --devices NVIDIA AMD Intel --jobs 2 --timeout 900
```

Only these three categories were remeasured in this run. The previously
confirmed count remains **7/97**, with no new full-category pass claimed.
The group/ND-item prerequisites still include narrow types, legacy interfaces,
and hierarchical group execution.

### Vector local storage and cooperative copies

Local accessors now accept trivially copyable SYCL `vec` and `marray` value
arrays, even when their default constructor is not trivial. Like scalar local
storage, their device contents are not initialized by constructing the accessor.
This increment retained mutable-element, trivial-destruction, **16-byte alignment**, and
**32768-byte total workgroup storage** limits remain in force.

The compiler accepts fixed-vector accesses of 32/64-bit integers and 32-bit
floats in both buffer and local memory. It splits them into scalar accesses
before pointer validation/remapping, retaining scalar alignment, alias metadata,
and debug locations. Local 64-bit components use the existing word-pair lowering.
Constant nonvolatile memory copies with four-byte-aligned operands and a size
that is a multiple of four, at most 32768 bytes, are also expanded into word
loads/stores. This handles compiler-generated vector assignments without a
special vector-copy path in `group`; all words are loaded first so overlapping
`memmove` remains correct. Address-space casts are preserved until remapping.
Atomic and volatile accesses, narrow components, 64-bit floating point, and
larger local alignments were not enabled by this increment. The following
alignment extension raises that limit.

Cooperative-copy synchronization and eager event completion are unchanged.
The existing native regression now includes `vec<int, 4>`, padded
`vec<float, 3>`, `vec<long long, 2>`, and `marray<int, 3>`, with distinct component
values, both directions, contiguous/strided transfers, zero and partial counts,
guard objects, neighbor-lane visibility, and completed-event waits. The largest
16-byte-vector cases use two 1024-element allocations, including guards, exactly
**32768 bytes**. It passes on NVIDIA, AMD, and Intel, as do the existing aligned
buffer, multidimensional buffer, local-accessor, pointer/barrier, and
workgroup-specialization binaries.

Rebuild the compiler tools and runtime/public headers together, then use the
existing `workgroup_copy.cpp` Windows build/run command above:

```bat
cmake --build "%COMPILER_BUILD%" --target clang opt llc
cmake --build "%RUNTIME_BUILD%" --target LLVMSYCL
```

Twelve focused ABI compiler tests pass, including scalarized vector loads/stores,
casted global-to-local copies, overlapping word copies, full constant-access
bounds, and rejection of partial-word copies and volatile vector loads. A
broader selection also exposes a pre-existing diagnostic mismatch in
`cpp-kernel-capture-invalid.test`: its dynamic-index expectation differs from
the diagnostic already present before this increment. That unrelated test is
not changed or counted as passing.

The unchanged five-category CTS remeasurement passes header, error, and
device-event tests: **4/4 cases and 2/2 assertions per vendor**. Full group and
ND-item still fail compilation and execute no tests; no CTS source or assertion
is changed, and failed categories are retained in the report. Diagnostics now
include `sycl::byte`, over-16-byte local alignments, scalar `half` local-accessor
construction, and missing hierarchical group submission. Further narrow-type,
legacy-interface, and execution prerequisites remain.

```bat
cmake -E copy_if_different "%RUNTIME_BUILD%\lib\LLVMOffload.dll" "%CTS_BUILD%\bin"
cmake -E copy_if_different "%LLVM_LIB_BUILD%\bin\LLVMSYCL.dll" "%CTS_BUILD%\bin"
set "OFFLOAD_DIRECTX_COMPILER_PATH=%COMPILER_BUILD%\bin"
set "OFFLOAD_DIRECTX_DXIL_VALIDATOR=%DXV%"
python "%LLVM_SOURCE%\libsycl\test\cts\measure.py" ^
  --build-dir "%CTS_BUILD%" --report-dir "%CTS_BUILD%\vector-local-final" ^
  --targets test_device_event test_group test_nd_item test_header test_error ^
  --devices NVIDIA AMD Intel --jobs 2 --timeout 900
```

The runner exits with failure because group and ND-item do not compile; it does
not run stale executables for those categories. The previously confirmed count
remains **7/97**, not a new group/ND-item category pass or full SYCL conformance.

### Over-aligned local storage

Local accessor elements and captured local slots now support power-of-two
alignment through **128 bytes**, covering the alignment of standard SYCL
vectors up to sixteen 64-bit components. The frontend already records element
alignment; the public accessor, compiler ABI, and manifest validator now accept
the larger values. The compiled groupshared globals retain that alignment.
The token and specialization-cache formats are unchanged. New over-aligned
packages need the matching compiler and runtime; older runtimes reject them.

Manifest validation now accounts for alignment gaps between independent local
allocations, matching compiler and runtime accounting. Aliases count once and
must still agree on allocation size and alignment. Invalid alignment,
unaligned sizes, inconsistent aliases, and totals exceeding **32768 bytes**
remain explicit errors. This does not enable narrow local operations, scalar
`half` construction, 64-bit floating-point execution, atomics, or volatile
accesses.

The cooperative-copy regression adds `vec<int, 8>` (32-byte alignment),
`vec<float, 16>` (64), and `vec<long long, 16>` (128). Each runs contiguous and
strided transfers in both directions, distinct component values, guard checks,
barriers, zero/partial counts, and a largest pair of allocations totaling
exactly **32768 bytes**, including guards. The local-accessor regression now
mixes 128-byte-aligned aggregates with integer/float allocations, checks aliases
and neighbor-lane visibility, accepts a near-limit layout, and rejects the
next larger layout.

Both rebuilt native tests pass on NVIDIA, AMD, and Intel. Existing aligned
buffer, multidimensional buffer, pointer/barrier, and workgroup-specialization
binaries also pass with the updated DLLs. Thirteen focused compiler tests and
25 manifest/workgroup unit tests pass. The unrelated pre-existing
`cpp-kernel-capture-invalid.test` diagnostic mismatch described above remains
unchanged and is not counted as passing.

Use the existing Windows compilation commands for `workgroup_copy.cpp` and
`local_accessor.cpp` after rebuilding all three components:

```bat
cmake --build "%COMPILER_BUILD%" --target clang opt llc
cmake --build "%RUNTIME_BUILD%" --target LLVMSYCL directx.unittests
```

The unchanged CTS remeasurement retains five selected categories. Header,
error, and device-event pass **4/4 cases and 2/2 assertions per vendor**.
Group and ND-item still fail compilation and execute no tests. Their current
diagnostics include missing `sycl::byte`, scalar `half` local-accessor
construction, `sycl::opencl` compatibility names, and hierarchical group
submission; the former over-16-byte local-alignment errors are gone.

```bat
cmake -E copy_if_different "%RUNTIME_BUILD%\lib\LLVMOffload.dll" "%CTS_BUILD%\bin"
cmake -E copy_if_different "%LLVM_LIB_BUILD%\bin\LLVMSYCL.dll" "%CTS_BUILD%\bin"
set "OFFLOAD_DIRECTX_COMPILER_PATH=%COMPILER_BUILD%\bin"
set "OFFLOAD_DIRECTX_DXIL_VALIDATOR=%DXV%"
python "%LLVM_SOURCE%\libsycl\test\cts\measure.py" ^
  --build-dir "%CTS_BUILD%" --report-dir "%CTS_BUILD%\local-alignment-final" ^
  --targets test_device_event test_group test_nd_item test_header test_error ^
  --devices NVIDIA AMD Intel --jobs 2 --timeout 900
```

The runner correctly exits with failure for the two compile-failed categories.
No CTS assertions or category sources are changed. The previously confirmed
count remains **7/97**; this is an alignment prerequisite, not full group,
ND-item, or SYCL conformance.

### Packed 8-bit local transfers

Local memory now supports ordinary signed and unsigned 8-bit loads/stores.
Byte offsets are retained throughout pointer remapping, then split into a
groupshared word index and bit offset. Four adjacent byte elements share one
32-bit word. Writes use compare-exchange retry loops so independent lane writes
cannot overwrite neighboring bytes; reads use atomic OR with zero to observe
the word, then extract the requested byte. Potentially uninitialized neighboring
bits are frozen before use. These are internal implementation operations, not
an advertised user atomic API or permission for data races on the same byte.
Barrier and eager cooperative-event semantics remain unchanged.

Accessor allocation sizes are rounded up to complete four-byte words, while
`size()` and `byte_size()` retain their logical element counts. The compiler and
prepared-IR reader normalize subword element alignment to four-byte slot
alignment; invalid source alignment is still rejected. Rounded allocation sizes
and alignment gaps count toward the existing **32768-byte** workgroup limit.
Non-byte accesses retain their four-byte-aligned offset requirement.

DXIL preparation also emits typed-pointer casts for atomics on array allocations.
Explicit groupshared word GEPs avoid folded byte-pointer constants that cannot
be decoded correctly as legacy typed pointers. This fixes the validation failure
encountered while implementing packed local storage.

The existing cooperative-copy regression now tests signed-char 1D and
unsigned-char 2D transfers with 32 lanes. Inputs cover every byte bit pattern;
the tests check contiguous/strided copies in both directions, zero/partial
counts, guards sharing words with data, neighbor-lane reads, and CPU-verified
modular arithmetic. The largest case has two 16384-byte allocations including
guards, exactly **32768 bytes**. Scalar, vector, and marray copy cases still run.

All cases pass on NVIDIA, AMD, and Intel. The existing aligned local-accessor,
buffer, pointer/barrier, and workgroup-specialization binaries also pass.
Thirty-six focused compiler/atomic tests and 25 manifest/workgroup unit tests
pass, including profiled retry-branch metadata, allocation bounds, atomic
array pointer casts, and subword recipe-alignment validation. The pre-existing
capture diagnostic mismatch remains unchanged and is not counted as passing.

Use the existing `workgroup_copy.cpp` Windows compilation/run command after:

```bat
cmake --build "%COMPILER_BUILD%" --target clang opt llc
cmake --build "%RUNTIME_BUILD%" --target LLVMSYCL directx.unittests
```

This is **scalar 8-bit transfer support**, not all narrow/vector operations.
Sixteen-bit transfers, scalar `half` construction, the deprecated `sycl::byte`
interface, OpenCL compatibility names, and hierarchical group submission remain
separate prerequisites.

The unchanged five-category CTS measurement passes header, error, and
device-event: **4/4 cases and 2/2 assertions per vendor**. Full group and ND-item
still fail compilation and execute no tests. No CTS sources or assertions are
changed, and the report retains both failed categories:

```bat
cmake -E copy_if_different "%RUNTIME_BUILD%\lib\LLVMOffload.dll" "%CTS_BUILD%\bin"
cmake -E copy_if_different "%LLVM_LIB_BUILD%\bin\LLVMSYCL.dll" "%CTS_BUILD%\bin"
set "OFFLOAD_DIRECTX_COMPILER_PATH=%COMPILER_BUILD%\bin"
set "OFFLOAD_DIRECTX_DXIL_VALIDATOR=%DXV%"
python "%LLVM_SOURCE%\libsycl\test\cts\measure.py" ^
  --build-dir "%CTS_BUILD%" --report-dir "%CTS_BUILD%\local-byte-final" ^
  --targets test_device_event test_group test_nd_item test_header test_error ^
  --devices NVIDIA AMD Intel --jobs 2 --timeout 900
```

The runner exits with failure for group/ND-item compile failures and never runs
their stale executables. Confirmed coverage remains **7/97 categories**.

### Full 97-category inventory

The 2026-11-24 inventory attempts **all 97 configured categories**, retaining
full-conformance, deprecated-feature, half, and double test configuration.
OpenCL interop remains disabled as in the earlier baseline. No CTS sources,
assertions, category selection, or feature-test settings were changed during
this inventory. The checkout retains the existing adapter-only additions:
`LibSYCL` in CMake's known-implementation list and its identity registration
in `disabled_for_test_case.h`. Neither addition changes a test or assertion.
LLVM revision `4a8d553815b24d4c2bcda13153b660ede0d31b10` and CTS revision
`b74823b4179358851d7949d816e41e6f1a7e105a` identify this baseline.
The compiler/linker tools, validator, staged runtime DLLs, target manifest, and
copied public SYCL headers were SHA256-hashed before measurement and verified
unchanged afterward. Builds use two jobs; each successfully built executable
is attempted on NVIDIA, AMD, and Intel.

| Outcome | Categories |
|---|---:|
| Object compilation fails | 82 |
| Objects compile, executable/device link fails | 3 |
| Executable builds, but every test is skipped on each vendor | 2 |
| Executable builds and tests pass on all three vendors | 10 |
| Unmeasured categories | 0 |

The **10/97 confirmed passing categories** are `device_event`, `error`,
`header`, `id`, `item`, `kernel_args`, `language`, `namespace`, `nd_range`, and
`range`. The last three newly confirmed categories relative to the earlier
seven-category baseline are `kernel_args`, `language`, and `namespace`.
They were already supported: the inventory increases measured coverage
without implementing additional functionality.

The ten passing categories report **114/114 cases and 12,565/12,565 assertions
per vendor**. These are totals from the passing categories, not complete
executed-suite totals. `is_device_copyable` skips its one case because
`SYCL_DEVICE_COPYABLE` is undefined; `sycl_external` skips all six cases because
`SYCL_EXTERNAL` is unsupported. Neither is counted as passing. The unchanged
measurement runner records their nonzero process outcomes as failed runs and
cannot parse the skipped-case summary, so its aggregate `counts_complete`
remains false. The raw logs establish that these are all-skipped categories,
not observed failing assertions. Failed builds never run stale executables.

The three executable/device-link failures are:

- `bit_cast`: an assertion in the DXIL array flattener rejects a GEP whose
  pointer operand is a PHI.
- `exception_handling`: the kernel ABI rejects a kernel without the expected
  by-value capture record/layout; these tests contain empty-capture kernels.
- `pointers`: DXIL validation rejects an unused external constant-array
  declaration and remaining i8 operations.

The compile failures expose several shared families:

| Family | Observed blockers and scope |
|---|---|
| Compatibility names / shared CTS helpers | Missing `host_image` is the first diagnostic in 25 kernel-bundle/ID-related categories. The shared helper also references missing image targets and placeholder names. These names do not implement images or guarantee bundle execution. |
| Numeric value types | Missing `rounding_mode` is the first diagnostic in seven vector categories. Other gaps include vector deduction guides, boolean marray aliases, compound/bitwise/increment operators, and scalar relational overloads. |
| Application API surface | Accessor iterators, zero-dimensional local accessors, buffer allocators/properties, context/queue information, extension queries, host tasks, and USM allocators are incomplete. |
| Workgroup execution | Group/ND-item still need byte/half compatibility and transfer prerequisites; group, hierarchical, and h-item tests also require hierarchical execution. |
| Larger runtime/compiler capabilities | User atomics/fences and capability queries, reductions, subgroups, math builtins, images, specialization constants, and broader kernel/program APIs remain substantial work. |

These families overlap. The report keeps up to twenty diagnostics per build;
the complete logs remain authoritative. First-error frequency is an indicator
of shared blockers, **not a prediction of how many categories a small fix will
make pass**. Compile-error limits and subsequent device-link/runtime failures
can conceal additional requirements.

#### Ranked next objectives

The inventory changes the priority from predetermined ND-item work to the
following evidence-based candidates. Effort is relative, not a schedule or a
guarantee of a category pass:

| Rank | Objective | Evidence / expected effort |
|---|---|---|
| 1 | Complete the platform extension-query API and close `platform` | Its reported compilation blocker is `platform::has_extension`. Small API completion using truthful extension reporting; verify the entire category. |
| 2 | Implement the deprecated byte interface and close `scalars` | Its reported errors are the two missing `sycl::byte` uses. Small compatibility work, also removing a group/ND-item blocker; do not assume those larger categories then pass. |
| 3 | Implement standard vector deduction guides and close `vector_deduction_guides` | The category exercises host-side construction/type/value checks for 1/2/3/4/8/16 elements. Small, bounded library change without new GPU execution machinery. |
| 4 | Support valid empty-capture kernels and retry `exception_handling` | Object compilation already succeeds. A focused compiler/ABI prerequisite, with asynchronous-error behavior still needing validation after linking works. |
| 5 | Complete shared compatibility names, then rebuild the kernel-ID/bundle family | Twenty-five categories encounter the same helper-level first error. Small naming work may reveal many downstream blockers; close the cheapest kernel-ID or zero-kernel category actually supported, rather than promising 25 passes. |

Marray API completion is the next candidate family after these: six configured
marray categories expose library gaps that can share operator implementations,
but narrow/private device operations may add backend work. ND-item, general
atomics/subgroups, hierarchical execution, and OpenCL/OpenMP integration should
not displace cheaper demonstrated category candidates merely because they were
the previous workstream. Every closure requires the full unchanged applicable
category to pass on all three vendors; skipped feature tests are not progress.

Reproduce without a `--targets` filter, keeping compiler/runtime/header inputs
fixed for the duration:

```bat
cmake -E copy_if_different "%RUNTIME_BUILD%\lib\LLVMOffload.dll" "%CTS_BUILD%\bin"
cmake -E copy_if_different "%LLVM_LIB_BUILD%\bin\LLVMSYCL.dll" "%CTS_BUILD%\bin"
set "OFFLOAD_DIRECTX_COMPILER_PATH=%COMPILER_BUILD%\bin"
set "OFFLOAD_DIRECTX_DXIL_VALIDATOR=%DXV%"
python "%LLVM_SOURCE%\libsycl\test\cts\measure.py" ^
  --build-dir "%CTS_BUILD%" ^
  --report-dir "%CTS_BUILD%\full-inventory-2026-11-24" ^
  --devices NVIDIA AMD Intel --jobs 2 --timeout 900
```

The report directory contains `baseline.json`, per-stage/per-device logs, and
the original `inputs.json` hash record. The runner exits with failure because
the full configuration does not pass; this inventory is not SYCL conformance.
Earlier seven-category counts above are historical focused measurements,
superseded by this full **10/97** inventory.

### Platform extension queries and first inventory-driven closure

The first ranked objective closes the full unchanged `platform` category.
`platform::has_extension` and the
`get_info<info::platform::extensions>` descriptor are now available. The query
returns an empty extension list because libsycl currently implements no
extension-string contracts; `has_extension` performs exact membership checks
against that same list. It does not infer OpenCL extensions from DirectX
hardware features or claim that optional SYCL aspects are extensions.
The extension-list result is deliberate capability reporting, not a fallback
for a failed backend query. Existing name/vendor/version queries and `has`
aspect reporting are unchanged. `has_extension` uses the standard SYCL 2020
deprecation annotation; the new symbols require the matching rebuilt runtime.

The existing five platform discovery/grouping/capability unit tests pass.
The whole unchanged CTS platform category builds and passes on NVIDIA, AMD,
and Intel: **4/4 cases and 57/57 assertions per vendor**, with complete totals,
no skipped cases, and no failed assertions. No CTS test or assertion is changed.

This advances cumulative confirmed coverage from **10/97 to 11/97**.
The earlier ten-category passing baseline plus this platform measurement
account for **118 cases and 12,622 assertions per vendor** across those
eleven categories. Only platform was rebuilt and remeasured in this increment;
this is not a fresh full-suite measurement. The earlier full-inventory failures
and all-skipped categories remain documented, not silently reclassified.

Rebuild the runtime/public headers together and run without filtering cases:

```bat
cmake --build "%RUNTIME_BUILD%" --target LLVMSYCL PlatformTests
cmake -E copy_if_different "%RUNTIME_BUILD%\lib\LLVMOffload.dll" "%CTS_BUILD%\bin"
cmake -E copy_if_different "%LLVM_LIB_BUILD%\bin\LLVMSYCL.dll" "%CTS_BUILD%\bin"
set "OFFLOAD_DIRECTX_COMPILER_PATH=%COMPILER_BUILD%\bin"
set "OFFLOAD_DIRECTX_DXIL_VALIDATOR=%DXV%"
python "%LLVM_SOURCE%\libsycl\test\cts\measure.py" ^
  --build-dir "%CTS_BUILD%" --report-dir "%CTS_BUILD%\platform-extensions-final" ^
  --targets test_platform --devices NVIDIA AMD Intel --jobs 2 --timeout 900
```

The next ranked objective is deprecated byte compatibility and full scalar
category closure, followed by vector deduction guides.

### Deprecated byte compatibility and scalar category closure

`sycl::byte` is now available as an unsigned eight-bit integer alias,
`std::uint8_t`, with the standard SYCL 2020 deprecation annotation recommending
`std::byte`. It retains integer conversion/arithmetic semantics; it is not an
alias of the distinct C++17 `std::byte` enum. This library compatibility addition
does not change buffer/local storage, barriers, packing, or advertised device
aspects.

The full unchanged scalar CTS category builds and passes on NVIDIA, AMD, and
Intel: **2/2 cases and 17/17 assertions per vendor**. Deprecated-feature, half,
double, and full-conformance test settings are unchanged. Optional device
branches still follow the reported aspects; this does not enable FP16/FP64
arithmetic. The platform, header, and error categories also pass in the same
remeasurement, totaling **9/9 cases and 76/76 assertions per vendor** across
the four selected categories, with complete totals and no failing or skipped
cases. No CTS source or assertion is changed.

Cumulative confirmed coverage advances from **11/97 to 12/97**, accounting for
**120 cases and 12,639 assertions per vendor** across the earlier passing
baseline and the platform/scalar closures. Only the four selected categories
were remeasured here; the other earlier results are not a fresh full-suite run.
Byte compatibility also removes one known group/ND-item compilation blocker,
but no new group or ND-item pass is claimed.

Rebuild the runtime/public headers together, then reproduce:

```bat
cmake --build "%RUNTIME_BUILD%" --target LLVMSYCL
cmake -E copy_if_different "%RUNTIME_BUILD%\lib\LLVMOffload.dll" "%CTS_BUILD%\bin"
cmake -E copy_if_different "%LLVM_LIB_BUILD%\bin\LLVMSYCL.dll" "%CTS_BUILD%\bin"
set "OFFLOAD_DIRECTX_COMPILER_PATH=%COMPILER_BUILD%\bin"
set "OFFLOAD_DIRECTX_DXIL_VALIDATOR=%DXV%"
python "%LLVM_SOURCE%\libsycl\test\cts\measure.py" ^
  --build-dir "%CTS_BUILD%" --report-dir "%CTS_BUILD%\scalar-byte-final" ^
  --targets test_scalars test_platform test_header test_error ^
  --devices NVIDIA AMD Intel --jobs 2 --timeout 900
```

The next ranked objective is vector deduction guides and the full unchanged
deduction-guide category.

### Vector deduction guides

The standard `vec(T, U...) -> vec<T, sizeof...(U) + 1>` deduction guide is now
available. Scalar arguments determine the element type from the first argument
and the vector length from the argument count; the existing vector-size and
constructor rules still apply. Explicit vector types, their layout, and copy
construction semantics are unchanged.

The focused `value_types.cpp` regression checks constant-evaluated deduction
from const scalar lvalues, three-element type/value preservation, single-element
deduction, and copy deduction retaining the original vector type rather than
creating a vector of vectors. Its existing half conversion and explicit
vector/marray checks also pass with C++17 and warnings treated as errors.

The entire unchanged `vector_deduction_guides` CTS category passes:
**1/1 case and 480/480 assertions** for each NVIDIA, AMD, and Intel selection.
It checks host-side type/value deduction across the full configured scalar type
set and lengths 1/2/3/4/8/16. It does not dispatch a GPU kernel or establish
broader vector arithmetic/device-language support. The scalar and platform
categories also pass in the same run, totaling **7/7 cases and 554/554
assertions per vendor selection**, with complete totals and no skipped or
failed cases. CTS sources, assertions, and configuration are unchanged.

Cumulative confirmed coverage advances from **12/97 to 13/97**,
accounting for **121 cases and 13,119 assertions per vendor** across the
earlier passing baseline and the three inventory-driven closures. Only the
three selected categories were rebuilt and remeasured in this increment;
the cumulative count is not a fresh full-suite measurement.

Rebuild the runtime/public headers together and reproduce:

```bat
cmake --build "%RUNTIME_BUILD%" --target LLVMSYCL
cmake -E copy_if_different "%RUNTIME_BUILD%\lib\LLVMOffload.dll" "%CTS_BUILD%\bin"
cmake -E copy_if_different "%LLVM_LIB_BUILD%\bin\LLVMSYCL.dll" "%CTS_BUILD%\bin"
set "OFFLOAD_DIRECTX_COMPILER_PATH=%COMPILER_BUILD%\bin"
set "OFFLOAD_DIRECTX_DXIL_VALIDATOR=%DXV%"
python "%LLVM_SOURCE%\libsycl\test\cts\measure.py" ^
  --build-dir "%CTS_BUILD%" --report-dir "%CTS_BUILD%\vector-deduction-final" ^
  --targets test_vector_deduction_guides test_scalars test_platform ^
  --devices NVIDIA AMD Intel --jobs 2 --timeout 900
```

The next ranked objective is valid empty-capture kernels and a complete retry
of the exception-handling category, retaining its original error assertions.

### DirectX empty-capture kernel ABI

The DirectX compiler ABI and specialization-recipe reader now accept capture
layout metadata containing only the record size and pointer width, without any
field entries. This is the layout emitted for a C++ captureless lambda.
Previously both paths required at least one field: fixing compiler emission
alone produced signed DXIL but still failed runtime image compatibility.

The existing capture ABI is preserved. The host passes one complete nonzero-size
object record (one byte for the tested empty lambda), while the executable has
no capture arguments or resource/scalar bindings. Record-size, pointer-width,
by-value parameter, and field-access validation remain in force. No dummy field,
dummy buffer, generic SYCL API change, or relaxed image-compatibility fallback
is used.

Four focused compiler regressions pass, including the new empty-capture case,
DXIL object emission, rejection of padding reads, and malformed record size.
All twelve DirectX workgroup-recipe tests pass with tools configured; the new
test accepts an empty recipe layout and rejects mismatched sizes or bindings.
The unchanged exception-handling CTS executable now builds, signs, validates,
loads, and runs its captureless kernels on NVIDIA, AMD, and Intel:
**2/2 cases per vendor**. Its original success paths report zero assertions and
use failure assertions on errors. This verifies ordinary host exception
propagation and successful asynchronous submissions, not device-side exception
throwing or comprehensive asynchronous backend-failure handling.

Kernel-argument and language CTS regressions also pass, totaling **4/4 cases
and 1/1 assertion per vendor** across the three selected categories. No CTS
source, assertion, or feature configuration is changed. Cumulative confirmed
coverage advances from **13/97 to 14/97**, accounting for **123 cases and
13,119 assertions per vendor** across earlier passing measurements and this
closure. Only these three categories were remeasured here.

Rebuild both the compiler and DirectX runtime, then refresh both staged DLLs:

```bat
cmake --build "%COMPILER_BUILD%" --target clang opt llc
cmake --build "%RUNTIME_BUILD%" --target LLVMOffload directx.unittests
cmake -E copy_if_different "%RUNTIME_BUILD%\lib\LLVMOffload.dll" "%CTS_BUILD%\bin"
cmake -E copy_if_different "%LLVM_LIB_BUILD%\bin\LLVMSYCL.dll" "%CTS_BUILD%\bin"
set "OFFLOAD_DIRECTX_COMPILER_PATH=%COMPILER_BUILD%\bin"
set "OFFLOAD_DIRECTX_DXIL_VALIDATOR=%DXV%"
python "%LLVM_SOURCE%\libsycl\test\cts\measure.py" ^
  --build-dir "%CTS_BUILD%" --report-dir "%CTS_BUILD%\empty-capture-final" ^
  --targets test_exception_handling test_kernel_args test_language ^
  --devices NVIDIA AMD Intel --jobs 2 --timeout 900
```

The next DirectX-specific objective is the pointer-PHI assertion in DXIL array
flattening exposed by the full `bit_cast` category, rather than additional
unrelated libsycl API completion.

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

Uniform local-shape and bounded local-allocation regeneration are implemented
by the prepared-IR recipe above. This section describes the remaining general
local-memory and
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

The local-accessor slots above establish the bounded SYCL path. Future OpenCL
local-memory arguments and broader local-element support need stable
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
