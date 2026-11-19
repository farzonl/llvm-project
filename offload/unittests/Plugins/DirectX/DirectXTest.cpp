//===--- DirectXTest.cpp - DirectX discovery tests
//--------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "DirectXQueue.h"
#include <OffloadAPI.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <future>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

testing::AssertionResult succeeded(ol_result_t Result) {
  if (!Result)
    return testing::AssertionSuccess();
  return testing::AssertionFailure() << Result->Details;
}

testing::AssertionResult succeeded(llvm::Error Err) {
  if (Err)
    return testing::AssertionFailure() << llvm::toString(std::move(Err));
  return testing::AssertionSuccess();
}

testing::AssertionResult failedWith(ol_result_t Result, ol_errc_t Code) {
  if (!Result)
    return testing::AssertionFailure() << "expected an error";
  if (Result->Code != Code)
    return testing::AssertionFailure() << Result->Details;
  return testing::AssertionSuccess();
}

class ScopedEnvironment {
  const char *Name;
  std::optional<std::string> Previous;

  static void set(const char *Name, const char *Value) {
#ifdef _WIN32
    ASSERT_EQ(_putenv_s(Name, Value ? Value : ""), 0);
#else
    ASSERT_EQ(Value ? setenv(Name, Value, 1) : unsetenv(Name), 0);
#endif
  }

public:
  ScopedEnvironment(const char *Name, const char *Value) : Name(Name) {
    if (const char *Old = std::getenv(Name))
      Previous = Old;
    set(Name, Value);
  }
  ~ScopedEnvironment() { set(Name, Previous ? Previous->c_str() : nullptr); }
};

class DirectXTest : public testing::Test {
protected:
  void SetUp() override {
    ol_platform_backend_t Backend = OL_PLATFORM_BACKEND_DIRECTX;
    ol_init_args_t Args = OL_INIT_ARGS_INIT;
    Args.NumPlatforms = 1;
    Args.Platforms = &Backend;
    ASSERT_TRUE(succeeded(olInit(&Args)));
  }

  void TearDown() override { EXPECT_TRUE(succeeded(olShutDown())); }
};

class DirectXDeviceTest : public DirectXTest {
protected:
  ol_device_handle_t Device = nullptr;

  void SetUp() override {
    DirectXTest::SetUp();
    if (HasFatalFailure())
      return;
#ifndef _WIN32
    if (!std::getenv("OFFLOAD_DIRECTX_DXCORE_LIBRARY") ||
        !std::getenv("OFFLOAD_DIRECTX_D3D12_LIBRARY"))
      GTEST_SKIP() << "Set DirectX runtime paths to run Linux device tests";
#endif
    std::vector<ol_device_handle_t> Devices;
    ASSERT_TRUE(succeeded(olIterateDevices(
        [](ol_device_handle_t Device, void *Data) {
          static_cast<std::vector<ol_device_handle_t> *>(Data)->push_back(
              Device);
          return true;
        },
        &Devices)));
    for (auto Candidate : Devices) {
      ol_platform_handle_t Platform;
      ASSERT_TRUE(succeeded(olGetDeviceInfo(Candidate, OL_DEVICE_INFO_PLATFORM,
                                            sizeof(Platform), &Platform)));
      ol_platform_backend_t Backend;
      ASSERT_TRUE(succeeded(olGetPlatformInfo(
          Platform, OL_PLATFORM_INFO_BACKEND, sizeof(Backend), &Backend)));
      if (Backend == OL_PLATFORM_BACKEND_DIRECTX) {
        Device = Candidate;
        break;
      }
    }
    ASSERT_NE(Device, nullptr);
  }
};

TEST_F(DirectXTest, MissingRuntimeIsAnError) {
  ScopedEnvironment Missing("OFFLOAD_DIRECTX_DXCORE_LIBRARY",
                            "offload-nonexistent-directx-library");
  auto Result = olIterateDevices(
      [](ol_device_handle_t, void *) { return true; }, nullptr);
  ASSERT_NE(Result, nullptr);
  EXPECT_EQ(Result->Code, OL_ERRC_BACKEND_FAILURE);
  EXPECT_NE(std::string(Result->Details).find("OFFLOAD_DIRECTX_DXCORE_LIBRARY"),
            std::string::npos);
}

TEST_F(DirectXTest, DiscoversDevicesAndRestarts) {
#ifndef _WIN32
  if (!std::getenv("OFFLOAD_DIRECTX_DXCORE_LIBRARY") ||
      !std::getenv("OFFLOAD_DIRECTX_D3D12_LIBRARY"))
    GTEST_SKIP() << "Set DirectX runtime paths to run Linux discovery tests";
#endif

  std::vector<std::string> ExpectedIdentities;
  for (unsigned Iteration = 0; Iteration != 3; ++Iteration) {
    std::vector<ol_device_handle_t> Devices;
    ASSERT_TRUE(succeeded(olIterateDevices(
        [](ol_device_handle_t Device, void *Data) {
          static_cast<std::vector<ol_device_handle_t> *>(Data)->push_back(
              Device);
          return true;
        },
        &Devices)));

    std::vector<std::string> Identities;
    for (auto Device : Devices) {
      ol_platform_handle_t Platform;
      ASSERT_TRUE(succeeded(olGetDeviceInfo(Device, OL_DEVICE_INFO_PLATFORM,
                                            sizeof(Platform), &Platform)));
      ol_platform_backend_t Backend;
      ASSERT_TRUE(succeeded(olGetPlatformInfo(
          Platform, OL_PLATFORM_INFO_BACKEND, sizeof(Backend), &Backend)));
      if (Backend != OL_PLATFORM_BACKEND_DIRECTX)
        continue;

      for (auto Property : {OL_DEVICE_INFO_PRODUCT_NAME, OL_DEVICE_INFO_UID}) {
        size_t Size;
        ASSERT_TRUE(succeeded(olGetDeviceInfoSize(Device, Property, &Size)));
        ASSERT_GT(Size, 1u);
        std::string Value(Size, '\0');
        ASSERT_TRUE(
            succeeded(olGetDeviceInfo(Device, Property, Size, Value.data())));
        EXPECT_EQ(Value.back(), '\0');
        Identities.push_back(std::move(Value));
      }

      void *NativeDevice = nullptr;
      ASSERT_TRUE(
          succeeded(olGetDeviceInfo(Device, OL_DEVICE_INFO_NATIVE_HANDLE,
                                    sizeof(NativeDevice), &NativeDevice)));
      EXPECT_NE(NativeDevice, nullptr);

      ol_context_handle_t Contexts[2] = {};
      for (auto &Context : Contexts)
        ASSERT_TRUE(succeeded(olCreateContext(1, &Device, &Context)));
      EXPECT_NE(Contexts[0], Contexts[1]);
      ASSERT_TRUE(succeeded(olDestroyContext(Contexts[0])));

      void *ContextDevice = nullptr;
      ASSERT_TRUE(
          succeeded(olGetContextInfo(Contexts[1], OL_CONTEXT_INFO_NATIVE_HANDLE,
                                     sizeof(ContextDevice), &ContextDevice)));
      EXPECT_EQ(ContextDevice, NativeDevice);

      ol_device_handle_t RepeatedDevices[] = {Device, Device};
      ol_context_handle_t RejectedContext = nullptr;
      auto Result = olCreateContext(2, RepeatedDevices, &RejectedContext);
      ASSERT_NE(Result, nullptr);
      EXPECT_EQ(Result->Code, OL_ERRC_UNSUPPORTED);
      EXPECT_EQ(RejectedContext, nullptr);

      void *Allocation = nullptr;
      Result = olMemAlloc(Contexts[1], Device, OL_ALLOC_TYPE_MANAGED, 16,
                          &Allocation);
      ASSERT_NE(Result, nullptr);
      EXPECT_EQ(Result->Code, OL_ERRC_UNSUPPORTED);
      EXPECT_EQ(Allocation, nullptr);

      ol_queue_handle_t Queue = nullptr;
      ASSERT_TRUE(succeeded(olCreateQueue(Contexts[1], Device, &Queue)));
      ASSERT_NE(Queue, nullptr);
      ASSERT_TRUE(succeeded(olSyncQueue(Queue)));
      ASSERT_TRUE(succeeded(olDestroyQueue(Queue)));

      ASSERT_TRUE(succeeded(olDestroyContext(Contexts[1])));
    }
    ASSERT_FALSE(Identities.empty()) << "No DirectX adapters were discovered";
    if (Iteration == 0)
      ExpectedIdentities = Identities;
    else
      EXPECT_EQ(Identities, ExpectedIdentities);
    if (Iteration != 2) {
      ASSERT_TRUE(succeeded(olShutDown()));
      SetUp();
    }
  }
}

TEST_F(DirectXDeviceTest, DeviceBufferLifecycle) {
  ol_context_handle_t Context = nullptr;
  ol_context_handle_t OtherContext = nullptr;
  ASSERT_TRUE(succeeded(olCreateContext(1, &Device, &Context)));
  ASSERT_TRUE(succeeded(olCreateContext(1, &Device, &OtherContext)));

  for (size_t Size : {31u, 100000u}) {
    for (size_t Alignment : {16u, 256u, 65536u, 131072u}) {
      SCOPED_TRACE(std::to_string(Size) + "/" + std::to_string(Alignment));
      void *Buffer = nullptr;
      ASSERT_TRUE(succeeded(olMemAllocAligned(
          Context, Device, OL_ALLOC_TYPE_DEVICE, Size, Alignment, &Buffer)));
      ASSERT_NE(Buffer, nullptr);
      uintptr_t Address = reinterpret_cast<uintptr_t>(Buffer);
      EXPECT_EQ(Address % Alignment, 0u);

      for (size_t Offset : {size_t(0), Size - 1}) {
        void *Interior = reinterpret_cast<void *>(Address + Offset);
        void *Base = nullptr;
        size_t Bytes = 0;
        ol_device_handle_t Owner = nullptr;
        ol_alloc_type_t Kind;
        ASSERT_TRUE(succeeded(olGetMemInfo(Context, Interior, OL_MEM_INFO_BASE,
                                           sizeof(Base), &Base)));
        EXPECT_EQ(Base, Buffer);
        ASSERT_TRUE(succeeded(olGetMemInfo(Context, Interior, OL_MEM_INFO_SIZE,
                                           sizeof(Bytes), &Bytes)));
        EXPECT_EQ(Bytes, Size);
        ASSERT_TRUE(succeeded(olGetMemInfo(
            Context, Interior, OL_MEM_INFO_DEVICE, sizeof(Owner), &Owner)));
        EXPECT_EQ(Owner, Device);
        ASSERT_TRUE(succeeded(olGetMemInfo(Context, Interior, OL_MEM_INFO_TYPE,
                                           sizeof(Kind), &Kind)));
        EXPECT_EQ(Kind, OL_ALLOC_TYPE_DEVICE);
      }

      size_t Bytes;
      EXPECT_TRUE(failedWith(
          olGetMemInfo(Context, reinterpret_cast<void *>(Address + Size),
                       OL_MEM_INFO_SIZE, sizeof(Bytes), &Bytes),
          OL_ERRC_NOT_FOUND));
      EXPECT_TRUE(
          failedWith(olGetMemInfo(OtherContext, Buffer, OL_MEM_INFO_SIZE,
                                  sizeof(Bytes), &Bytes),
                     OL_ERRC_NOT_FOUND));
      EXPECT_TRUE(
          failedWith(olMemFree(OtherContext, Buffer), OL_ERRC_NOT_FOUND));
      EXPECT_TRUE(
          failedWith(olMemFree(Context, reinterpret_cast<void *>(Address + 1)),
                     OL_ERRC_NOT_FOUND));
      ASSERT_TRUE(succeeded(olMemFree(Context, Buffer)));
      EXPECT_TRUE(failedWith(olMemFree(Context, Buffer), OL_ERRC_NOT_FOUND));
      EXPECT_TRUE(failedWith(olGetMemInfo(Context, Buffer, OL_MEM_INFO_SIZE,
                                          sizeof(Bytes), &Bytes),
                             OL_ERRC_NOT_FOUND));
    }
  }

  // These requests have the same padded size in the common memory pool.
  for (auto [Size, Alignment] : {std::pair<size_t, size_t>{16, 32}, {32, 16}}) {
    void *Buffer = nullptr;
    ASSERT_TRUE(succeeded(olMemAllocAligned(
        Context, Device, OL_ALLOC_TYPE_DEVICE, Size, Alignment, &Buffer)));
    size_t Bytes;
    ASSERT_TRUE(succeeded(olGetMemInfo(Context, Buffer, OL_MEM_INFO_SIZE,
                                       sizeof(Bytes), &Bytes)));
    EXPECT_EQ(Bytes, Size);
    ASSERT_TRUE(succeeded(olMemFree(Context, Buffer)));
  }

  void *Buffer = nullptr;
  ASSERT_TRUE(succeeded(
      olMemAlloc(OtherContext, Device, OL_ALLOC_TYPE_DEVICE, 128, &Buffer)));
  EXPECT_EQ(reinterpret_cast<uintptr_t>(Buffer) % alignof(std::max_align_t),
            0u);
  ASSERT_TRUE(succeeded(olDestroyContext(Context)));
  ASSERT_TRUE(succeeded(olMemFree(OtherContext, Buffer)));
  // Context teardown must also release resources the caller has not freed.
  ASSERT_TRUE(succeeded(
      olMemAlloc(OtherContext, Device, OL_ALLOC_TYPE_DEVICE, 128, &Buffer)));
  ASSERT_TRUE(succeeded(olDestroyContext(OtherContext)));
}

TEST_F(DirectXDeviceTest, RejectsUnsupportedMemoryRequests) {
  ol_context_handle_t Context = nullptr;
  ASSERT_TRUE(succeeded(olCreateContext(1, &Device, &Context)));
  void *Buffer = nullptr;
  EXPECT_TRUE(failedWith(
      olMemAlloc(Context, Device, OL_ALLOC_TYPE_MANAGED, 128, &Buffer),
      OL_ERRC_UNSUPPORTED));
  EXPECT_TRUE(failedWith(olMemAllocHost(Context, Device, 128, &Buffer),
                         OL_ERRC_UNSUPPORTED));
  EXPECT_EQ(Buffer, nullptr);
  ASSERT_TRUE(succeeded(olDestroyContext(Context)));
}

TEST_F(DirectXDeviceTest, QueueLifecycle) {
  ol_context_handle_t Context = nullptr;
  ASSERT_TRUE(succeeded(olCreateContext(1, &Device, &Context)));
  for (unsigned Iteration = 0; Iteration != 10; ++Iteration) {
    ol_queue_handle_t Queue = nullptr;
    ASSERT_TRUE(succeeded(olCreateQueue(Context, Device, &Queue)));
    void *First = nullptr;
    ASSERT_TRUE(succeeded(olGetQueueInfo(Queue, OL_QUEUE_INFO_NATIVE_HANDLE,
                                         sizeof(First), &First)));
    ASSERT_NE(First, nullptr);
    bool Empty = false;
    ASSERT_TRUE(succeeded(
        olGetQueueInfo(Queue, OL_QUEUE_INFO_EMPTY, sizeof(Empty), &Empty)));
    EXPECT_TRUE(Empty);
    bool Completed = false;
    ASSERT_TRUE(succeeded(olQueryQueue(Queue, &Completed)));
    EXPECT_TRUE(Completed);
    ASSERT_TRUE(succeeded(olSyncQueue(Queue)));
    void *Second = nullptr;
    ASSERT_TRUE(succeeded(olGetQueueInfo(Queue, OL_QUEUE_INFO_NATIVE_HANDLE,
                                         sizeof(Second), &Second)));
    EXPECT_EQ(First, Second);
    ol_event_handle_t Event = nullptr;
    EXPECT_TRUE(failedWith(olCreateEvent(Queue, OL_EVENT_FLAGS_NONE, &Event),
                           OL_ERRC_UNSUPPORTED));
    EXPECT_EQ(Event, nullptr);
    ASSERT_TRUE(succeeded(olDestroyQueue(Queue)));
  }
  ASSERT_TRUE(succeeded(olDestroyContext(Context)));
}

TEST_F(DirectXDeviceTest, NativeFenceWaitsForCompletion) {
  void *Handle = nullptr;
  ASSERT_TRUE(succeeded(olGetDeviceInfo(Device, OL_DEVICE_INFO_NATIVE_HANDLE,
                                        sizeof(Handle), &Handle)));
  llvm::omp::target::plugin::directx::DirectXQueueTy Queue(
      static_cast<ID3D12Device *>(Handle));
  ASSERT_TRUE(succeeded(Queue.init()));
  auto Value = Queue.signal();
  ASSERT_TRUE(static_cast<bool>(Value)) << llvm::toString(Value.takeError());
  ASSERT_TRUE(succeeded(Queue.wait(*Value)));

  // Rewind the real fence without submitting work, then release the CPU wait.
  ASSERT_TRUE(SUCCEEDED(Queue.Fence->Signal(*Value - 1)));
  auto Pending = Queue.hasPendingWork();
  ASSERT_TRUE(static_cast<bool>(Pending))
      << llvm::toString(Pending.takeError());
  EXPECT_TRUE(*Pending);
  auto Wait =
      std::async(std::launch::async, [&] { return Queue.wait(*Value); });
  EXPECT_EQ(Wait.wait_for(std::chrono::milliseconds(30)),
            std::future_status::timeout);
  EXPECT_TRUE(SUCCEEDED(Queue.Fence->Signal(*Value)));
  EXPECT_TRUE(succeeded(Wait.get()));
  Pending = Queue.hasPendingWork();
  ASSERT_TRUE(static_cast<bool>(Pending))
      << llvm::toString(Pending.takeError());
  EXPECT_FALSE(*Pending);
  ASSERT_TRUE(succeeded(Queue.synchronize()));
}

TEST_F(DirectXDeviceTest, FenceTimelineDoesNotUseDeviceRemovalValue) {
  void *Handle = nullptr;
  ASSERT_TRUE(succeeded(olGetDeviceInfo(Device, OL_DEVICE_INFO_NATIVE_HANDLE,
                                        sizeof(Handle), &Handle)));
  llvm::omp::target::plugin::directx::DirectXQueueTy Queue(
      static_cast<ID3D12Device *>(Handle));
  ASSERT_TRUE(succeeded(Queue.init()));
  Queue.LastSignaledValue = std::numeric_limits<uint64_t>::max() - 2;
  ASSERT_TRUE(succeeded(Queue.synchronize()));
  auto Value = Queue.signal();
  ASSERT_FALSE(static_cast<bool>(Value));
  EXPECT_EQ(llvm::errorToErrorCode(Value.takeError()),
            error::make_error_code(error::ErrorCode::OUT_OF_RESOURCES));
  auto Pending = Queue.hasPendingWork();
  ASSERT_TRUE(static_cast<bool>(Pending))
      << llvm::toString(Pending.takeError());
  EXPECT_FALSE(*Pending);
}

TEST_F(DirectXDeviceTest, StagedBufferCopies) {
  ol_context_handle_t Context = nullptr;
  ol_queue_handle_t Queue = nullptr;
  ASSERT_TRUE(succeeded(olCreateContext(1, &Device, &Context)));
  ASSERT_TRUE(succeeded(olCreateQueue(Context, Device, &Queue)));
  for (bool Queued : {false, true}) {
    auto CopyQueue = Queued ? Queue : nullptr;
    for (size_t Size : {31u, 100000u}) {
      SCOPED_TRACE(std::to_string(Queued) + "/" + std::to_string(Size));
      void *Source = nullptr;
      void *Destination = nullptr;
      ASSERT_TRUE(succeeded(olMemAllocAligned(
          Context, Device, OL_ALLOC_TYPE_DEVICE, Size, 131072, &Source)));
      ASSERT_TRUE(succeeded(olMemAllocAligned(
          Context, Device, OL_ALLOC_TYPE_DEVICE, Size, 131072, &Destination)));
      std::vector<uint8_t> Input(Size);
      for (size_t I = 0; I < Size; ++I)
        Input[I] = static_cast<uint8_t>(I * 37 + 11);
      std::vector<uint8_t> Expected(Size, 91);
      std::vector<uint8_t> Output(Size + 2, 193);
      ASSERT_TRUE(succeeded(
          olMemcpy(CopyQueue, Source, Device, Input.data(), nullptr, Size)));
      ASSERT_TRUE(succeeded(olMemcpy(CopyQueue, Destination, Device,
                                     Expected.data(), nullptr, Size)));
      auto Interior = [](void *Base, size_t Offset) {
        return reinterpret_cast<void *>(reinterpret_cast<uintptr_t>(Base) +
                                        Offset);
      };
      ASSERT_TRUE(
          succeeded(olMemcpy(CopyQueue, Interior(Destination, 7), Device,
                             Interior(Source, 3), Device, Size - 13)));
      std::copy_n(Input.begin() + 3, Size - 13, Expected.begin() + 7);
      ASSERT_TRUE(succeeded(olMemcpy(CopyQueue, Output.data() + 1, nullptr,
                                     Destination, Device, Size)));
      EXPECT_TRUE(
          std::equal(Expected.begin(), Expected.end(), Output.begin() + 1));
      EXPECT_EQ(Output.front(), 193);
      EXPECT_EQ(Output.back(), 193);

      // Copies within one resource require a temporary, not conflicting states.
      ASSERT_TRUE(succeeded(olMemcpy(CopyQueue, Interior(Source, Size / 2),
                                     Device, Source, Device, Size / 3)));
      Expected = Input;
      std::copy_n(Input.begin(), Size / 3, Expected.begin() + Size / 2);
      ASSERT_TRUE(succeeded(olMemcpy(CopyQueue, Output.data() + 1, nullptr,
                                     Source, Device, Size)));
      EXPECT_TRUE(
          std::equal(Expected.begin(), Expected.end(), Output.begin() + 1));
      EXPECT_EQ(Output.front(), 193);
      EXPECT_EQ(Output.back(), 193);
      ASSERT_TRUE(succeeded(olMemFree(Context, Destination)));
      ASSERT_TRUE(succeeded(olMemFree(Context, Source)));
    }
  }
  ASSERT_TRUE(succeeded(olDestroyQueue(Queue)));
  ASSERT_TRUE(succeeded(olDestroyContext(Context)));
}

TEST_F(DirectXDeviceTest, CopyBoundsAndContextIsolation) {
  ol_context_handle_t Context = nullptr;
  ol_context_handle_t OtherContext = nullptr;
  ol_queue_handle_t Queue = nullptr;
  ol_queue_handle_t OtherQueue = nullptr;
  ASSERT_TRUE(succeeded(olCreateContext(1, &Device, &Context)));
  ASSERT_TRUE(succeeded(olCreateContext(1, &Device, &OtherContext)));
  ASSERT_TRUE(succeeded(olCreateQueue(Context, Device, &Queue)));
  ASSERT_TRUE(succeeded(olCreateQueue(OtherContext, Device, &OtherQueue)));
  void *Buffer = nullptr;
  void *Destination = nullptr;
  ASSERT_TRUE(succeeded(olMemAllocAligned(Context, Device, OL_ALLOC_TYPE_DEVICE,
                                          31, 131072, &Buffer)));
  ASSERT_TRUE(succeeded(
      olMemAlloc(Context, Device, OL_ALLOC_TYPE_DEVICE, 31, &Destination)));
  auto Last =
      reinterpret_cast<void *>(reinterpret_cast<uintptr_t>(Buffer) + 30);
  auto End = reinterpret_cast<void *>(reinterpret_cast<uintptr_t>(Buffer) + 31);
  std::vector<uint8_t> Bytes(64, 87);
  for (auto CopyQueue : {Queue, static_cast<ol_queue_handle_t>(nullptr)}) {
    EXPECT_TRUE(
        failedWith(olMemcpy(CopyQueue, Last, Device, Bytes.data(), nullptr, 2),
                   OL_ERRC_INVALID_SIZE));
    EXPECT_TRUE(
        failedWith(olMemcpy(CopyQueue, Bytes.data(), nullptr, Last, Device, 2),
                   OL_ERRC_INVALID_SIZE));
    EXPECT_TRUE(
        failedWith(olMemcpy(CopyQueue, Destination, Device, Last, Device, 2),
                   OL_ERRC_INVALID_SIZE));
    EXPECT_TRUE(
        failedWith(olMemcpy(CopyQueue, Last, Device, Destination, Device, 2),
                   OL_ERRC_INVALID_SIZE));
    EXPECT_TRUE(
        failedWith(olMemcpy(CopyQueue, End, Device, Bytes.data(), nullptr, 1),
                   OL_ERRC_NOT_FOUND));
    EXPECT_TRUE(
        failedWith(olMemcpy(CopyQueue, Buffer, Device, Bytes.data(), nullptr,
                            std::numeric_limits<size_t>::max()),
                   OL_ERRC_INVALID_SIZE));
    ASSERT_TRUE(succeeded(
        olMemcpy(CopyQueue, Buffer, Device, Bytes.data(), nullptr, 0)));
  }
  EXPECT_TRUE(
      failedWith(olMemcpy(OtherQueue, Buffer, Device, Bytes.data(), nullptr, 1),
                 OL_ERRC_NOT_FOUND));
  EXPECT_TRUE(
      failedWith(olMemcpy(OtherQueue, Bytes.data(), nullptr, Buffer, Device, 1),
                 OL_ERRC_NOT_FOUND));
  EXPECT_EQ(Bytes, std::vector<uint8_t>(64, 87));
  ASSERT_TRUE(succeeded(olMemFree(Context, Buffer)));
  for (auto CopyQueue : {Queue, static_cast<ol_queue_handle_t>(nullptr)})
    EXPECT_TRUE(failedWith(
        olMemcpy(CopyQueue, Bytes.data(), nullptr, Buffer, Device, 1),
        OL_ERRC_NOT_FOUND));
  ASSERT_TRUE(succeeded(olMemFree(Context, Destination)));
  ASSERT_TRUE(succeeded(olDestroyQueue(OtherQueue)));
  ASSERT_TRUE(succeeded(olDestroyQueue(Queue)));
  ASSERT_TRUE(succeeded(olDestroyContext(OtherContext)));
  ASSERT_TRUE(succeeded(olDestroyContext(Context)));
}

TEST_F(DirectXDeviceTest, ConcurrentSynchronousCopies) {
  std::vector<std::future<void>> Tasks;
  for (unsigned I = 0; I < 4; ++I)
    Tasks.push_back(std::async(std::launch::async, [&, I] {
      ol_context_handle_t Context = nullptr;
      ASSERT_TRUE(succeeded(olCreateContext(1, &Device, &Context)));
      void *Buffer = nullptr;
      ASSERT_TRUE(succeeded(
          olMemAlloc(Context, Device, OL_ALLOC_TYPE_DEVICE, 4096, &Buffer)));
      std::vector<uint8_t> Input(4096, I + 1);
      std::vector<uint8_t> Output(4096, 0);
      ASSERT_TRUE(succeeded(olMemcpy(nullptr, Buffer, Device, Input.data(),
                                     nullptr, Input.size())));
      ASSERT_TRUE(succeeded(olMemcpy(nullptr, Output.data(), nullptr, Buffer,
                                     Device, Output.size())));
      EXPECT_EQ(Input, Output);
      ASSERT_TRUE(succeeded(olMemFree(Context, Buffer)));
      ASSERT_TRUE(succeeded(olDestroyContext(Context)));
    }));
  for (auto &Task : Tasks)
    Task.get();
}

TEST_F(DirectXDeviceTest, CopiesUseNativeGPUAddressOffsets) {
  using namespace llvm::omp::target::plugin::directx;
  void *Handle = nullptr;
  ASSERT_TRUE(succeeded(olGetDeviceInfo(Device, OL_DEVICE_INFO_NATIVE_HANDLE,
                                        sizeof(Handle), &Handle)));
  auto *NativeDevice = static_cast<ID3D12Device *>(Handle);
  DirectXQueueTy Queue(NativeDevice);
  ASSERT_TRUE(succeeded(Queue.init()));
  auto Create = [&](D3D12_HEAP_TYPE Type, D3D12_RESOURCE_STATES State,
                    DirectXBufferTy &Buffer) {
    D3D12_HEAP_PROPERTIES Heap{};
    Heap.Type = Type;
    Heap.CreationNodeMask = Heap.VisibleNodeMask = 1;
    D3D12_RESOURCE_DESC Desc{};
    Desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    Desc.Width = 64;
    Desc.Height = Desc.DepthOrArraySize = Desc.MipLevels = 1;
    Desc.SampleDesc.Count = 1;
    Desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    Buffer.State = State;
    return succeeded(check(NativeDevice->CreateCommittedResource(
                               &Heap, D3D12_HEAP_FLAG_NONE, &Desc, State,
                               nullptr, IID_PPV_ARGS(&Buffer.Resource)),
                           "create native offset-test buffer"));
  };
  DirectXBufferTy Buffer, Upload, Readback;
  ASSERT_TRUE(
      Create(D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COMMON, Buffer));
  ASSERT_TRUE(Create(D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ,
                     Upload));
  ASSERT_TRUE(Create(D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST,
                     Readback));
  void *Mapped = nullptr;
  D3D12_RANGE None{0, 0}, Whole{0, 64};
  ASSERT_TRUE(SUCCEEDED(Upload.Resource->Map(0, &None, &Mapped)));
  std::fill_n(static_cast<uint8_t *>(Mapped), 64, uint8_t{0});
  Upload.Resource->Unmap(0, &Whole);
  ASSERT_TRUE(succeeded(Queue.copy(Buffer, Upload, 64)));
  ASSERT_TRUE(SUCCEEDED(Upload.Resource->Map(0, &None, &Mapped)));
  std::fill_n(static_cast<uint8_t *>(Mapped) + 3, 9, uint8_t{123});
  Upload.Resource->Unmap(0, &Whole);
  Upload.Offset = 3;
  auto Interior = getBufferFromAddress(
      Buffer.Resource, Buffer.Resource->GetGPUVirtualAddress() + 7, 9);
  ASSERT_TRUE(static_cast<bool>(Interior))
      << llvm::toString(Interior.takeError());
  ASSERT_TRUE(succeeded(Queue.copy(*Interior, Upload, 9)));
  ASSERT_TRUE(succeeded(Queue.copy(Readback, Buffer, 64)));
  ASSERT_TRUE(SUCCEEDED(Readback.Resource->Map(0, &Whole, &Mapped)));
  std::vector<uint8_t> Output(static_cast<uint8_t *>(Mapped),
                              static_cast<uint8_t *>(Mapped) + 64);
  Readback.Resource->Unmap(0, &None);
  std::vector<uint8_t> Expected(64, 0);
  std::fill_n(Expected.begin() + 7, 9, uint8_t{123});
  EXPECT_EQ(Output, Expected);
}

} // namespace
