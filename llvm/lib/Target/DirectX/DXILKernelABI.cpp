//===- DXILKernelABI.cpp - Resource-backed C++ compute kernel ABI
//-----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "DirectX.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/Frontend/Offloading/DirectX.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicsDirectX.h"
#include "llvm/IR/Module.h"
#include "llvm/Pass.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/JSON.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

namespace {

void fail(const Twine &Message) {
  reportFatalUsageError("DirectX C++ kernel ABI: " + Message);
}

bool checkPointer(Value &Arg, bool ReadOnly) {
  SmallVector<Value *> Worklist{&Arg};
  SmallPtrSet<Value *, 16> Seen;
  bool Accessed = false;
  while (!Worklist.empty()) {
    Value *Pointer = Worklist.pop_back_val();
    if (!Seen.insert(Pointer).second)
      continue;
    for (User *U : Pointer->users()) {
      if (auto *GEP = dyn_cast<GetElementPtrInst>(U)) {
        Worklist.push_back(GEP);
        continue;
      }
      Type *Ty = nullptr;
      if (auto *Load = dyn_cast<LoadInst>(U)) {
        if (Load->isAtomic() || Load->isVolatile()) {
          fail("atomic and volatile buffer accesses are not supported");
          return false;
        }
        Ty = Load->getType();
      } else if (auto *Store = dyn_cast<StoreInst>(U)) {
        if (ReadOnly || Store->getPointerOperand() != Pointer ||
            Store->isAtomic() || Store->isVolatile()) {
          fail("buffer pointer escape or unsupported store");
          return false;
        }
        Ty = Store->getValueOperand()->getType();
      } else {
        fail("buffer pointers may only be indexed, loaded, or stored");
        return false;
      }
      if (!Ty->isIntegerTy(32) && !Ty->isFloatTy()) {
        fail("buffer accesses must use 32-bit integers or floats");
        return false;
      }
      Accessed = true;
    }
  }
  if (!Accessed) {
    fail("unused pointer arguments are not supported");
    return false;
  }
  return true;
}

void replaceBufferPointer(Value *Old, Value *Pointer) {
  for (User *U : make_early_inc_range(Old->users())) {
    if (auto *GEP = dyn_cast<GetElementPtrInst>(U)) {
      IRBuilder<> Builder(GEP);
      SmallVector<Value *> Indices(GEP->indices());
      Value *New = Builder.CreateGEP(GEP->getSourceElementType(), Pointer,
                                     Indices, "", GEP->getNoWrapFlags());
      if (auto *I = dyn_cast<Instruction>(New))
        I->copyMetadata(*GEP);
      replaceBufferPointer(GEP, New);
      GEP->eraseFromParent();
    } else if (auto *Load = dyn_cast<LoadInst>(U)) {
      Load->setOperand(0, Pointer);
    } else {
      cast<StoreInst>(U)->setOperand(1, Pointer);
    }
  }
}

Function *flattenCapture(Function &Kernel, json::Object &Capture) {
  auto *MD = Kernel.getMetadata("dx.capture.layout");
  if (!MD || MD->getNumOperands() < 3 || Kernel.arg_size() != 1 ||
      !Kernel.getArg(0)->hasByValAttr()) {
    fail("capture kernels require one by-value record and a capture layout");
    return nullptr;
  }
  const DataLayout &Layout = Kernel.getParent()->getDataLayout();
  auto Integer = [](Metadata *Value) -> std::optional<uint32_t> {
    auto *CAM = dyn_cast<ConstantAsMetadata>(Value);
    auto *CI = CAM ? dyn_cast<ConstantInt>(CAM->getValue()) : nullptr;
    if (!CI || !CI->getType()->isIntegerTy(32))
      return std::nullopt;
    return CI->getZExtValue();
  };
  auto *Record = dyn_cast<StructType>(Kernel.getArg(0)->getParamByValType());
  if (!Record || Record->isOpaque() || Record->getNumElements() == 0) {
    fail("capture argument must be a nonempty flat record");
    return nullptr;
  }
  auto *RecordLayout = Layout.getStructLayout(Record);
  auto Bytes = Integer(MD->getOperand(0));
  auto PointerSize = Integer(MD->getOperand(1));
  if (!Bytes || !*Bytes || !PointerSize || *PointerSize != 8 ||
      *Bytes != RecordLayout->getSizeInBytes()) {
    fail("capture ABI requires a 64-bit pointer layout and a bounded record");
    return nullptr;
  }
  SmallVector<Type *> Types;
  unsigned NumFields = MD->getNumOperands() - 2;
  SmallVector<SmallVector<LoadInst *>> Loads(NumFields);
  SmallVector<bool> ReadOnly(NumFields, true);
  SmallVector<uint32_t> FieldOffsets;
  SmallVector<Type *> FieldTypes;
  json::Array Offsets;
  uint32_t End = 0;
  for (unsigned I = 0; I < NumFields; ++I) {
    auto *Field = dyn_cast<MDNode>(MD->getOperand(I + 2));
    auto Offset = Field && Field->getNumOperands() == 2
                      ? Integer(Field->getOperand(0))
                      : std::nullopt;
    auto Size = Field && Field->getNumOperands() == 2
                    ? Integer(Field->getOperand(1))
                    : std::nullopt;
    if (!Offset || !Size || *Offset < End || *Offset >= *Bytes ||
        *Size > *Bytes - *Offset) {
      fail("capture fields must be ordered, nonoverlapping, and in bounds");
      return nullptr;
    }
    unsigned Element = RecordLayout->getElementContainingOffset(*Offset);
    Type *Ty = Record->getElementType(Element);
    if (RecordLayout->getElementOffset(Element) != *Offset) {
      fail("capture layout does not match record field offsets");
      return nullptr;
    }
    if (Ty->isPointerTy()) {
      unsigned AS = Ty->getPointerAddressSpace();
      if ((AS != 0 && AS != 4) || *Size != 8 || *Offset % 8) {
        fail("captured buffers require 64-bit default or generic pointers");
        return nullptr;
      }
      Types.push_back(PointerType::get(Kernel.getContext(), 0));
    } else if ((Ty->isIntegerTy(32) || Ty->isFloatTy()) && *Size == 4 &&
               *Offset % 4 == 0) {
      Types.push_back(Ty);
    } else {
      fail("captures must be buffer pointers or 32-bit integer/float values");
      return nullptr;
    }
    FieldOffsets.push_back(*Offset);
    FieldTypes.push_back(Ty);
    Offsets.push_back(*Offset);
    End = *Offset + *Size;
  }
  SmallVector<Value *> Worklist{Kernel.getArg(0)};
  SmallVector<Instruction *> Addresses;
  SmallPtrSet<Value *, 16> Seen;
  while (!Worklist.empty()) {
    Value *Address = Worklist.pop_back_val();
    if (!Seen.insert(Address).second)
      continue;
    for (User *U : Address->users()) {
      if (isa<GetElementPtrInst, AddrSpaceCastInst>(U)) {
        auto *I = cast<Instruction>(U);
        Worklist.push_back(I);
        Addresses.push_back(I);
        continue;
      }
      auto *Load = dyn_cast<LoadInst>(U);
      int64_t Offset = 0;
      if (!Load || Load->isAtomic() || Load->isVolatile() ||
          GetPointerBaseWithConstantOffset(Address, Offset, Layout) !=
              Kernel.getArg(0) ||
          Offset < 0 || uint64_t(Offset) >= *Bytes) {
        fail("capture records may only be read at constant field offsets");
        return nullptr;
      }
      auto It = llvm::find(FieldOffsets, uint64_t(Offset));
      unsigned Field = It - FieldOffsets.begin();
      if (It == FieldOffsets.end() || Load->getType() != FieldTypes[Field]) {
        fail("capture load does not match a complete record field");
        return nullptr;
      }
      Loads[Field].push_back(Load);
      if (Load->getType()->isPointerTy()) {
        if (!checkPointer(*Load, false))
          return nullptr;
        SmallVector<Value *> Pointers{Load};
        SmallPtrSet<Value *, 16> Visited;
        while (!Pointers.empty()) {
          Value *Pointer = Pointers.pop_back_val();
          if (!Visited.insert(Pointer).second)
            continue;
          for (User *Use : Pointer->users())
            if (isa<StoreInst>(Use))
              ReadOnly[Field] = false;
            else if (isa<GetElementPtrInst>(Use))
              Pointers.push_back(Use);
        }
      }
    }
  }
  if (llvm::any_of(Loads, [](const auto &Field) { return Field.empty(); })) {
    fail("unused captured fields are not supported");
    return nullptr;
  }
  auto *Flat =
      Function::Create(FunctionType::get(Kernel.getReturnType(), Types, false),
                       Kernel.getLinkage(), "", Kernel.getParent());
  Flat->takeName(&Kernel);
  Flat->setAttributes(AttributeList::get(
      Kernel.getContext(), Kernel.getAttributes().getFnAttrs(), {}, {}));
  Flat->copyMetadata(&Kernel, 0);
  Flat->setMetadata("dx.capture.layout", nullptr);
  Flat->setVisibility(Kernel.getVisibility());
  Flat->setDSOLocal(Kernel.isDSOLocal());
  Flat->splice(Flat->begin(), &Kernel);
  for (auto [Index, Arg] : enumerate(Flat->args())) {
    if (Arg.getType()->isPointerTy() && ReadOnly[Index])
      Arg.addAttr(Attribute::get(Kernel.getContext(), "dx.readonly"));
    for (LoadInst *Load : Loads[Index]) {
      if (Load->getType()->isPointerTy())
        replaceBufferPointer(Load, &Arg);
      else
        Load->replaceAllUsesWith(&Arg);
      Load->eraseFromParent();
    }
  }
  // Children can be reached in different orders through shared address chains.
  while (!Addresses.empty()) {
    auto It =
        llvm::find_if(Addresses, [](Instruction *I) { return I->use_empty(); });
    assert(It != Addresses.end() && "validated capture address escaped");
    (*It)->eraseFromParent();
    Addresses.erase(It);
  }
  Capture = json::Object{
      {"size", *Bytes}, {"pointer_size", 8}, {"offsets", std::move(Offsets)}};
  Kernel.eraseFromParent();
  return Flat;
}

bool lowerKernel(Module &M) {
  Function *Kernel = nullptr;
  for (Function &F : M) {
    if (!F.hasFnAttribute("dx.kernel"))
      continue;
    if (Kernel || F.isDeclaration()) {
      fail("exactly one defined kernel is required");
      return false;
    }
    Kernel = &F;
  }
  if (!Kernel)
    return false;
  if (!Kernel->getReturnType()->isVoidTy() || Kernel->isVarArg() ||
      !Kernel->use_empty()) {
    fail("kernel must return void and cannot be called or variadic");
    return false;
  }
  if (M.getTargetTriple().getEnvironment() != Triple::Compute) {
    fail("a compute shader target is required");
    return false;
  }
  SmallVector<StringRef> Dims;
  Kernel->getFnAttribute("hlsl.numthreads").getValueAsString().split(Dims, ',');
  uint32_t Threads[3]{};
  uint64_t Product = 1;
  if (Dims.size() != 3) {
    fail("reqd_work_group_size is required");
    return false;
  }
  for (unsigned I = 0; I < 3; ++I) {
    if (Dims[I].getAsInteger(10, Threads[I]) || !Threads[I] ||
        Threads[I] > (I == 2 ? 64u : 1024u)) {
      fail("invalid workgroup dimensions");
      return false;
    }
    Product *= Threads[I];
  }
  if (Product > 1024) {
    fail("workgroup exceeds 1024 threads");
    return false;
  }
  std::optional<json::Object> Capture;
  if (Kernel->getMetadata("dx.capture.layout")) {
    Capture.emplace();
    Kernel = flattenCapture(*Kernel, *Capture);
    if (!Kernel)
      return false;
  }
  SmallVector<Type *> ScalarTypes;
  for (Argument &Arg : Kernel->args()) {
    Type *Ty = Arg.getType();
    if (Ty->isIntegerTy(32) || Ty->isFloatTy()) {
      if (Arg.use_empty()) {
        fail("unused scalar arguments are not supported");
        return false;
      }
      ScalarTypes.push_back(Ty);
      continue;
    }
    if (!Ty->isPointerTy() || Ty->getPointerAddressSpace() != 0) {
      fail("arguments must be default-address-space buffer pointers or "
           "32-bit integer/float scalars");
      return false;
    }
    if (!checkPointer(Arg, Arg.hasAttribute("dx.readonly")))
      return false;
  }
  using namespace offloading::directx;
  SmallVector<CallInst *> DispatchQueries;
  for (BasicBlock &BB : *Kernel)
    for (Instruction &I : BB)
      if (auto *Call = dyn_cast<CallInst>(&I);
          Call && (Call->getIntrinsicID() == Intrinsic::dx_num_groups ||
                   Call->getIntrinsicID() == Intrinsic::dx_global_offset)) {
        auto *Dim = dyn_cast<ConstantInt>(Call->getArgOperand(0));
        if (!Dim || Dim->getZExtValue() > 2) {
          fail("dispatch dimension must be between 0 and 2");
          return false;
        }
        DispatchQueries.push_back(Call);
      }
  unsigned DispatchOffset = ScalarTypes.size() * 4;
  if (!DispatchQueries.empty())
    ScalarTypes.append(DispatchWords, Type::getInt32Ty(M.getContext()));
  if (ScalarTypes.size() > 63) {
    fail("scalar arguments exceed the 252-byte root constant limit");
    return false;
  }
  for (BasicBlock &BB : *Kernel)
    for (Instruction &I : BB)
      if (auto *Call = dyn_cast<CallBase>(&I))
        if (!Call->getCalledFunction() ||
            !Call->getCalledFunction()->isIntrinsic()) {
          if (Function *Callee = Call->getCalledFunction())
            if (Callee->hasFnAttribute("dontcall-error")) {
              fail(Callee->getFnAttribute("dontcall-error").getValueAsString());
              return false;
            }
          fail("device helper calls must be inlined before ABI lowering");
          return false;
        }

  auto *Entry = Function::Create(
      FunctionType::get(Type::getVoidTy(M.getContext()), false),
      Kernel->getLinkage(), "", &M);
  Entry->takeName(Kernel);
  Entry->setAttributes(AttributeList::get(
      M.getContext(), Kernel->getAttributes().getFnAttrs(), {}, {}));
  Entry->removeFnAttr("dx.kernel");
  // Resource-backed memory is no longer accessed through function arguments.
  Entry->removeFnAttr(Attribute::Memory);
  Entry->copyMetadata(Kernel, 0);
  Entry->setVisibility(Kernel->getVisibility());
  Entry->setDSOLocal(Kernel->isDSOLocal());
  Entry->addFnAttr("hlsl.shader", "compute");
  Entry->addFnAttr(
      "hlsl.numthreads",
      Kernel->getFnAttribute("hlsl.numthreads").getValueAsString());
  Entry->splice(Entry->begin(), Kernel);
  IRBuilder<> Builder(&*Entry->getEntryBlock().getFirstInsertionPt());
  SmallVector<OperandBundleDef, 1> PointerBundles;
  for (Instruction &I : Entry->getEntryBlock())
    if (auto *Call = dyn_cast<CallInst>(&I);
        Call &&
        Call->getIntrinsicID() == Intrinsic::experimental_convergence_entry) {
      Builder.SetInsertPoint(Call->getNextNode());
      PointerBundles.emplace_back("convergencectrl", ArrayRef<Value *>{Call});
      break;
    }
  if (!M.getNamedMetadata("dx.valver"))
    M.getOrInsertNamedMetadata("dx.valver")
        ->addOperand(MDNode::get(
            M.getContext(), {ConstantAsMetadata::get(Builder.getInt32(1)),
                             ConstantAsMetadata::get(Builder.getInt32(8))}));
  unsigned SRV = 0, UAV = 0;
  json::Array Arguments;
  json::Value Constants = nullptr;
  Value *ConstantHandle = nullptr;
  TargetExtType *ConstantHandleTy = nullptr;
  if (!ScalarTypes.empty()) {
    auto *Layout = StructType::create(M.getContext(), ScalarTypes,
                                      "__dx_kernel_args", true);
    ConstantHandleTy =
        TargetExtType::get(M.getContext(), "dx.CBuffer", {Layout});
    ConstantHandle = Builder.CreateIntrinsic(
        ConstantHandleTy, Intrinsic::dx_resource_handlefrombinding,
        {Builder.getInt32(0), Builder.getInt32(0), Builder.getInt32(1),
         Builder.getInt32(0), Builder.CreateGlobalString(Layout->getName())});
    unsigned Bytes = ScalarTypes.size() * 4;
    SmallVector<Metadata *> Fields{
        ConstantAsMetadata::get(Builder.getInt32(Bytes))};
    for (auto [Index, Ty] : enumerate(ScalarTypes))
      Fields.push_back(MDNode::get(
          M.getContext(), {ConstantAsMetadata::get(Builder.getInt32(3)),
                           ConstantAsMetadata::get(Builder.getInt32(Index * 4)),
                           ConstantAsMetadata::get(Builder.getInt32(7)),
                           ConstantAsMetadata::get(
                               Builder.getInt32(Ty->isFloatTy() ? 9 : 4))}));
    M.getOrInsertNamedMetadata("dx.typeAnnotations")
        ->addOperand(MDNode::get(
            M.getContext(),
            {ConstantAsMetadata::get(Builder.getInt32(0)),
             ConstantAsMetadata::get(ConstantAggregateZero::get(Layout)),
             MDNode::get(M.getContext(), Fields)}));
    Constants = json::Object{{"register", 0}, {"space", 0}, {"size", Bytes}};
  }
  unsigned ScalarOffset = 0;
  for (Argument &Arg : Kernel->args()) {
    if (!Arg.getType()->isPointerTy()) {
      auto *Pointer = Builder.CreateIntrinsic(
          Intrinsic::dx_resource_getpointer,
          {Builder.getPtrTy(2), ConstantHandleTy, Builder.getInt32Ty()},
          {ConstantHandle, Builder.getInt32(ScalarOffset)}, {}, "",
          PointerBundles);
      Arg.replaceAllUsesWith(Builder.CreateLoad(Arg.getType(), Pointer));
      Arguments.push_back(json::Object{{"index", Arg.getArgNo()},
                                       {"kind", "scalar"},
                                       {"size", 4},
                                       {"offset", ScalarOffset}});
      ScalarOffset += 4;
      continue;
    }
    bool ReadOnly = Arg.hasAttribute("dx.readonly");
    unsigned Register = ReadOnly ? SRV++ : UAV++;
    auto *HandleTy =
        TargetExtType::get(M.getContext(), "dx.RawBuffer",
                           {Builder.getInt8Ty()}, {unsigned(!ReadOnly), 0});
    auto *Handle = Builder.CreateIntrinsic(
        HandleTy, Intrinsic::dx_resource_handlefrombinding,
        {Builder.getInt32(0), Builder.getInt32(Register), Builder.getInt32(1),
         Builder.getInt32(0), ConstantPointerNull::get(Builder.getPtrTy())});
    auto *Pointer = Builder.CreateIntrinsic(
        Intrinsic::dx_resource_getpointer,
        {Arg.getType(), HandleTy, Builder.getInt32Ty()},
        {Handle, Builder.getInt32(0)}, {}, "", PointerBundles);
    Arg.replaceAllUsesWith(Pointer);
    Arguments.push_back(json::Object{{"index", Arg.getArgNo()},
                                     {"kind", ReadOnly ? "srv" : "uav"},
                                     {"register", Register},
                                     {"space", 0},
                                     {"min_size", 4}});
  }
  for (CallInst *Call : DispatchQueries) {
    IRBuilder<> QueryBuilder(Call);
    unsigned Dim = cast<ConstantInt>(Call->getArgOperand(0))->getZExtValue();
    bool IsOffset = Call->getIntrinsicID() == Intrinsic::dx_global_offset;
    unsigned Word = IsOffset ? GlobalOffsetWord + Dim * 2 : Dim;
    auto LoadWord = [&](unsigned Index) {
      Value *Pointer = QueryBuilder.CreateIntrinsic(
          Intrinsic::dx_resource_getpointer,
          {Builder.getPtrTy(2), ConstantHandleTy, Builder.getInt32Ty()},
          {ConstantHandle, Builder.getInt32(DispatchOffset + Index * 4)}, {},
          "", PointerBundles);
      return QueryBuilder.CreateLoad(Builder.getInt32Ty(), Pointer);
    };
    Value *Result = LoadWord(Word);
    if (IsOffset)
      Result = QueryBuilder.CreateOr(
          QueryBuilder.CreateZExt(Result, Builder.getInt64Ty()),
          QueryBuilder.CreateShl(
              QueryBuilder.CreateZExt(LoadWord(Word + 1), Builder.getInt64Ty()),
              32));
    Call->replaceAllUsesWith(Result);
    Call->eraseFromParent();
  }
  std::string Manifest;
  raw_string_ostream OS(Manifest);
  VersionTuple Model = M.getTargetTriple().getOSVersion();
  json::Object ManifestObject{
      {"version", 1},
      {"entry", Entry->getName().str()},
      {"shader_model",
       json::Array{Model.getMajor(), Model.getMinor().value_or(0)}},
      {"numthreads", json::Array{Threads[0], Threads[1], Threads[2]}},
      {"features", json::Array{}},
      {"constants", std::move(Constants)},
      {"arguments", std::move(Arguments)}};
  if (Capture)
    ManifestObject["capture"] = std::move(*Capture);
  if (!DispatchQueries.empty())
    ManifestObject["dispatch"] =
        json::Object{{"version", DispatchVersion}, {"offset", DispatchOffset}};
  OS << json::Value(std::move(ManifestObject));
  M.getOrInsertNamedMetadata("llvm.offload.manifest")
      ->addOperand(
          MDNode::get(M.getContext(), MDString::get(M.getContext(), Manifest)));
  Kernel->eraseFromParent();

  for (StringRef Name : {"llvm.dx.num.groups", "llvm.dx.global.offset"})
    if (Function *Query = M.getFunction(Name); Query && Query->use_empty())
      Query->eraseFromParent();
  if (Function *Size = M.getFunction("llvm.dx.thread.group.size")) {
    for (User *U : make_early_inc_range(Size->users())) {
      auto *Call = dyn_cast<CallInst>(U);
      auto *Dim =
          Call ? dyn_cast<ConstantInt>(Call->getArgOperand(0)) : nullptr;
      if (!Dim || Dim->getZExtValue() > 2) {
        fail("group-size dimension must be between 0 and 2");
        return true;
      }
      Call->replaceAllUsesWith(Builder.getInt32(Threads[Dim->getZExtValue()]));
      Call->eraseFromParent();
    }
    Size->eraseFromParent();
  }
  M.setDataLayout(M.getTargetTriple().computeDataLayout());
  return true;
}

class DXILKernelABILegacy : public ModulePass {
public:
  static char ID;
  DXILKernelABILegacy() : ModulePass(ID) {}
  bool runOnModule(Module &M) override { return lowerKernel(M); }
};

} // namespace

char DXILKernelABILegacy::ID = 0;

INITIALIZE_PASS(DXILKernelABILegacy, "dxil-kernel-abi",
                "DirectX C++ compute kernel ABI", false, false)

ModulePass *llvm::createDXILKernelABILegacyPass() {
  return new DXILKernelABILegacy();
}
