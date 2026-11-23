//===--- BuiltinsDirectX.td - DirectX Builtin function database -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

include "clang/Basic/BuiltinsBase.td"

foreach Name = ["thread_id", "group_id", "group_size", "num_groups"] in {
  def : Builtin {
    let Spellings = ["__builtin_dx_" # Name];
    let Attributes = [NoThrow, Const];
    let Prototype = "unsigned int(_Constant int)";
  }
}

def DxGlobalOffset : Builtin {
  let Spellings = ["__builtin_dx_global_offset"];
  let Attributes = [NoThrow, Const];
  let Prototype = "unsigned long long int(_Constant int)";
}

def DxSyncThreads : Builtin {
  let Spellings = ["__builtin_dx_sync_threads"];
  let Attributes = [NoThrow];
  let Prototype = "void()";
}

def DxSyncThreadsAndMemory : Builtin {
  let Spellings = ["__builtin_dx_sync_threads_and_memory"];
  let Attributes = [NoThrow];
  let Prototype = "void()";
}

def DxDot2Add : Builtin { 
  let Spellings = ["__builtin_dx_dot2add"];
  let Attributes = [NoThrow, Const];
  let Prototype = "float(_ExtVector<2, _Float16>, _ExtVector<2, _Float16>, float)";
}
