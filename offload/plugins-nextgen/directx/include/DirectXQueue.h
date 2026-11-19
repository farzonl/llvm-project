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

#include <algorithm>
#include <cstdint>
#include <limits>
#include <mutex>
#include <utility>
#include <vector>

namespace llvm::omp::target::plugin {
struct PluginContextTy;
}

namespace llvm::omp::target::plugin::directx {

inline Error check(HRESULT Result, const char *Operation) {
  if (SUCCEEDED(Result))
    return Error::success();
  return error::createOffloadError(error::ErrorCode::BACKEND_FAILURE,
                                   "%s failed (HRESULT 0x%08x)", Operation,
                                   static_cast<uint32_t>(Result));
}

struct DirectXBufferTy {
  Microsoft::WRL::ComPtr<ID3D12Resource> Resource;
  uint64_t Offset = 0;
  D3D12_RESOURCE_STATES State = D3D12_RESOURCE_STATE_COMMON;
};

inline Expected<DirectXBufferTy>
getBufferFromAddress(Microsoft::WRL::ComPtr<ID3D12Resource> Resource,
                     uint64_t Address, uint64_t Size) {
  uint64_t Base = Resource->GetGPUVirtualAddress();
  uint64_t Width = Resource->GetDesc().Width;
  if (Address < Base || Address - Base > Width ||
      Size > Width - (Address - Base))
    return error::createOffloadError(
        error::ErrorCode::BACKEND_FAILURE,
        "DirectX allocation exceeds its backing resource");
  return DirectXBufferTy{std::move(Resource), Address - Base};
}

struct DirectXQueueTy {
  struct SubmissionTy {
    uint64_t Value;
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> Allocator;
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> List;
    Microsoft::WRL::ComPtr<ID3D12Resource> Source;
    Microsoft::WRL::ComPtr<ID3D12Resource> Destination;
  };

  Microsoft::WRL::ComPtr<ID3D12Device> Device;
  Microsoft::WRL::ComPtr<ID3D12CommandQueue> Queue;
  Microsoft::WRL::ComPtr<ID3D12Fence> Fence;
  std::mutex Mutex;
  uint64_t LastSignaledValue = 0;
  PluginContextTy *Context;
  std::vector<SubmissionTy> Submissions;

  explicit DirectXQueueTy(ID3D12Device *Device,
                          PluginContextTy *Context = nullptr)
      : Device(Device), Context(Context) {}

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
    reclaim(*Completed);
    return !Submissions.empty() || *Completed < LastSignaledValue;
  }

  Expected<uint64_t> nextValue() {
    if (LastSignaledValue == std::numeric_limits<uint64_t>::max() - 1)
      return error::createOffloadError(error::ErrorCode::OUT_OF_RESOURCES,
                                       "DirectX fence timeline is exhausted");
    return LastSignaledValue + 1;
  }

  Error signalImpl(uint64_t Value) {
    if (auto Err = check(Queue->Signal(Fence.Get(), Value),
                         "ID3D12CommandQueue::Signal"))
      return Err;
    LastSignaledValue = Value;
    return Error::success();
  }

  Expected<uint64_t> signal() {
    std::lock_guard<std::mutex> Lock(Mutex);
    auto Value = nextValue();
    if (!Value)
      return Value.takeError();
    if (auto Err = signalImpl(*Value))
      return std::move(Err);
    return *Value;
  }

  void reclaim(uint64_t Completed) {
    Submissions.erase(std::remove_if(Submissions.begin(), Submissions.end(),
                                     [=](const SubmissionTy &S) {
                                       return S.Value <= Completed;
                                     }),
                      Submissions.end());
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
    std::lock_guard<std::mutex> Lock(Mutex);
    reclaim(*Completed);
    return Error::success();
  }

  Error copy(const DirectXBufferTy &Dst, const DirectXBufferTy &Src,
             uint64_t Size) {
    SubmissionTy Submission{};
    Submission.Source = Src.Resource;
    Submission.Destination = Dst.Resource;
    if (auto Err = check(
            Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_COMPUTE,
                                           IID_PPV_ARGS(&Submission.Allocator)),
            "ID3D12Device::CreateCommandAllocator"))
      return Err;
    if (auto Err =
            check(Device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_COMPUTE,
                                            Submission.Allocator.Get(), nullptr,
                                            IID_PPV_ARGS(&Submission.List)),
                  "ID3D12Device::CreateCommandList"))
      return Err;

    D3D12_RESOURCE_BARRIER Barriers[2]{};
    unsigned Count = 0;
    auto Transition = [&](const DirectXBufferTy &Buffer,
                          D3D12_RESOURCE_STATES State) {
      if (Buffer.State != D3D12_RESOURCE_STATE_COMMON)
        return;
      auto &Barrier = Barriers[Count++];
      Barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
      Barrier.Transition.pResource = Buffer.Resource.Get();
      Barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
      Barrier.Transition.StateBefore = Buffer.State;
      Barrier.Transition.StateAfter = State;
    };
    Transition(Dst, D3D12_RESOURCE_STATE_COPY_DEST);
    Transition(Src, D3D12_RESOURCE_STATE_COPY_SOURCE);
    if (Count)
      Submission.List->ResourceBarrier(Count, Barriers);
    Submission.List->CopyBufferRegion(Dst.Resource.Get(), Dst.Offset,
                                      Src.Resource.Get(), Src.Offset, Size);
    for (unsigned I = 0; I < Count; ++I)
      std::swap(Barriers[I].Transition.StateBefore,
                Barriers[I].Transition.StateAfter);
    if (Count)
      Submission.List->ResourceBarrier(Count, Barriers);
    if (auto Err =
            check(Submission.List->Close(), "ID3D12GraphicsCommandList::Close"))
      return Err;

    uint64_t Value;
    {
      std::lock_guard<std::mutex> Lock(Mutex);
      auto Next = nextValue();
      if (!Next)
        return Next.takeError();
      Value = *Next;
      Submission.Value = Value;
      ID3D12CommandList *Lists[] = {Submission.List.Get()};
      // Retain every GPU dependency even if signaling or waiting fails.
      Submissions.push_back(std::move(Submission));
      Queue->ExecuteCommandLists(1, Lists);
      if (auto Err = signalImpl(Value))
        return Err;
    }
    return wait(Value);
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
