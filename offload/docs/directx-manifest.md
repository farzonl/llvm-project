# Experimental DirectX launch manifest

The DirectX prototype uses a versioned JSON launch manifest stored under the
`directx.manifest` string-data key of an LLVM OffloadBinary entry. Each entry
contains one executable compute DXIL shader and the manifest for its one entry
point. Multiple entries keep independent payload/manifest associations.
The image kind is `IMG_DXIL`, the producer kind is `OFK_None` or `OFK_SYCL`,
and entry flags are zero. SYCL images require a capture-record layout.
The DXIL payload must be nonempty.

Manifest decoding, LLVM-native DXIL/manifest validation, and single-entry program
loading with device-capability checks, canonical native root-signature creation,
and kernel objects with compute pipelines are implemented. `olLaunchKernel`
supports compute execution with bounded raw buffers and scalar root
constants. The prototype ABI is experimental.

The restricted C++ compiler path generates argument bindings and this manifest
in `llvm.offload.manifest` intermediate bitcode metadata. The standalone demo's
`llvm-offload-binary` reads that metadata with the `directx-metadata` image key
and adds feature declarations from the final DXIL
container. The metadata is not embedded in executable DXIL: validation rejects
unknown named metadata. This path currently accepts only indexed buffer-pointer
arguments with 32-bit accesses, 32-bit integer/float scalar arguments packed
into `b0, space0` root constants, and fixed workgroup sizes.
See the [C++ demo](directx-demo.md) for the supported subset and build pipeline.

## Restricted capture-record ABI

An optional `capture` object describes one host kernel argument block:

```json
"capture": {"size": 32, "pointer_size": 8, "offsets": [0, 8, 16, 24]}
```

Its offsets correspond to the dense indices in `arguments`, not their order
in the JSON array. Each offset identifies the source bytes for that binding;
scalar `offset` fields still describe destinations in root constants.
Fields must be aligned, ordered, nonoverlapping, and entirely inside the
record. Pointer fields carry native 64-bit allocation tokens, not shader
addresses. With `capture`, a launch supplies exactly one argument whose size
matches the record, rather than one argument per binding. The runtime copies
its scalar values and resolves its buffer tokens during submission.
Existing manifests without `capture` retain the separate-argument ABI.

The experimental Clang `-cc1 -fsycl-is-device` compute path records the actual
AST capture-field offsets and sizes in `dx.capture.layout` function metadata.
It requires an x64 auxiliary host target for captured pointers. The DirectX
kernel ABI lowers a single by-value record into resources and root constants,
and removes this metadata and the capture record before executable emission.
Flat captures of buffer pointers and 32-bit integers/floats are supported;
aggregates, unused fields, dynamic field indexing, and pointer escapes are
rejected. Buffer access instructions determine SRV versus UAV bindings.

The experimental libsycl DirectX backend accepts packaged SYCL DXIL images.
`llvm-offload-binary` generates their binary SYCL symbol tables from the
captured entry name; Clang's host embedding registers them automatically.
libsycl loads each image as a separate one-entry package so its manifest is
preserved. See the [parallel SYCL demo](directx-sycl.md). Device index/range
helpers now expose work-item, group, and range queries through the NDRange
dispatch ABI below. Automatic driver/linker compilation remains future work.

## NDRange dispatch ABI

An optional versioned `dispatch` object reserves an implicit region in the
existing root-constant block:

```json
"dispatch": {"version": 1, "offset": 4}
```

Version 1 occupies 36 bytes: three 32-bit group counts in native x/y/z order,
followed by three 64-bit global offsets, each stored low word first.
The region must be dword-aligned, in bounds, and disjoint from user scalar
arguments. It counts against the 252-byte root-constant budget; kernels using
dispatch data can therefore carry at most 54 additional 32-bit scalar values.
Executable cbuffer annotations must account for every dispatch field.

The compiler lowers `llvm.dx.num.groups` and `llvm.dx.global.offset` queries
to this block. Local sizes remain the compiled `numthreads`; global sizes
are group counts multiplied by local sizes. The ABI is independent of
SYCL captures and also applies to flat C++ kernel arguments, so a future
OpenCL frontend can use the same geometry transport and target intrinsics.
Version 1 covers uniform workgroups. Non-uniform OpenCL workgroups and
runtime workgroup specialization require further compiler/runtime support;
the version field allows their ABI requirements to evolve without changing
existing packages.

`olLaunchKernel` accepts an optional `OL_KERNEL_LAUNCH_PROP_TYPE_GLOBAL_OFFSET`
property pointing to `ol_kernel_global_offset_t`. The three 64-bit values use
native axis order. Inactive axes require zero offsets; duplicates, null data,
and offset-plus-range overflow are errors. Backends without offset support,
and DirectX kernels without a dispatch block, reject nonzero offsets rather
than ignoring them. Omitting the property supplies zero offsets.

The runtime snapshots group counts and offsets into submission-owned constants.
Changing the host property or launching another range cannot alter pending
work. SYCL reverses its dimension order when converting ranges and offsets:
the last SYCL dimension maps to native x. No application-authored capture
fields, manifest, or scalar argument is needed for launch geometry.

## Program loading

`olCreateProgram` accepts exactly one entry in one current-version (version 3),
uncompressed OffloadBinary package. Multi-entry packages are explicitly
`UNSUPPORTED`; no entry is silently selected. Concatenated packages, trailing
bytes, malformed package metadata, missing manifests, and mismatched DXIL are
rejected. Legacy and compressed packages are not enabled in this prototype.
Raw DXIL, HLSL, and LLVM bitcode are not accepted; raw bitcode is rejected before
the shared runtime JIT path.

Loading validates the launch ABI, queries the selected D3D12 device's shader-model
support, and checks all declared feature requirements. The prototype currently
implements device queries for `Doubles`, `WaveOps`, `Int64Ops`,
`NativeLowPrecision`, and `AtomicInt64OnGroupShared`. Other requirements produce
`UNSUPPORTED` rather than being assumed available. Optional feature queries are
made only when required. An unrecognized query (`E_INVALIDARG`) means the
requirement is unsupported; other failed driver queries remain backend errors.
Overdeclared features must also pass these checks.

`olIsValidBinary` uses the same package, reflection, and device checks, reporting
false for invalid or unsupported input. A true result does not guarantee that
subsequent native root-signature or pipeline creation will succeed.

After validation, loading serializes the canonical root layout using
`D3D12SerializeRootSignature` from the already-loaded D3D12 runtime, then creates
an `ID3D12RootSignature` on the selected device. No DXC library is required.
Serialization and native creation failures are backend errors; no program is
published on failure. `olIsValidBinary` does not create native root objects and
does not guarantee that subsequent native resource creation will succeed.

Each successful program owns a copy of the executable DXIL bytes, its decoded
manifest, a native device reference, and its root signature until unload.
Program image-base and image-size queries describe those DXIL bytes, not the
surrounding package.
The caller may release the input package after successful creation. Programs are
independent even when created from identical packages.
`olDestroyProgram` releases the image, its kernel/pipeline objects, and removes it
from device shutdown cleanup. Device globals remain explicitly unsupported.
DXIL global constructors/destructors remain rejected during validation.
Static `groupshared` storage supports numeric scalars (`i1`, 16/32/64-bit integers,
half, float, and double) and nested arrays of those scalars, as emitted by the
supported compiler lowering. Other shared-storage types are explicitly
unsupported. Storage is summed over executable DXIL address-space-3 globals
using the original DXIL scalar allocation sizes, not the replacement layout used
for bitcode decoding or the optional `STAT` module. Both Clang's external
shared-storage declarations and DXC's internal definitions are reflected.
Zero-length arrays and totals above the D3D12 32 KiB group limit are rejected
before loading. Shared storage is allocated by the shader pipeline; no launch
argument or root binding is needed. Group synchronization and shared-memory
barriers execute as compiled DXIL instructions.

## Kernel lookup

`olGetSymbol` accepts the manifest's exact entry name for kernel symbols; other
names return `NOT_FOUND`. The first successful lookup creates an
`ID3D12PipelineState` using the program's canonical root signature and owned DXIL.
Native creation failures are backend errors, and a failed lookup publishes no
kernel symbol. Repeated successful lookups return the same symbol within a
program; different programs own independent kernels and pipelines.

Kernel enumeration visits exactly that entry, creating its kernel on demand
through the same lookup path. Global lookup/enumeration remain `UNSUPPORTED`.
Kernel names, kinds, and metadata queries are available. Maximum threads is the
product of the compiled `numthreads` dimensions, not permission to reshape the
fixed thread group. Static shared-memory queries report the reflected byte count,
including zero for shaders without shared storage. Occupancy calculations and cooperative launch queries remain
`UNSUPPORTED`.
Kernel symbols are borrowed from their program and must not be used after its
destruction.

## Dispatch and copy completion

`olLaunchKernel` accepts null queues and explicit compute queues. Explicit-queue
launches and buffer copies enqueue work without waiting for GPU completion.
Null-queue operations block until their work and host readbacks complete.
A null queue uses the device-owned synchronous queue, but arguments are still
resolved only in the program's originating
context. Explicit queues must belong to that same context. Programs, queues,
events, and allocations must be released before destroying their context.

Synchronize an explicit queue or a recorded event before observing readback
memory or freeing/reusing its allocations. A null-queue operation does not
implicitly wait for other queues. Use events to order access across queues;
unsynchronized access to the same resource from multiple queues is not supported.
Program handles may be destroyed after submission: submitted work owns its native
pipeline, root signature, descriptors, and bound resources independently.
Launch argument values are copied during submission; host copy destinations must
remain valid until completion.

Uploads snapshot pageable host memory into owned staging buffers. Preparing an
upload may block for earlier readbacks or event waits on that queue so the
snapshot observes their host writes. Queued host-to-host copies also drain the
queue before copying.

Host callbacks run on the retirement worker after earlier queue work completes.
Later GPU work waits for the callback to return, without preventing queue queries
or further submissions. Callbacks must not call liboffload or backend APIs.
Pattern fills snapshot arbitrary positive-sized patterns into owned upload
staging, validate the destination against the queue's context and allocation
bounds, and enqueue a buffer copy. The fill size must be a multiple of the
pattern size. These staging-based fills prioritize correctness over throughput.

Launch dimensions must be 1 through 3; inactive dimensions are normalized to 1.
The thread-group size must match the compiled `numthreads` in all three
dimensions. Each dispatch group count must be at most 65535; zero counts are
allowed and perform no work. Cooperative dispatch and nonzero dynamic shared
memory are unsupported.
Zero-group launches still validate arguments. They preserve blocking null-queue
semantics, but do not wait for explicit-queue work or record an empty dispatch.

Each launch prepares independent bindings and records the pipeline, root
signature, arguments, and `Dispatch`. Each distinct buffer resource transitions
from `COMMON` to `NON_PIXEL_SHADER_RESOURCE` for SRVs or `UNORDERED_ACCESS` for
UAVs, then back to `COMMON`. Duplicate bindings of the same access kind share one
transition. Simultaneous SRV and UAV bindings of the same physical resource are
`UNSUPPORTED`, even for disjoint views.

Submission records retain the pipeline, root signature, descriptor heap, bound
resources, command allocator, and command list until fence completion. Submitted
objects remain retained if signaling or waiting fails. Native failures are
reported as backend errors.

Each queue has a retirement worker and separate execution and completion fences.
The completion timeline advances only after execution and any host readback
action finish, so observing completion also makes readback bytes visible.
Completion failures poison the queue and are reported by subsequent operations
and synchronization rather than publishing successful completion.
Native Windows retirement uses fence completion events; host waiters are notified
when retirement advances. A 100 ms watchdog checks failures and device removal,
while portable emulators retain their short polling fallback. Each pending native
fence value is registered only once, including across watchdog wakeups.

Failure state records the last genuinely completed snapshot. Failed queues
release their GPU completion-fence waits so dependent queues can drain, but those
fence advances never count as successful API completion. Earlier completed events
remain usable. Pending submission ownership is retained through a native GPU
drain (or device removal), and host completion actions are not run for failed work.
If the driver cannot accept even a drain fence signal, safe teardown must wait
for device removal rather than release objects still potentially in use; the
plugin does not forcibly remove a shared native device.

## Events

`olCreateEvent` records a snapshot of an explicit queue's completion timeline.
`olSyncEvent` waits for that snapshot, while `OL_EVENT_INFO_IS_COMPLETE` queries
it without waiting. Work submitted after the snapshot does not delay the event.
Events keep their native queue state alive and remain usable after
`olDestroyQueue`.

`olWaitEvents` enqueues native GPU waits for events from other queues on the same
device and in the same context. It does not block the calling thread.
Queued waits retain their fence dependencies even if the event or originating
queue handle is destroyed. Cross-device and cross-context waits are explicitly
rejected.

Profiling-enabled events record D3D12 timestamp queries and retain their query
heaps and readback buffers through completion, independently of event-handle
lifetime. Synchronize both events before querying elapsed milliseconds;
incomplete or reversed intervals return `INVALID_ARGUMENT`. Same-queue intervals
use the native queue timestamp frequency. On Windows, cross-queue intervals use
`GetClockCalibration` to correlate each queue to QPC; other platforms explicitly
reject cross-queue timing. Calibration is approximate and can drift when hardware
stops its GPU timestamp clock while idle; it does not change GPU power policy.
Queue timestamp frequencies are not advertised as device-code clock frequencies.

## Argument binding preparation

The internal `DirectXArgumentBindingsTy` helper prepares arguments from a
validated manifest, creates a shader-visible descriptor heap, and records the
canonical root bindings on an open compute command list. It does not submit
commands, dispatch a shader, or perform resource transitions itself; the launch
path handles those operations.

Preparation requires one non-null host argument-value slot and exact size per
manifest argument. Buffer slots contain a host pointer-sized encoded device
pointer; scalar slots contain exactly four or eight little-endian bytes. Slot
addresses need not be naturally aligned. Scalar bits are copied without numeric
conversion, and constant-block padding is zero-filled. Failed preparation
preserves any previously prepared bindings.

The caller supplies a resolver restricted to the launch context's live
allocations; unknown, freed, and foreign-context pointers must fail rather than
fall back to another context. The resolver returns an owned native resource,
the byte offset from its physical base (including alignment displacement), and
the remaining **logical allocation** bytes. Preparation verifies resource bounds
and access flags before constructing raw views:

- The resource-relative offset must be 16-byte aligned, as required for raw
  SRV/UAV views. Misaligned offsets are rejected, not rounded.
- The remaining logical size must cover `min_size`. Only complete DWORDs are
  exposed; trailing partial DWORDs, pool capacity, and allocation padding are
  never included.
- The prototype limits each view to `2^27` DWORDs (512 MiB), using the SDK's
  portable buffer-view texel limit. Larger views return `UNSUPPORTED`; their
  bounds are not silently truncated.
- Views use `R32_TYPELESS`, `RAW` flags, zero structure stride, and no UAV counter.
  Descriptor order follows the manifest's canonical buffer-argument order.

Buffer bindings own their resource references and a per-binding-set
shader-visible CBV/SRV/UAV heap. Heap creation rejects resources from another
device. Scalar-only and empty kernels allocate no descriptor heap. Recording
uses table root 0 when buffers exist and constants root 1 (otherwise root 0).
The root signature must match the manifest, and the binding set and root
signature must remain alive and unchanged through submission completion.

## Shader validation

The internal `DirectXManifestTy::validateShader` helper validates a decoded
manifest against a DXBC container using LLVM's container and bitcode readers.
There is no DXC compiler/reflection library dependency. Validation checks:

- Container, part, and embedded bitcode bounds; executable compute DXIL, rather
  than a debug-only image or shader library.
- Agreement between the manifest, program header, and DXIL metadata on shader
  model, entry point, and thread dimensions.
- Exact SRV/UAV/cbuffer binding coverage. Raw, single-descriptor buffers are
  supported; resource arrays, typed/structured resources, textures, samplers,
  UAV counters, and rasterizer-ordered views are rejected.
- Constant block size and each scalar field's byte width and explicit HLSL
  offset, including padding between fields.
- SFI0 feature requirements covered by the manifest, and known DXIL module
  feature requirements covered by SFI0. Overdeclaring manifest features is
  permitted; omitting actual requirements is not.

Scalar shaders require a named `cbuffer` declaration with a matching named struct
annotation, retained either in executable DXIL or in `STAT` reflection data.
When both are present, both layouts are checked. Completely reflection-stripped
scalar shaders, non-scalar constant fields, and other constant-buffer annotation
forms are explicitly unsupported. Buffer-only shaders do not require `STAT`.
The C++ kernel ABI emits executable `dx.typeAnnotations` for scalar arguments;
the `cpp-scalar.cpp` fixture validates that path without `STAT`. The older HLSL
scalar-reflection fixture still uses DXC, because upstream Clang's HLSL path
does not emit the required reflection. Fixture-generation commands are documented
in the accompanying source; no compiler or DXC library is needed at runtime.
Legacy DXIL's data layout is overridden only for LLVM bitcode decoding; HLSL
offsets come from reflection, not LLVM's replacement layout.
Shared-memory reflection preserves the original scalar layout, including legacy
16-bit scalar padding. Legacy i8 over-alignment is normalized only to allow
modern LLVM to parse that layout; i8 shared-storage types remain unsupported.

Embedded root signatures and additional entry/resource properties are
conservatively rejected until their compatibility checks are implemented. The
helper does not perform DXIL validator/signature verification, establish device
support, or create native objects. Program loading separately checks device
capabilities before publishing an image. Native root-signature, pipeline, and
descriptor creation failures are surfaced separately.

## Version 1 schema

All fields shown below are required. Unknown fields are rejected, including
fields inappropriate for an argument's kind. Integer fields must be JSON
integers, not floating-point numbers or numeric strings.

```json
{
  "version": 1,
  "entry": "main",
  "shader_model": [6, 0],
  "numthreads": [64, 1, 1],
  "features": [],
  "constants": {"register": 0, "space": 0, "size": 4},
  "arguments": [
    {"index": 0, "kind": "srv", "register": 0, "space": 0, "min_size": 4},
    {"index": 1, "kind": "uav", "register": 0, "space": 0, "min_size": 4},
    {"index": 2, "kind": "scalar", "size": 4, "offset": 0}
  ]
}
```

This example maps arguments to `t0, space0`, `u0, space0`, and the first four
bytes of `b0, space0`, respectively. It describes a shader such as:

```hlsl
ByteAddressBuffer Input : register(t0);
RWByteAddressBuffer Output : register(u0);
cbuffer Parameters : register(b0) { uint Count; };

[numthreads(64, 1, 1)]
void main(uint3 ID : SV_DispatchThreadID) {
  if (ID.x < Count)
    Output.Store(4 * ID.x, Input.Load(4 * ID.x));
}
```

### Shader declaration

- `version`: exactly `1`; other versions produce `UNSUPPORTED`.
- `entry`: a nonempty ASCII HLSL identifier, also used as the liboffload kernel
  symbol name. There are no separate aliases or library exports in version 1.
- `shader_model`: `[major, minor]`. The representation accepts major `6` and
  minor `0` through `15`; this is not a claim that the compiler or device supports
  every represented version. Actual image/device compatibility is checked on load.
- `numthreads`: exactly three positive integers. X and Y are at most 1024, Z is
  at most 64, and their product is at most 1024. The launch group size must
  match these dimensions and the reflected shader dimensions exactly;
  dispatch group counts remain runtime values.
- `features`: required shader features, using the non-reserved LLVM DXContainer
  `FeatureFlags` names, such as `Doubles`, `WaveOps`, `Int64Ops`, or
  `NativeLowPrecision`. Duplicate names are invalid. Unknown names, `RESERVED`,
  and `NextUnusedBit` produce `UNSUPPORTED`. Decoding records the corresponding
  DXIL SFI0 bit mask; it does not establish support on a device.

Reflection must validate the entry point, compute stage, shader model, thread
dimensions, feature requirements, and bindings rather than infer an argument ABI.
An understated feature requirement must not bypass capability checks.

### Arguments and bindings

Argument indices must be unique and dense from zero. The JSON array may be in
any order; decoding canonicalizes it into index order.

`srv` and `uav` describe raw buffer views, not typed or structured buffers,
textures, samplers, or arbitrary shader pointers:

- `register` and `space` identify `t` or `u` bindings. Registers are unsigned
  32-bit integers other than `UINT32_MAX`. Register spaces from `0xfffffff0`
  through `0xffffffff` are reserved and rejected.
- Bindings must be unique within each register namespace and space. `t0` and
  `u0` are distinct; `t0, space0` and `t0, space1` are also distinct.
- `min_size` is a positive, four-byte-aligned minimum accessible byte count.
- At launch, the argument value is a host pointer-sized slot containing an
  encoded device pointer returned by this plugin, possibly with an
  allocation-relative offset. It is not a host-dereferenceable HLSL pointer.
- Launch validation must resolve that pointer in the queue's context, check the
  remaining live allocation size against `min_size`, and enforce raw-view
  alignment requirements. Descriptor bounds must exclude pool capacity and
  alignment padding; any trailing partial DWORD is not exposed.

`scalar` describes a four- or eight-byte scalar value copied without conversion
from the host argument into the root constant block:

- `size` is exactly `4` or `8`.
- `offset` is a byte offset aligned to `size`.
- Scalar ranges must not overlap and must fit entirely inside the constant block.
- The launch argument size must match `size`. The layout uses little-endian
  scalar representations and must agree with the reflected HLSL constant layout.
  Pointer-bearing aggregates and implicit host/HLSL layout conversion are not
  supported.

`constants` is `null` exactly when there are no scalar arguments. Otherwise it
specifies the `b` register, register space, and total block `size`. Its register
and space obey the same numeric constraints as buffer bindings. Block size is a
positive multiple of four, at most **252 bytes**. Padding between scalar ranges
is zero-filled at launch.

### Canonical root layout

The version 1 contract uses a root-signature 1.0 layout with no static samplers,
no root-signature flags, and `ALL` visibility:

1. If buffer arguments exist, root parameter 0 is one SRV/UAV descriptor table.
   Each buffer contributes one single-descriptor range, in argument-index order,
   with consecutive table offsets and its explicit register and space. A heap
   has at most 1,000,000 descriptors; device-specific resource limits still apply.
2. If scalar arguments exist, the next root parameter is a block of 32-bit root
   constants, with the manifest's `b` register, space, and `size / 4` DWORD count.

One table DWORD plus up to 63 constant DWORDs fits the 64-DWORD D3D12 root budget.
Version 1 retains the 252-byte limit even for scalar-only kernels. If both kinds
of argument are absent, there are no root parameters.
The loader creates this layout even for shaders without arguments. Native
root-creation failures, including device-specific layout limits, are surfaced as
backend errors. Each launch creates its descriptor heap/views and records the
canonical argument bindings.
The validator conservatively rejects all embedded root signatures, including
otherwise compatible layouts; compatibility checking is deferred.

## Packaging

The existing C++ OffloadBinary writer can carry the JSON verbatim:

```cpp
llvm::object::OffloadBinary::OffloadingImage Image;
Image.TheImageKind = llvm::object::IMG_DXIL;
Image.StringData["triple"] = "dxil-pc-shadermodel6.0-compute";
Image.StringData["directx.manifest"] = ManifestJSON;
Image.Image = std::move(DXILBuffer);
auto Package = llvm::object::OffloadBinary::write(
    llvm::ArrayRef(&Image, 1));
```

Keep the strings backing `ManifestJSON` alive through `write`. Do not pass a path
in place of JSON. The existing comma-separated `llvm-offload-binary --image`
syntax does not provide JSON-file metadata import; a producer convenience tool
is not part of this slice.

Malformed fields, conflicting bindings, invalid constant layouts, and malformed
entry contracts produce `INVALID_BINARY`. Unsupported versions, shader-model
representations, argument kinds, and feature names produce `UNSUPPORTED`.
