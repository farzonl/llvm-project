//===- DirectXGuids.cpp - DirectX SDK GUID definitions
//---------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifdef _WIN32
#include <windows.h>

#include <initguid.h>
#else
#define INITGUID
#endif

#include "DirectXQueue.h"
