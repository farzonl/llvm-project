//===- DirectXShader.cpp - DirectX shader ABI validation
//--------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "DirectXManifest.h"
#include "OffloadError.h"

#include "llvm/BinaryFormat/DXContainer.h"
#include "llvm/Bitcode/BitcodeReader.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"
#include "llvm/Object/DXContainer.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/MemoryBufferRef.h"

#include <cstring>
#include <set>
#include <tuple>

namespace llvm::omp::target::plugin::directx {
namespace {

Error invalid(const Twine &Message) {
  return make_error<error::OffloadError>(
      error::ErrorCode::INVALID_BINARY,
      ("invalid DirectX shader: " + Message).str());
}

Error unsupported(const Twine &Message) {
  return make_error<error::OffloadError>(
      error::ErrorCode::UNSUPPORTED,
      ("unsupported DirectX shader: " + Message).str());
}

Expected<uint64_t> integer(const Metadata *MD, unsigned Bits = 32) {
  auto *Value = mdconst::dyn_extract_or_null<ConstantInt>(MD);
  if (!Value || Value->getBitWidth() != Bits)
    return invalid("expected i" + Twine(Bits) + " metadata");
  return Value->getZExtValue();
}

Expected<const MDNode *> namedNode(const Module &M, StringRef Name,
                                   unsigned Operands) {
  auto *Named = M.getNamedMetadata(Name);
  if (!Named || Named->getNumOperands() != 1 ||
      Named->getOperand(0)->getNumOperands() != Operands)
    return invalid("missing or malformed " + Name);
  return Named->getOperand(0);
}

Expected<StringRef> programBitcode(StringRef Part) {
  if (Part.size() < sizeof(dxbc::ProgramHeader))
    return invalid("truncated program header");
  dxbc::ProgramHeader Header;
  memcpy(&Header, Part.data(), sizeof(Header));
  if (sys::IsBigEndianHost)
    Header.swapBytes();
  uint64_t Offset =
      offsetof(dxbc::ProgramHeader, Bitcode) + uint64_t(Header.Bitcode.Offset);
  if (memcmp(Header.Bitcode.Magic, "DXIL", 4) || Header.Unused ||
      Header.Bitcode.Unused || uint64_t(Header.Size) * 4 != Part.size() ||
      Header.Bitcode.Offset < sizeof(dxbc::BitcodeHeader) ||
      Offset > Part.size() || !Header.Bitcode.Size ||
      Header.Bitcode.Size > Part.size() - Offset)
    return invalid("invalid program bitcode bounds or header");
  if (Header.ShaderKind != Triple::Compute - Triple::Pixel)
    return unsupported("only executable compute shaders are supported");
  return Part.substr(Offset, Header.Bitcode.Size);
}

// Check the bounds that DXContainer's view-based accessors assume, including
// program-relative bitcode offsets, before asking them to interpret the image.
Expected<StringRef> checkContainer(StringRef Bytes) {
  if (Bytes.size() < sizeof(dxbc::Header) || !Bytes.starts_with("DXBC"))
    return invalid("missing DXBC container header");
  dxbc::Header Header;
  memcpy(&Header, Bytes.data(), sizeof(Header));
  if (sys::IsBigEndianHost)
    Header.swapBytes();
  if (Header.FileSize != Bytes.size() || Header.Version.Major != 1 ||
      Header.Version.Minor || !Header.PartCount ||
      Header.PartCount > (Bytes.size() - sizeof(Header)) / sizeof(uint32_t))
    return invalid("invalid container size, version, or part count");

  StringRef Reflection;
  bool HasExecutable = false, HasFeatures = false;
  size_t End = sizeof(Header) + size_t(Header.PartCount) * sizeof(uint32_t);
  for (uint32_t I = 0; I < Header.PartCount; ++I) {
    uint32_t Offset = support::endian::read32le(Bytes.data() + sizeof(Header) +
                                                I * sizeof(uint32_t));
    if (Offset < End || Offset > Bytes.size() ||
        Bytes.size() - Offset < sizeof(dxbc::PartHeader))
      return invalid("overlapping or truncated container part");
    StringRef Name = Bytes.substr(Offset, 4);
    HasExecutable |= Name == "DXIL";
    HasFeatures |= Name == "SFI0";
    uint32_t Size = support::endian::read32le(Bytes.data() + Offset + 4);
    size_t Start = size_t(Offset) + sizeof(dxbc::PartHeader);
    if (Size > Bytes.size() - Start)
      return invalid("container part extends beyond image");
    End = Start + Size;
    if (Name == "RTS0")
      return unsupported("embedded root signatures are not enabled yet");
    if (Name == "DXIL" || Name == "STAT" || Name == "ILDB") {
      auto Bitcode = programBitcode(Bytes.substr(Start, Size));
      if (!Bitcode)
        return Bitcode.takeError();
      if (Name == "STAT") {
        if (!Reflection.empty())
          return invalid("duplicate STAT reflection part");
        Reflection = *Bitcode;
      }
    }
    if (Name == "SFI0" && Size != sizeof(uint64_t))
      return invalid("invalid SFI0 feature mask size");
  }
  if (!HasExecutable || !HasFeatures)
    return invalid("executable DXIL and SFI0 parts are required");
  return Reflection;
}

Expected<std::unique_ptr<Module>> readModule(StringRef Bitcode,
                                             LLVMContext &Context) {
  ParserCallbacks Callbacks;
  // Legacy DXIL uses alignments rejected by modern LLVM. Reflection uses
  // explicit HLSL field offsets and widths, never LLVM's replacement layout.
  Callbacks.DataLayout =
      [](StringRef, StringRef) -> std::optional<std::string> { return "e"; };
  auto M = parseBitcodeFile(MemoryBufferRef(Bitcode, "DirectX shader"), Context,
                            Callbacks);
  if (!M)
    return invalid("cannot read bitcode: " + toString(M.takeError()));
  return std::move(*M);
}

constexpr uint64_t moduleBit(int Bit) {
  return Bit < 0 ? 0 : uint64_t(1) << Bit;
}

Expected<uint64_t> featureFlags(uint64_t ModuleFlags) {
  uint64_t Known = 0, Features = 0;
#define SHADER_FEATURE_FLAG(Bit, ModuleBit, Name, Description)                 \
  Known |= moduleBit(ModuleBit);                                               \
  if (ModuleFlags & moduleBit(ModuleBit))                                      \
    Features |= uint64_t(dxbc::FeatureFlags::Name);
#define DXIL_MODULE_FLAG(Bit, Name, Description) Known |= moduleBit(Bit);
#include "llvm/BinaryFormat/DXContainerConstants.def"
  if (ModuleFlags & ~Known)
    return unsupported("unknown DXIL module flags");
  if (ModuleFlags & moduleBit(5))
    Features |= uint64_t(ModuleFlags & moduleBit(23)
                             ? dxbc::FeatureFlags::NativeLowPrecision
                             : dxbc::FeatureFlags::MinimumPrecision);
  return Features;
}

Error checkModule(const Module &M, const DirectXManifestTy &Manifest,
                  uint64_t ContainerFeatures, bool Reflection) {
  auto Model = namedNode(M, "dx.shaderModel", 3);
  if (!Model)
    return Model.takeError();
  auto *Stage = dyn_cast_or_null<MDString>((*Model)->getOperand(0));
  auto Major = integer((*Model)->getOperand(1));
  auto Minor = integer((*Model)->getOperand(2));
  if (!Major)
    return Major.takeError();
  if (!Minor)
    return Minor.takeError();
  if (!Stage || Stage->getString() != "cs")
    return unsupported("DXIL metadata must describe a compute shader");
  if (*Major != Manifest.ShaderModel[0] || *Minor != Manifest.ShaderModel[1])
    return invalid("shader model does not match manifest");

  auto Entry = namedNode(M, "dx.entryPoints", 5);
  if (!Entry)
    return Entry.takeError();
  auto *Name = dyn_cast_or_null<MDString>((*Entry)->getOperand(1));
  auto *Value = dyn_cast_or_null<ValueAsMetadata>((*Entry)->getOperand(0));
  auto *Function =
      Value ? dyn_cast<llvm::Function>(Value->getValue()) : nullptr;
  if (!Name || Name->getString() != Manifest.Entry || !Function ||
      Function->getName() != Manifest.Entry ||
      (!Reflection && Function->isDeclaration()) ||
      !Function->getReturnType()->isVoidTy() || !Function->arg_empty())
    return invalid("entry point does not match manifest or is not executable");
  if (const auto *Signature =
          dyn_cast_or_null<MDNode>((*Entry)->getOperand(2))) {
    for (const auto &Operand : Signature->operands())
      if (Operand)
        return unsupported("entry input/output signatures");
  } else if ((*Entry)->getOperand(2))
    return invalid("malformed entry signature");

  auto *Properties = dyn_cast_or_null<MDNode>((*Entry)->getOperand(4));
  if (!Properties || Properties->getNumOperands() % 2)
    return invalid("missing or malformed entry properties");
  bool ThreadsFound = false, FlagsFound = false;
  for (unsigned I = 0; I < Properties->getNumOperands(); I += 2) {
    auto Tag = integer(Properties->getOperand(I));
    if (!Tag)
      return Tag.takeError();
    const Metadata *Data = Properties->getOperand(I + 1);
    if (*Tag == 0) {
      if (FlagsFound)
        return invalid("duplicate shader flags");
      FlagsFound = true;
      auto Flags = integer(Data, 64);
      if (!Flags)
        return Flags.takeError();
      auto Features = featureFlags(*Flags);
      if (!Features)
        return Features.takeError();
      if (*Features & ~ContainerFeatures)
        return invalid("SFI0 understates DXIL feature requirements");
    } else if (*Tag == 4) {
      if (ThreadsFound)
        return invalid("duplicate numthreads property");
      ThreadsFound = true;
      auto *Threads = dyn_cast_or_null<MDNode>(Data);
      if (!Threads || Threads->getNumOperands() != 3)
        return invalid("malformed numthreads property");
      for (unsigned J = 0; J < 3; ++J) {
        auto Dimension = integer(Threads->getOperand(J));
        if (!Dimension)
          return Dimension.takeError();
        if (*Dimension != Manifest.NumThreads[J])
          return invalid("numthreads does not match manifest");
      }
    } else
      return unsupported("entry property " + Twine(*Tag));
  }
  if (!ThreadsFound)
    return invalid("missing numthreads property");
  return Error::success();
}

Expected<StringRef> checkResources(const Module &M,
                                   const DirectXManifestTy &Manifest) {
  using Binding = std::tuple<unsigned, uint32_t, uint32_t>;
  std::set<Binding> Remaining, Seen;
  for (const auto &Arg : Manifest.Arguments)
    if (auto *Buffer = std::get_if<DirectXBufferBindingTy>(&Arg.Binding))
      Remaining.emplace(Buffer->Access == DirectXBufferBindingTy::Kind::UAV,
                        Buffer->Space, Buffer->Register);
  if (Manifest.Constants)
    Remaining.emplace(2, Manifest.Constants->Space,
                      Manifest.Constants->Register);

  auto Entry = namedNode(M, "dx.entryPoints", 5);
  if (!Entry)
    return Entry.takeError();
  auto *Root = dyn_cast_or_null<MDNode>((*Entry)->getOperand(3));
  if (auto *Global = M.getNamedMetadata("dx.resources")) {
    if (Global->getNumOperands() != 1 || Global->getOperand(0) != Root)
      return invalid("entry and module resource lists disagree");
  }
  if (!Root) {
    if (!Remaining.empty())
      return invalid("missing resource bindings");
    return StringRef();
  }
  if (Root->getNumOperands() != 4)
    return invalid("malformed resource list");
  StringRef ConstantName;
  for (unsigned Class = 0; Class < 4; ++Class) {
    const Metadata *ListMD = Root->getOperand(Class);
    if (!ListMD)
      continue;
    auto *List = dyn_cast<MDNode>(ListMD);
    if (!List)
      return invalid("malformed resource class");
    for (const auto &Operand : List->operands()) {
      auto *Resource = dyn_cast_or_null<MDNode>(Operand);
      unsigned ExpectedSize = Class == 0 ? 9 : Class == 1 ? 11 : 8;
      if (Class == 3)
        return unsupported("samplers");
      if (!Resource || Resource->getNumOperands() != ExpectedSize)
        return invalid("malformed resource binding");
      auto Space = integer(Resource->getOperand(3));
      auto Register = integer(Resource->getOperand(4));
      auto Count = integer(Resource->getOperand(5));
      auto KindOrSize = integer(Resource->getOperand(6));
      if (!Space)
        return Space.takeError();
      if (!Register)
        return Register.takeError();
      if (!Count)
        return Count.takeError();
      if (!KindOrSize)
        return KindOrSize.takeError();
      if (*Count != 1)
        return unsupported("resource arrays and unbounded bindings");
      if (Class < 2 && *KindOrSize != uint64_t(dxil::ResourceKind::RawBuffer))
        return unsupported("only raw SRV/UAV buffers are supported");
      Binding Key(Class, *Space, *Register);
      if (!Seen.insert(Key).second || !Remaining.erase(Key))
        return invalid("resource binding does not match manifest");
      if (Resource->getOperand(ExpectedSize - 1))
        return unsupported("extended resource properties");
      if (Class == 1) {
        auto Counter = integer(Resource->getOperand(8), 1);
        auto ROV = integer(Resource->getOperand(9), 1);
        if (!Counter)
          return Counter.takeError();
        if (!ROV)
          return ROV.takeError();
        if (*Counter || *ROV)
          return unsupported("UAV counters and rasterizer-ordered views");
      }
      if (Class == 2) {
        if (*KindOrSize != Manifest.Constants->Size)
          return invalid("constant block size does not match manifest");
        auto *Name = dyn_cast_or_null<MDString>(Resource->getOperand(2));
        if (!Name)
          return invalid("malformed constant buffer name");
        ConstantName = Name->getString();
      }
    }
  }
  if (!Remaining.empty())
    return invalid("manifest contains unbound resources");
  return ConstantName;
}

Error checkConstants(const Module &M, StringRef ConstantName,
                     const DirectXManifestTy &Manifest) {
  auto *Annotations = M.getNamedMetadata("dx.typeAnnotations");
  if (!Annotations || ConstantName.empty())
    return unsupported("scalar arguments require retained cbuffer reflection");
  const MDNode *Layout = nullptr;
  StructType *Type = nullptr;
  for (const MDNode *Annotation : Annotations->operands()) {
    if (!Annotation->getNumOperands())
      return invalid("empty type annotation");
    auto Tag = integer(Annotation->getOperand(0));
    if (!Tag)
      return Tag.takeError();
    if (*Tag != 0)
      continue;
    if (Annotation->getNumOperands() % 2 != 1)
      return invalid("malformed struct annotations");
    for (unsigned I = 1; I < Annotation->getNumOperands(); I += 2) {
      auto *Value =
          dyn_cast_or_null<ValueAsMetadata>(Annotation->getOperand(I));
      auto *Struct =
          Value ? dyn_cast<StructType>(Value->getValue()->getType()) : nullptr;
      if (!Struct || Struct->getName() != ConstantName)
        continue;
      if (Type)
        return invalid("duplicate cbuffer type annotation");
      Type = Struct;
      Layout = dyn_cast_or_null<MDNode>(Annotation->getOperand(I + 1));
    }
  }
  if (!Type || !Layout || Type->isOpaque())
    return unsupported("a named, annotated cbuffer declaration is required");
  if (Layout->getNumOperands() != Type->getNumElements() + 1)
    return invalid("constant field annotations do not match type");
  auto Size = integer(Layout->getOperand(0));
  if (!Size)
    return Size.takeError();
  if (*Size != Manifest.Constants->Size)
    return invalid("reflected constant size does not match manifest");
  std::set<std::pair<uint32_t, uint32_t>> Remaining;
  for (const auto &Arg : Manifest.Arguments)
    if (auto *Scalar = std::get_if<DirectXScalarBindingTy>(&Arg.Binding))
      Remaining.emplace(Scalar->Size, Scalar->Offset);
  for (unsigned I = 0; I < Type->getNumElements(); ++I) {
    auto *Field = dyn_cast_or_null<MDNode>(Layout->getOperand(I + 1));
    if (!Field || Field->getNumOperands() % 2)
      return invalid("malformed constant field annotation");
    auto *FieldType = Type->getElementType(I);
    uint32_t Width = FieldType->isIntegerTy(32) || FieldType->isFloatTy() ? 4
                     : FieldType->isIntegerTy(64) || FieldType->isDoubleTy()
                         ? 8
                         : 0;
    if (!Width)
      return unsupported("non-scalar constant fields");
    std::optional<uint32_t> Offset;
    for (unsigned J = 0; J < Field->getNumOperands(); J += 2) {
      auto Tag = integer(Field->getOperand(J));
      if (!Tag)
        return Tag.takeError();
      if (*Tag == 3) {
        if (Offset)
          return invalid("duplicate constant field offset");
        auto Value = integer(Field->getOperand(J + 1));
        if (!Value)
          return Value.takeError();
        Offset = *Value;
      } else if (*Tag != 6 && *Tag != 7 && *Tag != 9)
        return unsupported("constant field annotation " + Twine(*Tag));
    }
    if (!Offset || !Remaining.erase({Width, *Offset}))
      return invalid("constant field layout does not match manifest");
  }
  if (!Remaining.empty())
    return invalid("manifest contains unbound scalar arguments");
  return Error::success();
}

} // namespace

Error DirectXManifestTy::validateShader(MemoryBufferRef Shader) const {
  auto Reflection = checkContainer(Shader.getBuffer());
  if (!Reflection)
    return Reflection.takeError();
  auto Container = object::DXContainer::create(Shader);
  if (!Container)
    return invalid(toString(Container.takeError()));
  auto Program = Container->getDXIL(false);
  auto Features = Container->getShaderFeatureFlags();
  if (!Program || !Features)
    return invalid("executable DXIL and SFI0 parts are required");
  auto Header = Program->first;
  if (Header.getMajorVersion() != ShaderModel[0] ||
      Header.getMinorVersion() != ShaderModel[1])
    return invalid("program header shader model does not match manifest");
  uint64_t KnownFeatures = uint64_t(dxbc::FeatureFlags::NextUnusedBit) - 1;
  KnownFeatures &= ~uint64_t(dxbc::FeatureFlags::RESERVED);
  if (*Features & ~KnownFeatures)
    return unsupported("unknown or reserved SFI0 feature flags");
  if (*Features & ~this->Features)
    return invalid("manifest understates shader feature requirements");

  LLVMContext Context;
  auto M = readModule(StringRef(Program->second, Header.Bitcode.Size), Context);
  if (!M)
    return M.takeError();
  if (auto Err = checkModule(**M, *this, *Features, false))
    return Err;
  auto Constant = checkResources(**M, *this);
  if (!Constant)
    return Constant.takeError();
  if (!Constants)
    return Error::success();
  if (Reflection->empty())
    return unsupported("scalar arguments require a STAT reflection part");
  LLVMContext ReflectionContext;
  auto RM = readModule(*Reflection, ReflectionContext);
  if (!RM)
    return RM.takeError();
  if (auto Err = checkModule(**RM, *this, *Features, true))
    return Err;
  auto Name = checkResources(**RM, *this);
  if (!Name)
    return Name.takeError();
  return checkConstants(**RM, *Name, *this);
}

} // namespace llvm::omp::target::plugin::directx
