//===-- DirectXTypeMap.cpp - DirectX Type Map -------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the implementation of the DirectXTypeMap class,
// which is used to maintain  type information mapping to MIR registers
// after lowering from LLVM IR to MIR.
//
//===----------------------------------------------------------------------===//

#include "DirectXTypeMap.h"

using namespace llvm;

std::once_flag DirectXTypeMap::InitFlag;
DirectXTypeMap *DirectXTypeMap::Instance = nullptr;
std::mutex DirectXTypeMap::Mutex;