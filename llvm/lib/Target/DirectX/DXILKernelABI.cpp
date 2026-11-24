//===- DXILKernelABI.cpp - Resource-backed C++ compute kernel ABI
//-----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "DirectX.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/Frontend/Offloading/DirectX.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/IntrinsicsDirectX.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Module.h"
#include "llvm/Pass.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/JSON.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/Transforms/Utils/Local.h"
#include "llvm/Transforms/Utils/LowerMemIntrinsics.h"

using namespace llvm;

namespace {

cl::list<unsigned> WorkgroupSize(
    "dxil-kernel-numthreads", cl::CommaSeparated, cl::Hidden,
    cl::desc(
        "Specialize an eligible DirectX kernel to x,y,z workgroup dimensions"));

cl::list<uint64_t> LocalAllocations(
    "dxil-kernel-local-allocations", cl::CommaSeparated, cl::Hidden,
    cl::desc("Specialize captured local allocations (identity:32, bytes:32)"));

void fail(const Twine &Message) {
  reportFatalUsageError("DirectX C++ kernel ABI: " + Message);
}

void scalarizeVectorAccess(Instruction &Access, FixedVectorType *VectorType,
                           const DataLayout &Layout) {
  auto *Load = dyn_cast<LoadInst>(&Access);
  auto *Store = dyn_cast<StoreInst>(&Access);
  Value *Pointer =
      Load ? Load->getPointerOperand() : Store->getPointerOperand();
  Align Alignment = Load ? Load->getAlign() : Store->getAlign();
  Type *ElementType = VectorType->getElementType();
  uint64_t ElementBytes = Layout.getTypeStoreSize(ElementType).getFixedValue();
  IRBuilder<> Builder(&Access);
  Value *Result = PoisonValue::get(VectorType);
  for (unsigned I = 0; I < VectorType->getNumElements(); ++I) {
    uint64_t Offset = I * ElementBytes;
    Value *Address =
        Builder.CreateConstGEP1_64(Builder.getInt8Ty(), Pointer, Offset);
    if (Load) {
      auto *Part = Builder.CreateAlignedLoad(
          ElementType, Address, commonAlignment(Alignment, Offset));
      copyMetadataForLoad(*Part, *Load);
      Part->setAAMetadata(
          Load->getAAMetadata().adjustForAccess(Offset, ElementBytes));
      Part->setMetadata(LLVMContext::MD_range, nullptr);
      Result = Builder.CreateInsertElement(Result, Part, I);
    } else {
      auto *Part = Builder.CreateAlignedStore(
          Builder.CreateExtractElement(Store->getValueOperand(), I), Address,
          commonAlignment(Alignment, Offset));
      Part->copyMetadata(*Store);
      Part->setAAMetadata(
          Store->getAAMetadata().adjustForAccess(Offset, ElementBytes));
    }
  }
  if (Load)
    Load->replaceAllUsesWith(Result);
  Access.eraseFromParent();
}

void scalarizeWordCopy(MemTransferInst &Copy) {
  auto *Length = dyn_cast<ConstantInt>(Copy.getLength());
  if (Copy.isVolatile() || !Length || Length->getLimitedValue() > 32768 ||
      Length->getZExtValue() % 4 ||
      Copy.getSourceAlign().valueOrOne() < Align(4) ||
      Copy.getDestAlign().valueOrOne() < Align(4))
    fail("memory copies require a constant word-aligned size within "
         "the 32768-byte limit");
  IRBuilder<> Builder(&Copy);
  SmallVector<Value *> Words;
  for (uint64_t Offset = 0; Offset < Length->getZExtValue(); Offset += 4) {
    auto *Word = Builder.CreateAlignedLoad(
        Builder.getInt32Ty(),
        Builder.CreateConstGEP1_64(Builder.getInt8Ty(), Copy.getRawSource(),
                                   Offset),
        commonAlignment(Copy.getSourceAlign().valueOrOne(), Offset));
    Word->setAAMetadata(Copy.getAAMetadata().adjustForAccess(Offset, 4));
    Words.push_back(Word);
  }
  // Read all words before writing to preserve overlapping memmove semantics.
  for (auto [I, Word] : enumerate(Words)) {
    uint64_t Offset = I * 4;
    auto *Store = Builder.CreateAlignedStore(
        Word,
        Builder.CreateConstGEP1_64(Builder.getInt8Ty(), Copy.getRawDest(),
                                   Offset),
        commonAlignment(Copy.getDestAlign().valueOrOne(), Offset));
    Store->setAAMetadata(Copy.getAAMetadata().adjustForAccess(Offset, 4));
  }
  Copy.eraseFromParent();
}

bool checkLocalPointer(Value *Root, const DataLayout &Layout,
                       uint64_t &MinimumBytes) {
  SmallVector<Value *> Worklist{Root};
  SmallPtrSet<Value *, 16> Seen;
  while (!Worklist.empty()) {
    Value *Pointer = Worklist.pop_back_val();
    if (!Seen.insert(Pointer).second)
      continue;
    for (User *Use : make_early_inc_range(Pointer->users())) {
      if (isa<GetElementPtrInst, AddrSpaceCastInst>(Use)) {
        Worklist.push_back(Use);
        continue;
      }
      if (auto *Copy = dyn_cast<MemTransferInst>(Use)) {
        scalarizeWordCopy(*Copy);
        Seen.erase(Pointer);
        Worklist.push_back(Pointer);
        break;
      }
      Type *AccessType = nullptr;
      if (auto *Load = dyn_cast<LoadInst>(Use)) {
        AccessType = Load->getType();
        if (Load->isAtomic() || Load->isVolatile() ||
            (!AccessType->getScalarType()->isIntegerTy(32) &&
             !AccessType->getScalarType()->isIntegerTy(64) &&
             !AccessType->getScalarType()->isFloatTy()) ||
            isa<ScalableVectorType>(AccessType)) {
          fail("local accessors require non-atomic 32/64-bit integer or "
               "32-bit float scalar or fixed-vector accesses");
          return false;
        }
      } else if (auto *Store = dyn_cast<StoreInst>(Use)) {
        AccessType = Store->getValueOperand()->getType();
        if (Store->getPointerOperand() != Pointer || Store->isAtomic() ||
            Store->isVolatile() ||
            (!AccessType->getScalarType()->isIntegerTy(32) &&
             !AccessType->getScalarType()->isIntegerTy(64) &&
             !AccessType->getScalarType()->isFloatTy()) ||
            isa<ScalableVectorType>(AccessType)) {
          fail("local accessors require non-atomic 32/64-bit integer or "
               "32-bit float scalar or fixed-vector accesses");
          return false;
        }
      } else {
        fail("local accessor pointers may only be indexed, loaded, or stored");
        return false;
      }
      int64_t Offset = 0;
      if (GetPointerBaseWithConstantOffset(Pointer, Offset, Layout) == Root) {
        if (Offset < 0) {
          fail("local accessor constant accesses precede the allocation");
          return false;
        }
        MinimumBytes =
            std::max(MinimumBytes,
                     uint64_t(Offset) +
                         Layout.getTypeStoreSize(AccessType).getFixedValue());
      }
      if (auto *VectorType = dyn_cast<FixedVectorType>(AccessType)) {
        scalarizeVectorAccess(*cast<Instruction>(Use), VectorType, Layout);
        Seen.erase(Pointer);
        Worklist.push_back(Pointer);
      }
    }
  }
  return true;
}

Value *getLocalWordPointer(IRBuilder<> &Builder, Value *Pointer, Value *Index) {
  if (auto *Global = dyn_cast<GlobalVariable>(Pointer))
    return Builder.CreateGEP(Global->getValueType(), Global,
                             {Builder.getInt32(0), Index});
  return Builder.CreateGEP(Builder.getInt32Ty(), Pointer, Index);
}

void replaceLocalPointer(Value *Old, Value *Pointer, const DataLayout &Layout) {
  for (User *Use : make_early_inc_range(Old->users())) {
    if (auto *Cast = dyn_cast<AddrSpaceCastInst>(Use)) {
      replaceLocalPointer(Cast, Pointer, Layout);
      Cast->eraseFromParent();
    } else if (auto *GEP = dyn_cast<GetElementPtrInst>(Use)) {
      unsigned Bits = Layout.getIndexSizeInBits(GEP->getPointerAddressSpace());
      APInt ConstantOffset(Bits, 0);
      SmallMapVector<Value *, APInt, 4> Variables;
      if (!GEP->collectOffset(Layout, Bits, Variables, ConstantOffset) ||
          !ConstantOffset.srem(APInt(Bits, 4)).isZero() ||
          llvm::any_of(Variables, [Bits](const auto &Variable) {
            return !Variable.second.srem(APInt(Bits, 4)).isZero();
          }))
        fail("local accessor indexing requires four-byte-aligned strides");
      IRBuilder<> Builder(GEP);
      Value *Index =
          Builder.getInt32(ConstantOffset.sdiv(APInt(Bits, 4)).getSExtValue());
      for (auto &[Variable, Scale] : Variables)
        Index = Builder.CreateAdd(
            Index,
            Builder.CreateMul(
                Builder.CreateIntCast(Variable, Builder.getInt32Ty(), true),
                Builder.getInt32(Scale.sdiv(APInt(Bits, 4)).getSExtValue())));
      Value *Address = getLocalWordPointer(Builder, Pointer, Index);
      if (auto *Instruction = dyn_cast<llvm::Instruction>(Address))
        Instruction->copyMetadata(*GEP);
      replaceLocalPointer(GEP, Address, Layout);
      GEP->eraseFromParent();
    } else if (auto *Load = dyn_cast<LoadInst>(Use);
               Load && Load->getType()->isIntegerTy(64)) {
      IRBuilder<> Builder(Load);
      Value *Result = nullptr;
      for (unsigned Word = 0; Word != 2; ++Word) {
        auto *Part = Builder.CreateAlignedLoad(
            Builder.getInt32Ty(),
            getLocalWordPointer(Builder, Pointer, Builder.getInt32(Word)),
            commonAlignment(Load->getAlign(), Word * 4));
        copyMetadataForLoad(*Part, *Load);
        Part->setAAMetadata(Load->getAAMetadata().adjustForAccess(Word * 4, 4));
        // A whole-value range is not a range for either split word.
        Part->setMetadata(LLVMContext::MD_range, nullptr);
        Value *WidePart = Builder.CreateZExt(Part, Builder.getInt64Ty());
        Result = Word
                     ? Builder.CreateOr(Result, Builder.CreateShl(WidePart, 32))
                     : WidePart;
      }
      Load->replaceAllUsesWith(Result);
      Load->eraseFromParent();
    } else if (auto *Store = dyn_cast<StoreInst>(Use);
               Store && Store->getValueOperand()->getType()->isIntegerTy(64)) {
      IRBuilder<> Builder(Store);
      for (unsigned Word = 0; Word != 2; ++Word) {
        Value *Part = Store->getValueOperand();
        if (Word)
          Part = Builder.CreateLShr(Part, 32);
        auto *NewStore = Builder.CreateAlignedStore(
            Builder.CreateTrunc(Part, Builder.getInt32Ty()),
            getLocalWordPointer(Builder, Pointer, Builder.getInt32(Word)),
            commonAlignment(Store->getAlign(), Word * 4));
        NewStore->copyMetadata(*Store);
        NewStore->setAAMetadata(
            Store->getAAMetadata().adjustForAccess(Word * 4, 4));
      }
      Store->eraseFromParent();
    } else if (auto *Load = dyn_cast<LoadInst>(Use);
               Load && Load->getType()->isFloatTy()) {
      IRBuilder<> Builder(Load);
      auto *Bits = Builder.CreateAlignedLoad(Builder.getInt32Ty(), Pointer,
                                             Load->getAlign());
      Bits->copyMetadata(*Load);
      Load->replaceAllUsesWith(Builder.CreateBitCast(Bits, Load->getType()));
      Load->eraseFromParent();
    } else if (auto *Store = dyn_cast<StoreInst>(Use);
               Store && Store->getValueOperand()->getType()->isFloatTy()) {
      IRBuilder<> Builder(Store);
      Store->setOperand(0, Builder.CreateBitCast(Store->getValueOperand(),
                                                 Builder.getInt32Ty()));
      Store->setOperand(1, Pointer);
    } else {
      Use->replaceUsesOfWith(Old, Pointer);
    }
  }
}

bool checkPointer(Value &Arg, bool ReadOnly) {
  SmallVector<Value *> Worklist{&Arg};
  SmallPtrSet<Value *, 16> Seen;
  bool Accessed = false;
  while (!Worklist.empty()) {
    Value *Pointer = Worklist.pop_back_val();
    if (!Seen.insert(Pointer).second)
      continue;
    for (User *U : make_early_inc_range(Pointer->users())) {
      if (auto *GEP = dyn_cast<GetElementPtrInst>(U)) {
        Worklist.push_back(GEP);
        continue;
      }
      if (auto *Cast = dyn_cast<AddrSpaceCastInst>(U)) {
        unsigned AS = Cast->getDestAddressSpace();
        if (AS != 0 && AS != 1 && AS != 4) {
          fail("buffer address-space casts must preserve global memory");
          return false;
        }
        Worklist.push_back(Cast);
        continue;
      }
      if (auto *MemSet = dyn_cast<MemSetInst>(U)) {
        if (ReadOnly || MemSet->isVolatile()) {
          fail("buffer pointer escape or unsupported store");
          return false;
        }
        expandMemSetAsLoop(MemSet);
        MemSet->eraseFromParent();
        // Expansion adds pointer users that must be checked before remapping.
        Seen.erase(Pointer);
        Worklist.push_back(Pointer);
        continue;
      }
      if (auto *Copy = dyn_cast<MemTransferInst>(U)) {
        if (ReadOnly && Copy->getRawDest() == Pointer) {
          fail("buffer pointer escape or unsupported store");
          return false;
        }
        scalarizeWordCopy(*Copy);
        Seen.erase(Pointer);
        Worklist.push_back(Pointer);
        break;
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
      auto *VectorTy = dyn_cast<FixedVectorType>(Ty);
      bool Is64BitInteger =
          Ty->isIntegerTy(64) ||
          (VectorTy && VectorTy->getElementType()->isIntegerTy(64));
      if (isa<ScalableVectorType>(Ty) ||
          (!Ty->isIntegerTy(8) && !Ty->getScalarType()->isIntegerTy(32) &&
           !Ty->getScalarType()->isFloatTy() && !Is64BitInteger)) {
        fail("buffer accesses must use 8/32/64-bit integers or floats");
        return false;
      }
      if (VectorTy) {
        scalarizeVectorAccess(
            *cast<Instruction>(U), VectorTy,
            cast<Instruction>(U)->getModule()->getDataLayout());
        Seen.erase(Pointer);
        Worklist.push_back(Pointer);
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
    } else if (auto *Cast = dyn_cast<AddrSpaceCastInst>(U)) {
      replaceBufferPointer(Cast, Pointer);
      Cast->eraseFromParent();
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
  SmallVector<uint32_t> FieldSizes;
  SmallVector<Type *> FieldTypes;
  SmallVector<uint32_t> LocalAlignments(NumFields, 0);
  SmallVector<std::pair<uint32_t, ArrayType *>> CaptureArrays;
  json::Array Offsets;
  json::Array Sizes;
  uint32_t End = 0;
  for (unsigned I = 0; I < NumFields; ++I) {
    auto *Field = dyn_cast<MDNode>(MD->getOperand(I + 2));
    bool ValidField =
        Field && Field->getNumOperands() >= 2 && Field->getNumOperands() <= 4;
    auto Offset = ValidField ? Integer(Field->getOperand(0)) : std::nullopt;
    auto Size = ValidField ? Integer(Field->getOperand(1)) : std::nullopt;
    if (!Offset || !Size || *Offset < End || *Offset >= *Bytes ||
        *Size > *Bytes - *Offset) {
      fail("capture fields must be ordered, nonoverlapping, and in bounds");
      return nullptr;
    }
    unsigned Element = RecordLayout->getElementContainingOffset(*Offset);
    Type *Ty = Record->getElementType(Element);
    uint64_t RelativeOffset = *Offset - RecordLayout->getElementOffset(Element);
    while (Ty->isStructTy() || Ty->isArrayTy()) {
      if (auto *Array = dyn_cast<ArrayType>(Ty)) {
        std::pair<uint32_t, ArrayType *> Storage{
            uint32_t(*Offset - RelativeOffset), Array};
        if (!llvm::is_contained(CaptureArrays, Storage))
          CaptureArrays.push_back(Storage);
        Type *ElementType = Array->getElementType();
        uint64_t Stride = Layout.getTypeAllocSize(ElementType);
        if (!Stride || RelativeOffset / Stride >= Array->getNumElements()) {
          fail("capture layout does not match array element offsets");
          return nullptr;
        }
        RelativeOffset %= Stride;
        Ty = ElementType;
        continue;
      }
      auto *Nested = cast<StructType>(Ty);
      if (Nested->isOpaque() || Nested->getNumElements() == 0) {
        fail("capture layout contains an empty or opaque nested record");
        return nullptr;
      }
      const StructLayout *NestedLayout = Layout.getStructLayout(Nested);
      if (RelativeOffset >= NestedLayout->getSizeInBytes()) {
        fail("capture layout does not match record field offsets");
        return nullptr;
      }
      unsigned NestedElement =
          NestedLayout->getElementContainingOffset(RelativeOffset);
      RelativeOffset -= NestedLayout->getElementOffset(NestedElement);
      Ty = Nested->getElementType(NestedElement);
    }
    if (RelativeOffset != 0) {
      fail("capture layout does not match record field offsets");
      return nullptr;
    }
    if (Field->getNumOperands() == 4) {
      auto Marker = Integer(Field->getOperand(2));
      auto Alignment = Integer(Field->getOperand(3));
      if (!Marker || *Marker != 2 || !Alignment || !isPowerOf2_32(*Alignment) ||
          *Alignment < 4 || *Alignment > 128 || !Ty->isPointerTy() ||
          Ty->getPointerAddressSpace() != 3 || *Size != 8 || *Offset % 8) {
        fail("local captures require an aligned 64-bit local pointer field");
        return nullptr;
      }
      LocalAlignments[I] = *Alignment;
    } else if (Field->getNumOperands() == 3) {
      auto AccessorPointer = Integer(Field->getOperand(2));
      if (!AccessorPointer || *AccessorPointer != 1 || !Ty->isPointerTy()) {
        fail("accessor capture marker requires a pointer field");
        return nullptr;
      }
      // Accessors can alias across read and write modes. Keep their views in
      // UAV state instead of creating incompatible SRV/UAV aliases.
      ReadOnly[I] = false;
    }
    FieldOffsets.push_back(*Offset);
    FieldSizes.push_back(*Size);
    FieldTypes.push_back(Ty);
    End = *Offset + *Size;
  }
  bool ScalarizedCopies = false;
  for (Instruction &I : make_early_inc_range(instructions(Kernel))) {
    auto *Copy = dyn_cast<MemTransferInst>(&I);
    if (!Copy)
      continue;
    int64_t SourceOffset = 0;
    if (GetPointerBaseWithConstantOffset(Copy->getSource(), SourceOffset,
                                         Layout) != Kernel.getArg(0))
      continue;
    auto *Length = dyn_cast<ConstantInt>(Copy->getLength());
    if (Copy->isVolatile() || !Length || SourceOffset < 0 ||
        uint64_t(SourceOffset) > *Bytes ||
        Length->getLimitedValue() > *Bytes - uint64_t(SourceOffset)) {
      fail("captured aggregate copies require a bounded constant size");
      return nullptr;
    }
    SmallVector<unsigned> CopiedFields;
    uint64_t CopyEnd = SourceOffset + Length->getZExtValue();
    for (uint64_t Offset = SourceOffset; Offset < CopyEnd;) {
      auto It = llvm::find(FieldOffsets, Offset);
      unsigned Field = It - FieldOffsets.begin();
      if (It == FieldOffsets.end() ||
          (!FieldTypes[Field]->isIntegerTy(32) &&
           !FieldTypes[Field]->isIntegerTy(64) &&
           !FieldTypes[Field]->isFloatTy()) ||
          FieldSizes[Field] != Layout.getTypeStoreSize(FieldTypes[Field]) ||
          FieldSizes[Field] > CopyEnd - Offset ||
          commonAlignment(Copy->getDestAlign().valueOrOne(),
                          Offset - SourceOffset) < Align(FieldSizes[Field])) {
        fail("captured aggregate copies must cover complete aligned scalar "
             "fields");
        return nullptr;
      }
      CopiedFields.push_back(Field);
      Offset += FieldSizes[Field];
    }
    IRBuilder<> Builder(Copy);
    SmallVector<Value *> Values;
    for (unsigned Field : CopiedFields) {
      uint64_t Offset = FieldOffsets[Field] - SourceOffset;
      Value *Source = Builder.CreateConstGEP1_64(Builder.getInt8Ty(),
                                                 Copy->getSource(), Offset);
      Values.push_back(Builder.CreateAlignedLoad(
          FieldTypes[Field], Source,
          commonAlignment(Copy->getSourceAlign().valueOrOne(), Offset)));
    }
    // Load every field first to preserve memmove's overlapping-copy semantics.
    for (auto [Field, FieldValue] : zip(CopiedFields, Values)) {
      uint64_t Offset = FieldOffsets[Field] - SourceOffset;
      Value *Dest = Builder.CreateConstGEP1_64(Builder.getInt8Ty(),
                                               Copy->getDest(), Offset);
      Builder.CreateAlignedStore(
          FieldValue, Dest,
          commonAlignment(Copy->getDestAlign().valueOrOne(), Offset));
    }
    Copy->eraseFromParent();
    ScalarizedCopies = true;
  }
  if (ScalarizedCopies) {
    legacy::FunctionPassManager Passes(Kernel.getParent());
    Passes.add(createSROAPass());
    Passes.doInitialization();
    Passes.run(Kernel);
    Passes.doFinalization();
  }
  SmallVector<GetElementPtrInst *> DynamicAddresses;
  for (Instruction &I : instructions(Kernel))
    if (auto *GEP = dyn_cast<GetElementPtrInst>(&I)) {
      int64_t Base = 0;
      APInt Offset(Layout.getIndexSizeInBits(GEP->getPointerAddressSpace()), 0);
      if (GetPointerBaseWithConstantOffset(GEP->getPointerOperand(), Base,
                                           Layout) == Kernel.getArg(0) &&
          !GEP->accumulateConstantOffset(Layout, Offset))
        DynamicAddresses.push_back(GEP);
    }
  DenseMap<std::pair<uint32_t, ArrayType *>, AllocaInst *> ArrayStorage;
  for (auto *GEP : DynamicAddresses) {
    unsigned Bits = Layout.getIndexSizeInBits(GEP->getPointerAddressSpace());
    APInt Offset(Bits, 0);
    SmallMapVector<Value *, APInt, 4> Variables;
    int64_t Base = 0;
    GetPointerBaseWithConstantOffset(GEP->getPointerOperand(), Base, Layout);
    if (GEP->getPointerAddressSpace() != 0 ||
        !GEP->collectOffset(Layout, Bits, Variables, Offset)) {
      fail("dynamic capture arrays require default-address-space indexing");
      return nullptr;
    }
    Offset += APInt(Bits, Base);
    std::optional<std::pair<uint32_t, ArrayType *>> Array;
    uint64_t ArrayBytes = UINT64_MAX;
    // Materialize only the enclosing array needed by these indexing strides.
    for (auto Candidate : CaptureArrays) {
      uint64_t Bytes = Layout.getTypeAllocSize(Candidate.second);
      if (Offset.isNegative() || Offset.getLimitedValue() < Candidate.first ||
          Offset.getLimitedValue() - Candidate.first >= Bytes ||
          llvm::any_of(Variables, [&](const auto &Variable) {
            uint64_t Scale = Variable.second.getLimitedValue();
            return Scale > Bytes ||
                   (Scale == Bytes && Candidate.second->getNumElements() != 1);
          }))
        continue;
      if (Bytes < ArrayBytes) {
        Array = Candidate;
        ArrayBytes = Bytes;
      }
    }
    if (!Array) {
      fail("dynamic capture indexing requires a fixed-size scalar array");
      return nullptr;
    }
    SmallVector<Value *> ArrayPointers{GEP};
    SmallPtrSet<Value *, 16> SeenArrayPointers;
    while (!ArrayPointers.empty()) {
      Value *Pointer = ArrayPointers.pop_back_val();
      if (!SeenArrayPointers.insert(Pointer).second)
        continue;
      for (User *Use : Pointer->users()) {
        if (isa<GetElementPtrInst, AddrSpaceCastInst>(Use)) {
          ArrayPointers.push_back(Use);
          continue;
        }
        auto *Load = dyn_cast<LoadInst>(Use);
        if (!Load || Load->isAtomic() || Load->isVolatile() ||
            Load->getType()->isPointerTy()) {
          fail("dynamic capture arrays may only be indexed and read");
          return nullptr;
        }
      }
    }
    AllocaInst *&Storage = ArrayStorage[*Array];
    if (!Storage) {
      IRBuilder<> Builder(&*Kernel.getEntryBlock().getFirstInsertionPt());
      Storage = Builder.CreateAlloca(Array->second, nullptr, "capture.array");
      uint64_t End = Array->first + ArrayBytes;
      for (uint64_t FieldOffset = Array->first; FieldOffset < End;) {
        auto It = llvm::find(FieldOffsets, FieldOffset);
        unsigned Field = It - FieldOffsets.begin();
        if (It == FieldOffsets.end() ||
            (!FieldTypes[Field]->isIntegerTy(8) &&
             !FieldTypes[Field]->isIntegerTy(16) &&
             !FieldTypes[Field]->isIntegerTy(32) &&
             !FieldTypes[Field]->isIntegerTy(64) &&
             !FieldTypes[Field]->isFloatTy()) ||
            FieldSizes[Field] != Layout.getTypeStoreSize(FieldTypes[Field]) ||
            FieldSizes[Field] > End - FieldOffset) {
          fail("dynamic capture arrays require contiguous scalar fields");
          return nullptr;
        }
        Value *Source = Builder.CreateConstGEP1_64(
            Builder.getInt8Ty(), Kernel.getArg(0), FieldOffset);
        Value *FieldValue = Builder.CreateLoad(FieldTypes[Field], Source);
        Value *Dest = Builder.CreateConstGEP1_64(Builder.getInt8Ty(), Storage,
                                                 FieldOffset - Array->first);
        Builder.CreateStore(FieldValue, Dest);
        FieldOffset += FieldSizes[Field];
      }
    }
    IRBuilder<> Builder(GEP);
    Value *Index = ConstantInt::get(Kernel.getContext(),
                                    Offset - APInt(Bits, Array->first));
    for (auto &[Variable, Scale] : Variables)
      Index = Builder.CreateAdd(
          Index,
          Builder.CreateMul(
              Builder.CreateSExtOrTrunc(Variable, Builder.getIntNTy(Bits)),
              ConstantInt::get(Kernel.getContext(), Scale)));
    Value *Address = Builder.CreateGEP(Builder.getInt8Ty(), Storage, Index, "",
                                       GEP->getNoWrapFlags());
    cast<Instruction>(Address)->copyMetadata(*GEP);
    GEP->replaceAllUsesWith(Address);
    GEP->eraseFromParent();
  }
  SmallVector<Value *> Worklist{Kernel.getArg(0)};
  for (Instruction &I : make_early_inc_range(instructions(Kernel)))
    if (auto *Annotation = dyn_cast<IntrinsicInst>(&I))
      if (!removePointerAnnotation(*Annotation, "sycl.accessor.pointer"))
        removePointerAnnotation(*Annotation, "sycl.local.pointer");
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
      Type *Ty = Load->getType();
      if (Ty->isPointerTy()) {
        unsigned AS = Ty->getPointerAddressSpace();
        if ((!LocalAlignments[Field] && AS != 0 && AS != 4) ||
            FieldSizes[Field] != 8 || Offset % 8) {
          fail("captured buffers require 64-bit default or generic pointers");
          return nullptr;
        }
      } else if ((!Ty->isIntegerTy(8) && !Ty->isIntegerTy(16) &&
                  !Ty->isIntegerTy(32) && !Ty->isIntegerTy(64) &&
                  !Ty->isFloatTy()) ||
                 FieldSizes[Field] != Layout.getTypeStoreSize(Ty) ||
                 Offset % FieldSizes[Field]) {
        fail("captures must be buffer pointers, 8/16/32/64-bit integers, or "
             "floats");
        return nullptr;
      }
      Loads[Field].push_back(Load);
      if (LocalAlignments[Field])
        continue;
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
            else if (isa<GetElementPtrInst, AddrSpaceCastInst>(Use))
              Pointers.push_back(Use);
        }
      }
    }
  }
  SmallVector<unsigned> UsedFields;
  json::Array Locals;
  DenseMap<uint32_t, std::pair<uint64_t, GlobalVariable *>> Allocations;
  unsigned LocalIndex = 0;
  uint64_t LocalBytes = 0;
  for (unsigned I = 0; I < NumFields; ++I) {
    if (!LocalAlignments[I] || Loads[I].empty())
      continue;
    uint64_t MinimumBytes = LocalAlignments[I];
    for (auto *Load : Loads[I])
      if (!checkLocalPointer(Load, Layout, MinimumBytes))
        return nullptr;
    if (MinimumBytes > 32768) {
      fail("local accessor constant accesses exceed the 32768-byte limit");
      return nullptr;
    }
    // The packaged image is never dispatched for local-bearing kernels, but
    // constant accesses must still be in bounds for DXIL validation.
    uint64_t Token = (uint64_t(LocalIndex + 1) << 32) |
                     alignTo(MinimumBytes, LocalAlignments[I]);
    if (!LocalAllocations.empty()) {
      if (LocalIndex >= LocalAllocations.size()) {
        fail("local allocation count does not match captured slots");
        return nullptr;
      }
      Token = LocalAllocations[LocalIndex];
    }
    ++LocalIndex;
    uint32_t Identity = Token >> 32;
    uint32_t Bytes = uint32_t(Token);
    if (!Identity || !Bytes || Bytes > 32768 || Bytes % LocalAlignments[I]) {
      fail("local allocations require an identity and aligned nonzero bytes "
           "within the 32768-byte workgroup limit");
      return nullptr;
    }
    auto &Allocation = Allocations[Identity];
    if (Allocation.second &&
        (Allocation.first != Token ||
         Allocation.second->getAlign() != Align(LocalAlignments[I]))) {
      fail("aliased local allocation sizes and alignments must match");
      return nullptr;
    }
    if (!Allocation.second) {
      LocalBytes = alignTo(LocalBytes, LocalAlignments[I]) + Bytes;
      if (LocalBytes > 32768) {
        fail("local allocations exceed the 32768-byte workgroup limit");
        return nullptr;
      }
      auto *Ty =
          ArrayType::get(Type::getInt32Ty(Kernel.getContext()), Bytes / 4);
      auto *Global = new GlobalVariable(
          *Kernel.getParent(), Ty, false, GlobalValue::InternalLinkage,
          PoisonValue::get(Ty), "sycl.local", nullptr,
          GlobalValue::NotThreadLocal, 3);
      Global->setAlignment(Align(LocalAlignments[I]));
      Allocation = {Token, Global};
    }
    if (Bytes < MinimumBytes) {
      fail("local accessor constant accesses exceed the requested allocation");
      return nullptr;
    }
    for (auto *Load : Loads[I]) {
      IRBuilder<> Builder(Load);
      Value *Base = Builder.CreateInBoundsGEP(
          Allocation.second->getValueType(), Allocation.second,
          {Builder.getInt32(0), Builder.getInt32(0)});
      replaceLocalPointer(Load, Base, Layout);
      Load->eraseFromParent();
    }
    Loads[I].clear();
    Locals.push_back(json::Object{{"offset", FieldOffsets[I]},
                                  {"alignment", LocalAlignments[I]},
                                  {"allocation", int64_t(Token)}});
  }
  if (!LocalAllocations.empty() && LocalAllocations.size() != LocalIndex) {
    fail("local allocation count does not match captured slots");
    return nullptr;
  }
  bool HasNarrowIntegers = false;
  for (unsigned I = 0; I < NumFields; ++I) {
    if (Loads[I].empty())
      continue;
    UsedFields.push_back(I);
    Type *Ty = FieldTypes[I];
    if (Ty->isIntegerTy(8) || Ty->isIntegerTy(16)) {
      HasNarrowIntegers = true;
      Ty = Type::getInt32Ty(Kernel.getContext());
    }
    Types.push_back(Ty->isPointerTy() ? PointerType::get(Kernel.getContext(), 0)
                                      : Ty);
    Offsets.push_back(FieldOffsets[I]);
    Sizes.push_back(FieldSizes[I]);
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
    unsigned Field = UsedFields[Index];
    if (Arg.getType()->isPointerTy() && ReadOnly[Field])
      Arg.addAttr(Attribute::get(Kernel.getContext(), "dx.readonly"));
    for (LoadInst *Load : Loads[Field]) {
      if (Load->getType()->isPointerTy())
        replaceBufferPointer(Load, &Arg);
      else {
        IRBuilder<> Builder(Load);
        Value *Replacement = &Arg;
        if (Arg.getType() != Load->getType())
          Replacement = Builder.CreateTrunc(&Arg, Load->getType());
        Load->replaceAllUsesWith(Replacement);
      }
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
  if (HasNarrowIntegers)
    Capture["sizes"] = std::move(Sizes);
  if (!Locals.empty())
    Capture["local"] = std::move(Locals);
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
  for (StringRef Name : {"llvm.global_ctors", "llvm.global_dtors"})
    if (const auto *Initializers = M.getGlobalVariable(Name);
        Initializers && Initializers->hasInitializer() &&
        !Initializers->getInitializer()->isNullValue()) {
      fail("dynamic device initialization and destruction are not supported");
      return false;
    }
  if (!Kernel->getReturnType()->isVoidTy() || Kernel->isVarArg() ||
      !Kernel->use_empty()) {
    fail("kernel must return void and cannot be called or variadic");
    return false;
  }
  if (M.getTargetTriple().getEnvironment() != Triple::Compute) {
    fail("a compute shader target is required");
    return false;
  }
  if (!WorkgroupSize.empty()) {
    if (!Kernel->hasFnAttribute(
            offloading::directx::WorkgroupSpecializableAttr)) {
      fail("kernel has a fixed workgroup size");
      return false;
    }
    if (WorkgroupSize.size() != 3) {
      fail("specialization requires three workgroup dimensions");
      return false;
    }
    Kernel->addFnAttr("hlsl.numthreads",
                      std::to_string(WorkgroupSize[0]) + "," +
                          std::to_string(WorkgroupSize[1]) + "," +
                          std::to_string(WorkgroupSize[2]));
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
  SmallVector<unsigned> ScalarOffsets;
  unsigned ScalarBytes = 0;
  auto AddScalar = [&](Type *Ty) {
    unsigned Size = Ty->isIntegerTy(64) ? 8 : 4;
    ScalarBytes = alignTo(ScalarBytes, Size);
    ScalarOffsets.push_back(ScalarBytes);
    ScalarTypes.push_back(Ty);
    ScalarBytes += Size;
  };
  for (Argument &Arg : Kernel->args()) {
    Type *Ty = Arg.getType();
    if (Ty->isIntegerTy(32) || Ty->isIntegerTy(64) || Ty->isFloatTy()) {
      if (Arg.use_empty()) {
        fail("unused scalar arguments are not supported");
        return false;
      }
      AddScalar(Ty);
      continue;
    }
    if (!Ty->isPointerTy() || Ty->getPointerAddressSpace() != 0) {
      fail("arguments must be default-address-space buffer pointers or "
           "32/64-bit integer or float scalars");
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
  unsigned DispatchOffset = ScalarBytes;
  if (!DispatchQueries.empty())
    for (unsigned I = 0; I < DispatchWords; ++I)
      AddScalar(Type::getInt32Ty(M.getContext()));
  if (ScalarBytes > 252) {
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
  Entry->removeFnAttr(offloading::directx::WorkgroupSpecializableAttr);
  Entry->removeFnAttr(offloading::directx::LocalSpecializableAttr);
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
    Type *ResourceLayout = Layout;
    if (llvm::any_of(ScalarTypes,
                     [](Type *Ty) { return Ty->isIntegerTy(64); })) {
      SmallVector<unsigned> LayoutParameters{ScalarBytes};
      llvm::append_range(LayoutParameters, ScalarOffsets);
      ResourceLayout = TargetExtType::get(M.getContext(), "dx.Layout", {Layout},
                                          LayoutParameters);
    }
    ConstantHandleTy =
        TargetExtType::get(M.getContext(), "dx.CBuffer", {ResourceLayout});
    ConstantHandle = Builder.CreateIntrinsic(
        ConstantHandleTy, Intrinsic::dx_resource_handlefrombinding,
        {Builder.getInt32(0), Builder.getInt32(0), Builder.getInt32(1),
         Builder.getInt32(0), Builder.CreateGlobalString(Layout->getName())});
    unsigned Bytes = ScalarBytes;
    SmallVector<Metadata *> Fields{
        ConstantAsMetadata::get(Builder.getInt32(Bytes))};
    for (auto [Index, Ty] : enumerate(ScalarTypes))
      Fields.push_back(MDNode::get(
          M.getContext(),
          {ConstantAsMetadata::get(Builder.getInt32(3)),
           ConstantAsMetadata::get(Builder.getInt32(ScalarOffsets[Index])),
           ConstantAsMetadata::get(Builder.getInt32(7)),
           ConstantAsMetadata::get(Builder.getInt32(Ty->isFloatTy() ? 9
                                                    : Ty->isIntegerTy(64)
                                                        ? 6
                                                        : 4))}));
    M.getOrInsertNamedMetadata("dx.typeAnnotations")
        ->addOperand(MDNode::get(
            M.getContext(),
            {ConstantAsMetadata::get(Builder.getInt32(0)),
             ConstantAsMetadata::get(ConstantAggregateZero::get(Layout)),
             MDNode::get(M.getContext(), Fields)}));
    Constants = json::Object{{"register", 0}, {"space", 0}, {"size", Bytes}};
  }
  unsigned ScalarIndex = 0;
  SmallVector<std::pair<Value *, Value *>> BufferPointers;
  for (Argument &Arg : Kernel->args()) {
    if (!Arg.getType()->isPointerTy()) {
      unsigned ScalarOffset = ScalarOffsets[ScalarIndex++];
      unsigned Size = Arg.getType()->isIntegerTy(64) ? 8 : 4;
      auto *Pointer = Builder.CreateIntrinsic(
          Intrinsic::dx_resource_getpointer,
          {Builder.getPtrTy(2), ConstantHandleTy, Builder.getInt32Ty()},
          {ConstantHandle, Builder.getInt32(ScalarOffset)}, {}, "",
          PointerBundles);
      Arg.replaceAllUsesWith(Builder.CreateLoad(Arg.getType(), Pointer));
      Arguments.push_back(json::Object{{"index", Arg.getArgNo()},
                                       {"kind", "scalar"},
                                       {"size", Size},
                                       {"offset", ScalarOffset}});
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
    BufferPointers.emplace_back(&Arg, Pointer);
    Arguments.push_back(json::Object{{"index", Arg.getArgNo()},
                                     {"kind", ReadOnly ? "srv" : "uav"},
                                     {"register", Register},
                                     {"space", 0},
                                     {"min_size", 4}});
  }
  // Remapping can erase the builder's original insertion point.
  for (auto [Argument, Pointer] : BufferPointers)
    replaceBufferPointer(Argument, Pointer);
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
  // Executable DXIL uses its native ABI, not the capture's host language ABI.
  if (M.getModuleFlag("wchar_size"))
    M.setModuleFlag(Module::Error, "wchar_size", 4);
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
