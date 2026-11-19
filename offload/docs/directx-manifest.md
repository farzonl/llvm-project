# Experimental DirectX launch manifest

The DirectX prototype uses a versioned JSON launch manifest stored under the
`directx.manifest` string-data key of an LLVM OffloadBinary entry. Each entry
contains one executable compute DXIL shader and the manifest for its one entry
point. Multiple entries keep independent payload/manifest associations.
The image kind is `IMG_DXIL`, the producer kind is `OFK_None`, and entry flags
are zero. The DXIL payload must be nonempty.

Manifest decoding and device-independent DXIL/manifest validation are implemented.
This does not enable program loading or execution. Device-capability checks,
root-signature creation, and launch-time argument validation are subsequent
implementation steps. The prototype ABI is experimental.

## Shader validation

The internal `DirectXManifestTy::validateShader` helper validates a decoded
manifest against a DXBC container using LLVM's container and bitcode readers.
There is no DXC library dependency or runtime compilation. Validation checks:

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
support, create native objects, or make `olCreateProgram` available.

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
Incompatible embedded root signatures must be rejected when loading is enabled.
The current validator conservatively rejects all embedded root signatures.

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
