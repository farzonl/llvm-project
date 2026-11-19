//===- DirectXManifest.cpp - DirectX launch ABI
//-----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "DirectXManifest.h"
#include "OffloadError.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/BinaryFormat/DXContainer.h"
#include "llvm/Object/OffloadBinary.h"
#include "llvm/Support/JSON.h"

#include <limits>
#include <set>
#include <tuple>
#include <utility>

namespace llvm::omp::target::plugin::directx {
namespace {

Error invalid(const Twine &Message) {
  return make_error<error::OffloadError>(
      error::ErrorCode::INVALID_BINARY,
      ("invalid DirectX manifest: " + Message).str());
}

Error checkKeys(const json::Object &Object, ArrayRef<StringRef> Keys,
                const Twine &Where) {
  for (const auto &[Key, Value] : Object)
    if (!llvm::is_contained(Keys, StringRef(Key)))
      return invalid(Where + ": unknown field '" + StringRef(Key) + "'");
  return Error::success();
}

Expected<uint32_t> readU32(const json::Value *Value, const Twine &Where) {
  auto Number = Value ? Value->getAsUINT64() : std::nullopt;
  if (!Number || *Number > std::numeric_limits<uint32_t>::max())
    return invalid(Where + " must be an unsigned 32-bit integer");
  return static_cast<uint32_t>(*Number);
}

template <size_t N>
Error readTuple(const json::Object &Object, StringRef Key,
                std::array<uint32_t, N> &Result) {
  auto *Array = Object.getArray(Key);
  if (!Array || Array->size() != N)
    return invalid(Key + " must contain exactly " + Twine(N) + " integers");
  for (size_t I = 0; I < N; ++I) {
    auto Number = readU32(&(*Array)[I], Key + "[" + Twine(I) + "]");
    if (!Number)
      return Number.takeError();
    Result[I] = *Number;
  }
  return Error::success();
}

Error readRegister(const json::Object &Object, uint32_t &Register,
                   uint32_t &Space, const Twine &Where) {
  auto R = readU32(Object.get("register"), Where + ".register");
  if (!R)
    return R.takeError();
  auto S = readU32(Object.get("space"), Where + ".space");
  if (!S)
    return S.takeError();
  if (*R == std::numeric_limits<uint32_t>::max())
    return invalid(Where + ".register cannot be UINT32_MAX");
  if (*S >= 0xfffffff0)
    return invalid(Where + ".space is reserved for the D3D12 system");
  Register = *R;
  Space = *S;
  return Error::success();
}

Expected<uint64_t> readFeatures(const json::Object &Object) {
  auto *Features = Object.getArray("features");
  if (!Features)
    return invalid("features must be an array");
  uint64_t Flags = 0;
  for (const auto &Feature : *Features) {
    auto Name = Feature.getAsString();
    if (!Name)
      return invalid("features must contain strings");
    uint64_t Flag = StringSwitch<uint64_t>(*Name)
#define SHADER_FEATURE_FLAG(Num, ModuleNum, Name, Description)                 \
  .Case(#Name, static_cast<uint64_t>(dxbc::FeatureFlags::Name))
#include "llvm/BinaryFormat/DXContainerConstants.def"
                        .Default(0);
    if (!Flag || *Name == "RESERVED" || *Name == "NextUnusedBit")
      return error::createOffloadError(
          error::ErrorCode::UNSUPPORTED,
          "unsupported DirectX manifest feature '%s'", Name->str().c_str());
    if (Flags & Flag)
      return invalid("duplicate feature '" + *Name + "'");
    Flags |= Flag;
  }
  return Flags;
}

} // namespace

Expected<DirectXManifestTy> DirectXManifestTy::parse(StringRef JSON) {
  auto Value = json::parse(JSON);
  if (!Value)
    return invalid(toString(Value.takeError()));
  const auto *Object = Value->getAsObject();
  if (!Object)
    return invalid("root must be an object");
  auto ABI = readU32(Object->get("version"), "version");
  if (!ABI)
    return ABI.takeError();
  if (*ABI != Version)
    return error::createOffloadError(error::ErrorCode::UNSUPPORTED,
                                     "unsupported DirectX manifest version %u",
                                     *ABI);
  if (auto Err = checkKeys(*Object,
                           {"version", "entry", "shader_model", "numthreads",
                            "features", "constants", "arguments"},
                           "root"))
    return std::move(Err);

  DirectXManifestTy Manifest;
  auto Entry = Object->getString("entry");
  if (!Entry || Entry->empty() ||
      !(isAlpha(Entry->front()) || Entry->front() == '_') ||
      !llvm::all_of(*Entry, [](char C) { return isAlnum(C) || C == '_'; }))
    return invalid("entry must be a nonempty ASCII HLSL identifier");
  Manifest.Entry = Entry->str();
  if (auto Err = readTuple(*Object, "shader_model", Manifest.ShaderModel))
    return std::move(Err);
  if (Manifest.ShaderModel[0] != 6 || Manifest.ShaderModel[1] > 15)
    return error::createOffloadError(error::ErrorCode::UNSUPPORTED,
                                     "DirectX manifest requires a shader model "
                                     "in the 6.0 through 6.15 range");
  if (auto Err = readTuple(*Object, "numthreads", Manifest.NumThreads))
    return std::move(Err);
  auto [X, Y, Z] = Manifest.NumThreads;
  if (!X || !Y || !Z || X > 1024 || Y > 1024 || Z > 64 ||
      uint64_t(X) * Y * Z > 1024)
    return invalid("numthreads exceeds compute thread-group limits");
  auto Features = readFeatures(*Object);
  if (!Features)
    return Features.takeError();
  Manifest.Features = *Features;

  const auto *Constants = Object->get("constants");
  if (!Constants)
    return invalid("constants must be an object or null");
  if (!Constants->getAsNull()) {
    const auto *Block = Constants->getAsObject();
    if (!Block)
      return invalid("constants must be an object or null");
    if (auto Err =
            checkKeys(*Block, {"register", "space", "size"}, "constants"))
      return std::move(Err);
    DirectXConstantBindingTy Binding;
    if (auto Err =
            readRegister(*Block, Binding.Register, Binding.Space, "constants"))
      return std::move(Err);
    auto Size = readU32(Block->get("size"), "constants.size");
    if (!Size)
      return Size.takeError();
    if (!*Size || *Size % 4 || *Size > MaxConstantBytes)
      return invalid(
          "constants.size must be a multiple of 4 from 4 through 252");
    Binding.Size = *Size;
    Manifest.Constants = Binding;
  }

  const auto *Arguments = Object->getArray("arguments");
  if (!Arguments)
    return invalid("arguments must be an array");
  // One descriptor heap may have at most 1,000,000 entries, plus 63 scalars.
  if (Arguments->size() > 1000063)
    return invalid("too many arguments for the version 1 root layout");
  std::set<std::tuple<DirectXBufferBindingTy::Kind, uint32_t, uint32_t>>
      Bindings;
  SmallVector<std::pair<uint32_t, uint32_t>> ScalarRanges;
  uint32_t BufferCount = 0;
  for (size_t I = 0; I < Arguments->size(); ++I) {
    std::string Where = ("arguments[" + Twine(I) + "]").str();
    const auto *Argument = (*Arguments)[I].getAsObject();
    if (!Argument)
      return invalid(Where + " must be an object");
    auto Index = readU32(Argument->get("index"), Where + ".index");
    if (!Index)
      return Index.takeError();
    if (*Index >= Arguments->size())
      return invalid(Where + ".index is outside the argument array");
    auto Kind = Argument->getString("kind");
    if (!Kind)
      return invalid(Where + ".kind must be a string");
    if (*Kind == "srv" || *Kind == "uav") {
      if (auto Err = checkKeys(
              *Argument, {"index", "kind", "register", "space", "min_size"},
              Where))
        return std::move(Err);
      DirectXBufferBindingTy Binding;
      Binding.Access = *Kind == "srv" ? DirectXBufferBindingTy::Kind::SRV
                                      : DirectXBufferBindingTy::Kind::UAV;
      if (auto Err =
              readRegister(*Argument, Binding.Register, Binding.Space, Where))
        return std::move(Err);
      auto MinSize = readU32(Argument->get("min_size"), Where + ".min_size");
      if (!MinSize)
        return MinSize.takeError();
      if (!*MinSize || *MinSize % 4)
        return invalid(Where + ".min_size must be a positive multiple of 4");
      Binding.MinSize = *MinSize;
      if (!Bindings.emplace(Binding.Access, Binding.Register, Binding.Space)
               .second)
        return invalid(Where + " duplicates a buffer register binding");
      Manifest.Arguments.push_back({*Index, Binding});
      if (++BufferCount > 1000000)
        return invalid("too many buffer descriptors");
    } else if (*Kind == "scalar") {
      if (auto Err =
              checkKeys(*Argument, {"index", "kind", "size", "offset"}, Where))
        return std::move(Err);
      auto Size = readU32(Argument->get("size"), Where + ".size");
      if (!Size)
        return Size.takeError();
      auto Offset = readU32(Argument->get("offset"), Where + ".offset");
      if (!Offset)
        return Offset.takeError();
      if ((*Size != 4 && *Size != 8) || *Offset % *Size)
        return invalid(
            Where + " requires a 4- or 8-byte scalar at a size-aligned offset");
      if (!Manifest.Constants || *Offset > Manifest.Constants->Size ||
          *Size > Manifest.Constants->Size - *Offset)
        return invalid(Where + " is outside the root constant block");
      ScalarRanges.emplace_back(*Offset, *Offset + *Size);
      Manifest.Arguments.push_back(
          {*Index, DirectXScalarBindingTy{*Size, *Offset}});
    } else {
      return error::createOffloadError(error::ErrorCode::UNSUPPORTED,
                                       "unsupported DirectX argument kind '%s'",
                                       Kind->str().c_str());
    }
  }
  llvm::sort(Manifest.Arguments,
             [](const auto &A, const auto &B) { return A.Index < B.Index; });
  for (size_t I = 0; I < Manifest.Arguments.size(); ++I)
    if (Manifest.Arguments[I].Index != I)
      return invalid("argument indices must be unique and dense from zero");
  if (Manifest.Constants && ScalarRanges.empty())
    return invalid("a root constant block requires scalar arguments");
  llvm::sort(ScalarRanges);
  for (size_t I = 1; I < ScalarRanges.size(); ++I)
    if (ScalarRanges[I].first < ScalarRanges[I - 1].second)
      return invalid("scalar arguments overlap in the root constant block");
  return Manifest;
}

Expected<DirectXManifestTy>
DirectXManifestTy::parse(const object::OffloadBinary &Image) {
  if (Image.getImageKind() != object::IMG_DXIL ||
      Image.getOffloadKind() != object::OFK_None || Image.getFlags() != 0 ||
      Image.getImage().empty())
    return invalid(
        "expected a nonempty DXIL entry with no producer kind or flags");
  auto JSON = Image.getString(MetadataKey);
  if (JSON.empty())
    return invalid("missing directx.manifest metadata");
  return parse(JSON);
}

} // namespace llvm::omp::target::plugin::directx
