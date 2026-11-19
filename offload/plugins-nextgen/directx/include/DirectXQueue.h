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

#include <d3d12.h>
#include <dxcore_interface.h>
#include <wrl/client.h>
#else
#include <wsl/winadapter.h>

#include <directx/d3d12.h>
#include <directx/dxcore_interface.h>
#include <dxguids/dxguids.h>
#include <wsl/wrladapter.h>
#endif

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>

namespace llvm::omp::target::plugin {
struct PluginContextTy;
}

namespace llvm::omp::target::plugin::directx {

struct DirectXArgumentBindingsTy;

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
  struct FailureTy {
    std::mutex Mutex;
    std::string Message;

    Error get() {
      std::lock_guard<std::mutex> Lock(Mutex);
      if (Message.empty())
        return Error::success();
      return error::createOffloadError(error::ErrorCode::BACKEND_FAILURE, "%s",
                                       Message.c_str());
    }

    void set(std::string Text) {
      std::lock_guard<std::mutex> Lock(Mutex);
      Message = std::move(Text);
    }
  };

  struct SubmissionTy {
    uint64_t Value;
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> Allocator;
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> List;
    Microsoft::WRL::ComPtr<ID3D12Resource> Source;
    Microsoft::WRL::ComPtr<ID3D12Resource> Destination;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> Pipeline;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> RootSignature;
    std::shared_ptr<DirectXArgumentBindingsTy> Arguments;
    std::function<Error()> Complete;
    Microsoft::WRL::ComPtr<ID3D12Fence> Dependency;
    std::shared_ptr<FailureTy> DependencyFailure;
    uint64_t DependencyValue = 0;
  };

  Microsoft::WRL::ComPtr<ID3D12Device> Device;
  Microsoft::WRL::ComPtr<ID3D12CommandQueue> Queue;
  Microsoft::WRL::ComPtr<ID3D12Fence> ExecutionFence;
  Microsoft::WRL::ComPtr<ID3D12Fence> Fence;
  std::mutex Mutex;
  uint64_t LastSignaledValue = 0;
  uint64_t LastHostValue = 0;
  PluginContextTy *Context;
  std::deque<SubmissionTy> Submissions;

private:
  std::mutex SubmissionMutex;
  std::condition_variable Changed;
  std::thread Worker;
  bool Stopping = false;
  std::shared_ptr<FailureTy> Failure = std::make_shared<FailureTy>();
  bool Failed = false;

  Error checkFailure() { return Failure->get(); }

  void fail(Error Err) {
    Failure->set(toString(std::move(Err)));
    Failed = true;
    Changed.notify_all();
  }

  void retire() {
    std::unique_lock<std::mutex> Lock(Mutex);
    for (;;) {
      Changed.wait(Lock, [&] { return Stopping || !Submissions.empty(); });
      if (Failed || (Stopping && Submissions.empty()))
        return;
      auto &Front = Submissions.front();
      if (Front.Dependency &&
          Front.Dependency->GetCompletedValue() < Front.DependencyValue) {
        if (auto Err = Front.DependencyFailure->get()) {
          fail(std::move(Err));
          return;
        }
      }
      auto Completed = getCompletedValue(ExecutionFence.Get());
      if (!Completed) {
        fail(Completed.takeError());
        return;
      }
      if (*Completed < Submissions.front().Value) {
        Changed.wait_for(Lock, std::chrono::milliseconds(1));
        continue;
      }
      auto &Submission = Submissions.front();
      if (Submission.Complete) {
        auto Complete = std::move(Submission.Complete);
        Lock.unlock();
        auto Err = Complete();
        Lock.lock();
        if (Err) {
          fail(std::move(Err));
          return;
        }
        if (Failed)
          return;
      }
      // Publish completion only after host readbacks have become visible.
      if (auto Err =
              check(Fence->Signal(Submission.Value), "ID3D12Fence::Signal")) {
        fail(std::move(Err));
        return;
      }
      Submissions.pop_front();
      Changed.notify_all();
    }
  }

  Expected<uint64_t> enqueue(SubmissionTy Submission) {
    if (auto Err = checkFailure())
      return std::move(Err);
    auto Value = nextValue();
    if (!Value)
      return Value.takeError();
    Submission.Value = *Value;
    if (Submission.Complete || Submission.Dependency)
      LastHostValue = *Value;
    Submissions.push_back(std::move(Submission));
    auto &Stored = Submissions.back();
    if (Stored.Dependency)
      if (auto Err = check(
              Queue->Wait(Stored.Dependency.Get(), Stored.DependencyValue),
              "ID3D12CommandQueue::Wait")) {
        fail(std::move(Err));
        return checkFailure();
      }
    if (Stored.List) {
      ID3D12CommandList *Lists[] = {Stored.List.Get()};
      Queue->ExecuteCommandLists(1, Lists);
    }
    // Keep ownership even when a submitted command list cannot be signaled.
    if (auto Err = signalImpl(*Value)) {
      fail(std::move(Err));
      return checkFailure();
    }
    Changed.notify_all();
    return *Value;
  }

public:
  explicit DirectXQueueTy(ID3D12Device *Device,
                          PluginContextTy *Context = nullptr)
      : Device(Device), Context(Context) {}

  ~DirectXQueueTy() {
    {
      std::lock_guard<std::mutex> Lock(Mutex);
      Stopping = true;
      Changed.notify_all();
    }
    if (Worker.joinable())
      Worker.join();
  }

  Error init() {
    D3D12_COMMAND_QUEUE_DESC Desc{};
    Desc.Type = D3D12_COMMAND_LIST_TYPE_COMPUTE;
    Desc.NodeMask = 1;
    if (auto Err =
            check(Device->CreateCommandQueue(&Desc, IID_PPV_ARGS(&Queue)),
                  "ID3D12Device::CreateCommandQueue"))
      return Err;
    if (auto Err = check(Device->CreateFence(0, D3D12_FENCE_FLAG_NONE,
                                             IID_PPV_ARGS(&ExecutionFence)),
                         "ID3D12Device::CreateFence"))
      return Err;
    if (auto Err = check(
            Device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&Fence)),
            "ID3D12Device::CreateFence"))
      return Err;
    Worker = std::thread([this] { retire(); });
    return Error::success();
  }

  Expected<uint64_t> getCompletedValue(ID3D12Fence *NativeFence = nullptr) {
    uint64_t Completed =
        (NativeFence ? NativeFence : Fence.Get())->GetCompletedValue();
    if (Completed == std::numeric_limits<uint64_t>::max())
      return error::createOffloadError(
          error::ErrorCode::BACKEND_FAILURE,
          "DirectX fence reports device removal (HRESULT 0x%08x)",
          static_cast<uint32_t>(Device->GetDeviceRemovedReason()));
    return Completed;
  }

  Expected<bool> hasPendingWork() {
    std::lock_guard<std::mutex> Lock(Mutex);
    if (auto Err = checkFailure())
      return std::move(Err);
    auto Completed = getCompletedValue();
    if (!Completed)
      return Completed.takeError();
    return !Submissions.empty() || *Completed < LastSignaledValue;
  }

  Expected<bool> isComplete(uint64_t Value) {
    std::lock_guard<std::mutex> Lock(Mutex);
    auto Completed = getCompletedValue();
    if (!Completed)
      return Completed.takeError();
    if (*Completed >= Value)
      return true;
    if (auto Err = checkFailure())
      return std::move(Err);
    return false;
  }

  Error enqueueWait(std::shared_ptr<DirectXQueueTy> Dependency,
                    uint64_t Value) {
    if (Dependency.get() == this)
      return Error::success();
    SubmissionTy Submission{};
    Submission.Dependency = Dependency->Fence;
    Submission.DependencyFailure = Dependency->Failure;
    Submission.DependencyValue = Value;
    std::lock_guard<std::mutex> SubmitLock(SubmissionMutex);
    std::lock_guard<std::mutex> Lock(Mutex);
    auto Result = enqueue(std::move(Submission));
    if (!Result)
      return Result.takeError();
    return Error::success();
  }

  Expected<uint64_t> nextValue() {
    if (LastSignaledValue == std::numeric_limits<uint64_t>::max() - 1)
      return error::createOffloadError(error::ErrorCode::OUT_OF_RESOURCES,
                                       "DirectX fence timeline is exhausted");
    return LastSignaledValue + 1;
  }

  Error signalImpl(uint64_t Value) {
    if (auto Err = check(Queue->Signal(ExecutionFence.Get(), Value),
                         "ID3D12CommandQueue::Signal"))
      return Err;
    LastSignaledValue = Value;
    return Error::success();
  }

  Expected<uint64_t> signal() {
    std::lock_guard<std::mutex> SubmitLock(SubmissionMutex);
    std::lock_guard<std::mutex> Lock(Mutex);
    return enqueue(SubmissionTy{});
  }

  Error enqueueHostCall(std::function<Error()> Complete, bool Blocking) {
    uint64_t Value;
    {
      std::lock_guard<std::mutex> SubmitLock(SubmissionMutex);
      std::unique_lock<std::mutex> Lock(Mutex);
      SubmissionTy Submission{};
      Submission.Complete = std::move(Complete);
      auto Next = enqueue(std::move(Submission));
      if (!Next)
        return Next.takeError();
      Value = *Next;
      // Some portable D3D12 implementations block the caller in Queue::Wait.
      // Let retirement publish host completion while submission stays ordered.
      Lock.unlock();
      auto Err = check(Queue->Wait(Fence.Get(), Value),
                       "ID3D12CommandQueue::Wait(host callback)");
      Lock.lock();
      if (Err) {
        fail(std::move(Err));
        return checkFailure();
      }
    }
    return Blocking ? wait(Value) : Error::success();
  }

  Error wait(uint64_t Value) {
    std::unique_lock<std::mutex> Lock(Mutex);
    for (;;) {
      auto Completed = getCompletedValue();
      if (!Completed)
        return Completed.takeError();
      if (*Completed >= Value)
        return Error::success();
      if (auto Err = checkFailure())
        return Err;
      Changed.wait_for(Lock, std::chrono::milliseconds(1));
    }
  }

  Error waitForHostWork() {
    uint64_t Value;
    {
      std::lock_guard<std::mutex> SubmitLock(SubmissionMutex);
      std::lock_guard<std::mutex> Lock(Mutex);
      if (auto Err = checkFailure())
        return Err;
      Value = LastHostValue;
    }
    return wait(Value);
  }

  Error createCommandList(SubmissionTy &Submission) {
    if (auto Err = check(
            Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_COMPUTE,
                                           IID_PPV_ARGS(&Submission.Allocator)),
            "ID3D12Device::CreateCommandAllocator"))
      return Err;
    if (auto Err = check(
            Device->CreateCommandList(
                0, D3D12_COMMAND_LIST_TYPE_COMPUTE, Submission.Allocator.Get(),
                Submission.Pipeline.Get(), IID_PPV_ARGS(&Submission.List)),
            "ID3D12Device::CreateCommandList"))
      return Err;
    return Error::success();
  }

  Expected<uint64_t> submitWithValue(SubmissionTy Submission) {
    if (auto Err =
            check(Submission.List->Close(), "ID3D12GraphicsCommandList::Close"))
      return std::move(Err);
    std::lock_guard<std::mutex> SubmitLock(SubmissionMutex);
    std::lock_guard<std::mutex> Lock(Mutex);
    return enqueue(std::move(Submission));
  }

  Error submit(SubmissionTy Submission, bool Blocking = true) {
    auto Value = submitWithValue(std::move(Submission));
    if (!Value)
      return Value.takeError();
    return Blocking ? wait(*Value) : Error::success();
  }

  Error copy(const DirectXBufferTy &Dst, const DirectXBufferTy &Src,
             uint64_t Size, bool Blocking = true,
             std::function<Error()> Complete = {}) {
    SubmissionTy Submission{};
    Submission.Source = Src.Resource;
    Submission.Destination = Dst.Resource;
    Submission.Complete = std::move(Complete);
    if (auto Err = createCommandList(Submission))
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
    return submit(std::move(Submission), Blocking);
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
