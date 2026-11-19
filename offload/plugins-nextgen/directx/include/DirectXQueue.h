//===- DirectXQueue.h - DirectX command queues and fences --------*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef OMPTARGET_PLUGINS_DIRECTX_QUEUE_H
#define OMPTARGET_PLUGINS_DIRECTX_QUEUE_H

#include "OffloadError.h"

#ifdef _WIN32
#include <windows.h>

#include <initguid.h>

#include <d3d12.h>
#include <dxcore_interface.h>
#include <wrl/client.h>
#else
#define INITGUID
#include <wsl/winadapter.h>

#include <directx/d3d12.h>
#include <directx/dxcore_interface.h>
#include <dxguids/dxguids.h>
#include <wsl/wrladapter.h>
#endif

#include <cstdint>
#include <limits>
#include <mutex>

namespace llvm::omp::target::plugin::directx {

inline Error check(HRESULT Result, const char *Operation) {
  if (SUCCEEDED(Result))
    return Error::success();
  return error::createOffloadError(error::ErrorCode::BACKEND_FAILURE,
                                   "%s failed (HRESULT 0x%08x)", Operation,
                                   static_cast<uint32_t>(Result));
}

struct DirectXQueueTy {
  Microsoft::WRL::ComPtr<ID3D12Device> Device;
  Microsoft::WRL::ComPtr<ID3D12CommandQueue> Queue;
  Microsoft::WRL::ComPtr<ID3D12Fence> Fence;
  std::mutex Mutex;
  uint64_t LastSignaledValue = 0;

  explicit DirectXQueueTy(ID3D12Device *Device) : Device(Device) {}

  Error init() {
    D3D12_COMMAND_QUEUE_DESC Desc{};
    Desc.Type = D3D12_COMMAND_LIST_TYPE_COMPUTE;
    Desc.NodeMask = 1;
    if (auto Err =
            check(Device->CreateCommandQueue(&Desc, IID_PPV_ARGS(&Queue)),
                  "ID3D12Device::CreateCommandQueue"))
      return Err;
    return check(
        Device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&Fence)),
        "ID3D12Device::CreateFence");
  }

  Expected<uint64_t> getCompletedValue() {
    uint64_t Completed = Fence->GetCompletedValue();
    if (Completed == std::numeric_limits<uint64_t>::max())
      return error::createOffloadError(
          error::ErrorCode::BACKEND_FAILURE,
          "DirectX fence reports device removal (HRESULT 0x%08x)",
          static_cast<uint32_t>(Device->GetDeviceRemovedReason()));
    return Completed;
  }

  Expected<bool> hasPendingWork() {
    std::lock_guard<std::mutex> Lock(Mutex);
    auto Completed = getCompletedValue();
    if (!Completed)
      return Completed.takeError();
    return *Completed < LastSignaledValue;
  }

  Expected<uint64_t> signal() {
    std::lock_guard<std::mutex> Lock(Mutex);
    if (LastSignaledValue == std::numeric_limits<uint64_t>::max() - 1)
      return error::createOffloadError(error::ErrorCode::OUT_OF_RESOURCES,
                                       "DirectX fence timeline is exhausted");
    uint64_t Value = LastSignaledValue + 1;
    if (auto Err = check(Queue->Signal(Fence.Get(), Value),
                         "ID3D12CommandQueue::Signal"))
      return std::move(Err);
    LastSignaledValue = Value;
    return Value;
  }

  Error wait(uint64_t Value) {
    // A null event requests a blocking wait, on both Windows and Nacelle.
    if (auto Err = check(Fence->SetEventOnCompletion(Value, nullptr),
                         "ID3D12Fence::SetEventOnCompletion"))
      return Err;
    auto Completed = getCompletedValue();
    if (!Completed)
      return Completed.takeError();
    if (*Completed < Value)
      return error::createOffloadError(
          error::ErrorCode::BACKEND_FAILURE,
          "DirectX fence wait returned before completion");
    return Error::success();
  }

  Error synchronize() {
    auto Value = signal();
    if (!Value)
      return Value.takeError();
    return wait(*Value);
  }
};

} // namespace llvm::omp::target::plugin::directx

#endif
