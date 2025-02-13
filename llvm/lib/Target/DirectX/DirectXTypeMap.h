//===-- DirectXTypeMap.h - DirectX Type Map ---------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// DirectXTypeMap is used to maintain llvm::Type information for MIR registers
// after lowering from LLVM IR to MIR. It maps llvm::Type to a virtual register.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DIRECTX_TYPE_MAP_H
#define LLVM_DIRECTX_TYPE_MAP_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/CodeGen/GlobalISel/MachineIRBuilder.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/IR/Type.h"

#include <mutex>

namespace llvm {

class DirectXTypeMap {
private:
  DenseMap<const MachineFunction *, DenseMap<Register, Type *>> VRegToTypeMap;
  static std::once_flag InitFlag;
  static DirectXTypeMap *Instance;
  static std::mutex Mutex;

  DirectXTypeMap() = default;
  DirectXTypeMap(const DirectXTypeMap &) = delete;
  DirectXTypeMap &operator=(const DirectXTypeMap &) = delete;

public:
  static DirectXTypeMap &getInstance() {
    std::call_once(InitFlag, []() { Instance = new DirectXTypeMap(); });
    return *Instance;
  }

  void setType(const MachineFunction &MF, Register Reg, Type &Ty) {
    std::lock_guard<std::mutex> Lock(Mutex);
    VRegToTypeMap[&MF][Reg] = &Ty;
  }

  Type *getType(const MachineFunction &MF, Register Reg) {
    std::lock_guard<std::mutex> Lock(Mutex);
    auto MFIt = VRegToTypeMap.find(&MF);
    if (MFIt != VRegToTypeMap.end()) {
      auto RegIt = MFIt->second.find(Reg);
      if (RegIt != MFIt->second.end()) {
        return RegIt->second;
      }
    }
    return nullptr;
  }
};

} // namespace llvm

#endif // LLVM_DIRECTX_TYPE_MAP_H
