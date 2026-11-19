# Experimental DirectX launch manifest

The DirectX prototype uses a versioned JSON launch manifest stored under the
`directx.manifest` string-data key of an LLVM OffloadBinary entry. Each entry
contains one executable compute DXIL shader and the manifest for its one entry
point. Multiple entries keep independent payload/manifest associations.
The image kind is `IMG_DXIL`, the producer kind is `OFK_None`, and entry flags
are zero. The DXIL payload must be nonempty.

Manifest decoding, LLVM-native DXIL/manifest validation, and single-entry program
loading with device-capability checks, canonical native root-signature creation,
and kernel objects with compute pipelines are implemented. Argument binding and
execution remain unsupported. The prototype ABI is experimental.

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
subsequent native root-signature or pipeline creation will succeed, nor does it
establish dispatch support.

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
from device shutdown cleanup. Device globals and execution remain explicitly
unsupported. DXIL global constructors/destructors and `groupshared` storage are
rejected during validation. Static shared-memory reflection is not implemented;
rejecting such storage avoids reporting an incorrect zero shared-memory size.

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
fixed thread group; static shared memory is zero in the currently supported
subset. Occupancy calculations, cooperative launch queries, argument binding,
and dispatch remain `UNSUPPORTED`, including dispatch with an explicit queue.
Kernel symbols are borrowed from their program and must not be used after its
destruction.

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

Scalar shaders currently require retained `STAT` reflection data and a named
`cbuffer` declaration with a matching named struct annotation. Reflection-stripped
scalar shaders, non-scalar constant fields, and other constant-buffer annotation
forms are explicitly unsupported. Buffer-only shaders do not require `STAT`.
Legacy DXIL's data layout is overridden only for LLVM bitcode decoding; HLSL
offsets come from reflection, not LLVM's replacement layout.

Embedded root signatures and additional entry/resource properties are
conservatively rejected until their compatibility checks are implemented. The
helper does not perform DXIL validator/signature verification, establish device
support, or create native objects. Program loading separately checks device
capabilities before publishing an image. Pipeline/root-signature and descriptor
limits will be checked when those native objects are implemented.

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
  eventually match these dimensions and the reflected shader dimensions exactly;
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
backend errors. Descriptor heaps and views are not allocated yet, and argument
binding remains unsupported.
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
