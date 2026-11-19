//===--- DirectXTest.cpp - DirectX discovery tests
//--------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "DirectXArguments.h"
#include "DirectXQueue.h"
#include "DirectXRootSignature.h"
#include "llvm/Bitcode/BitcodeWriter.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/Object/OffloadBinary.h"
#include "llvm/Support/DynamicLibrary.h"
#include "llvm/Support/JSON.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/raw_ostream.h"
#include <OffloadAPI.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
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

llvm::Expected<PFN_D3D12_SERIALIZE_ROOT_SIGNATURE> rootSerializer() {
  const char *Name = std::getenv("OFFLOAD_DIRECTX_D3D12_LIBRARY");
  if (!Name)
    Name = "d3d12.dll";
  std::string Message;
  auto Library = llvm::sys::DynamicLibrary::getPermanentLibrary(Name, &Message);
  if (!Library.isValid())
    return error::createOffloadError(error::ErrorCode::BACKEND_FAILURE, "%s",
                                     Message.c_str());
  auto Serialize = reinterpret_cast<PFN_D3D12_SERIALIZE_ROOT_SIGNATURE>(
      Library.getAddressOfSymbol("D3D12SerializeRootSignature"));
  if (!Serialize)
    return error::createOffloadError(error::ErrorCode::BACKEND_FAILURE,
                                     "missing root serializer");
  return Serialize;
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

TEST_F(DirectXDeviceTest, CreatesCanonicalRootSignatures) {
  using namespace llvm;
  using namespace llvm::omp::target::plugin::directx;
  void *Handle = nullptr;
  ASSERT_TRUE(succeeded(olGetDeviceInfo(Device, OL_DEVICE_INFO_NATIVE_HANDLE,
                                        sizeof(Handle), &Handle)));
  auto *NativeDevice = static_cast<ID3D12Device *>(Handle);
  auto Serialize = rootSerializer();
  ASSERT_TRUE(static_cast<bool>(Serialize)) << toString(Serialize.takeError());

  DirectXManifestTy Manifest;
  for (unsigned Layout = 0; Layout != 4; ++Layout) {
    SCOPED_TRACE(Layout);
    Manifest.Arguments.clear();
    Manifest.Constants.reset();
    if (Layout & 1)
      Manifest.Arguments.push_back(
          {0,
           DirectXBufferBindingTy{DirectXBufferBindingTy::Kind::SRV, 2, 1, 4}});
    if (Layout & 2) {
      Manifest.Constants = DirectXConstantBindingTy{4, 2, 252};
      Manifest.Arguments.push_back(
          {static_cast<uint32_t>(Manifest.Arguments.size()),
           DirectXScalarBindingTy{4, 0}});
    }
    Microsoft::WRL::ComPtr<ID3D12RootSignature> Root;
    ASSERT_TRUE(succeeded(
        createRootSignature(Manifest, *NativeDevice, *Serialize, Root)));
    ASSERT_NE(Root.Get(), nullptr);
    Microsoft::WRL::ComPtr<ID3D12Device> Owner;
    ASSERT_TRUE(SUCCEEDED(Root->GetDevice(IID_PPV_ARGS(&Owner))));
    EXPECT_EQ(Owner.Get(), NativeDevice);
  }
}

TEST_F(DirectXDeviceTest, RecordsBoundedArgumentBindingsWithoutDispatch) {
  using namespace llvm;
  using namespace llvm::omp::target::plugin::directx;
  using Microsoft::WRL::ComPtr;
  void *Handle = nullptr;
  ASSERT_TRUE(succeeded(olGetDeviceInfo(Device, OL_DEVICE_INFO_NATIVE_HANDLE,
                                        sizeof(Handle), &Handle)));
  auto &NativeDevice = *static_cast<ID3D12Device *>(Handle);
  auto Serialize = rootSerializer();
  ASSERT_TRUE(static_cast<bool>(Serialize)) << toString(Serialize.takeError());
  D3D12_HEAP_PROPERTIES HeapProperties{};
  HeapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;
  HeapProperties.CreationNodeMask = HeapProperties.VisibleNodeMask = 1;
  D3D12_RESOURCE_DESC Desc{};
  Desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  Desc.Width = 128;
  Desc.Height = Desc.DepthOrArraySize = Desc.MipLevels = 1;
  Desc.SampleDesc.Count = 1;
  Desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  Desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
  ComPtr<ID3D12Resource> Resource;
  ASSERT_TRUE(succeeded(
      check(NativeDevice.CreateCommittedResource(
                &HeapProperties, D3D12_HEAP_FLAG_NONE, &Desc,
                D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&Resource)),
            "create binding-test buffer")));
  uint64_t Address = Resource->GetGPUVirtualAddress();
  void *UAVPointer = reinterpret_cast<void *>(Address + 16);
  void *SRVPointer = reinterpret_cast<void *>(Address + 64);
  uint8_t Scalar4[] = {0x34, 0x12, 0xc0, 0x7f};
  uint8_t Scalar8[] = {1, 2, 3, 4, 5, 6, 7, 8};

  for (unsigned Layout = 0; Layout != 4; ++Layout) {
    SCOPED_TRACE(Layout);
    DirectXManifestTy Manifest;
    SmallVector<void *> Args;
    SmallVector<int64_t> Sizes;
    auto Scalar = [&](uint32_t Size, uint32_t Offset, void *Value) {
      Manifest.Constants = DirectXConstantBindingTy{4, 2, 16};
      Manifest.Arguments.push_back({static_cast<uint32_t>(Args.size()),
                                    DirectXScalarBindingTy{Size, Offset}});
      Args.push_back(Value);
      Sizes.push_back(Size);
    };
    auto Buffer = [&](DirectXBufferBindingTy::Kind Kind, uint32_t Register,
                      uint32_t MinSize, void **Value) {
      Manifest.Arguments.push_back(
          {static_cast<uint32_t>(Args.size()),
           DirectXBufferBindingTy{Kind, Register, 1, MinSize}});
      Args.push_back(Value);
      Sizes.push_back(sizeof(void *));
    };
    if (Layout & 2)
      Scalar(4, 0, Scalar4);
    if (Layout & 1)
      Buffer(DirectXBufferBindingTy::Kind::UAV, 3, 32, &UAVPointer);
    if (Layout & 2)
      Scalar(8, 8, Scalar8);
    if (Layout & 1)
      Buffer(DirectXBufferBindingTy::Kind::SRV, 2, 4, &SRVPointer);
    auto Resolve =
        [&](const void *Pointer) -> Expected<DirectXResolvedBufferTy> {
      if (Pointer != UAVPointer && Pointer != SRVPointer)
        return error::createOffloadError(error::ErrorCode::NOT_FOUND,
                                         "unexpected binding-test address");
      bool IsUAV = Pointer == UAVPointer;
      return DirectXResolvedBufferTy{
          {Resource, IsUAV ? uint64_t{16} : uint64_t{64}},
          IsUAV ? uint64_t{35} : uint64_t{7}};
    };
    DirectXArgumentBindingsTy Bindings;
    ASSERT_TRUE(succeeded(Bindings.prepare(Manifest, Args, Sizes, Resolve)));
    if (Layout & 2)
      EXPECT_EQ(ArrayRef(Bindings.Constants),
                (ArrayRef<uint32_t>{0x7fc01234, 0, 0x04030201, 0x08070605}));
    else
      EXPECT_TRUE(Bindings.Constants.empty());
    if (Layout & 1) {
      ASSERT_EQ(Bindings.Buffers.size(), 2u);
      const auto &UAV =
          std::get<D3D12_UNORDERED_ACCESS_VIEW_DESC>(Bindings.Buffers[0].View);
      EXPECT_EQ(UAV.Format, DXGI_FORMAT_R32_TYPELESS);
      EXPECT_EQ(UAV.ViewDimension, D3D12_UAV_DIMENSION_BUFFER);
      EXPECT_EQ(UAV.Buffer.FirstElement, 4u);
      EXPECT_EQ(UAV.Buffer.NumElements, 8u);
      EXPECT_EQ(UAV.Buffer.StructureByteStride, 0u);
      EXPECT_EQ(UAV.Buffer.CounterOffsetInBytes, 0u);
      EXPECT_EQ(UAV.Buffer.Flags, D3D12_BUFFER_UAV_FLAG_RAW);
      const auto &SRV =
          std::get<D3D12_SHADER_RESOURCE_VIEW_DESC>(Bindings.Buffers[1].View);
      EXPECT_EQ(SRV.Format, DXGI_FORMAT_R32_TYPELESS);
      EXPECT_EQ(SRV.ViewDimension, D3D12_SRV_DIMENSION_BUFFER);
      EXPECT_EQ(SRV.Shader4ComponentMapping,
                D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING);
      EXPECT_EQ(SRV.Buffer.FirstElement, 16u);
      EXPECT_EQ(SRV.Buffer.NumElements, 1u);
      EXPECT_EQ(SRV.Buffer.StructureByteStride, 0u);
      EXPECT_EQ(SRV.Buffer.Flags, D3D12_BUFFER_SRV_FLAG_RAW);
    } else
      EXPECT_TRUE(Bindings.Buffers.empty());

    ComPtr<ID3D12RootSignature> Root;
    ASSERT_TRUE(succeeded(
        createRootSignature(Manifest, NativeDevice, *Serialize, Root)));
    ComPtr<ID3D12CommandAllocator> Allocator;
    ComPtr<ID3D12GraphicsCommandList> List;
    ASSERT_TRUE(succeeded(
        check(NativeDevice.CreateCommandAllocator(
                  D3D12_COMMAND_LIST_TYPE_COMPUTE, IID_PPV_ARGS(&Allocator)),
              "create binding-test allocator")));
    ASSERT_TRUE(succeeded(
        check(NativeDevice.CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_COMPUTE,
                                             Allocator.Get(), nullptr,
                                             IID_PPV_ARGS(&List)),
              "create binding-test list")));
    if (Layout & 1) {
      auto Err = Bindings.record(*List.Get(), *Root.Get());
      ASSERT_TRUE(static_cast<bool>(Err));
      handleAllErrors(std::move(Err), [](const error::OffloadError &Detail) {
        EXPECT_EQ(Detail.convertToErrorCode(),
                  error::make_error_code(error::ErrorCode::INVALID_ARGUMENT));
      });
    }
    ASSERT_TRUE(succeeded(Bindings.createDescriptorHeap(NativeDevice)));
    if (Layout & 1) {
      ASSERT_NE(Bindings.Heap.Get(), nullptr);
      auto HeapDesc = Bindings.Heap->GetDesc();
      EXPECT_EQ(HeapDesc.Type, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
      EXPECT_EQ(HeapDesc.Flags, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE);
      EXPECT_EQ(HeapDesc.NumDescriptors, 2u);
      DirectXArgumentBindingsTy Other;
      ASSERT_TRUE(succeeded(Other.prepare(Manifest, Args, Sizes, Resolve)));
      ASSERT_TRUE(succeeded(Other.createDescriptorHeap(NativeDevice)));
      EXPECT_NE(Other.Heap.Get(), Bindings.Heap.Get());
      auto Err = Bindings.prepare(
          Manifest, Args, Sizes,
          [&](const void *) -> Expected<DirectXResolvedBufferTy> {
            return DirectXResolvedBufferTy{{Resource, 16}, 128};
          });
      ASSERT_TRUE(static_cast<bool>(Err));
      handleAllErrors(std::move(Err), [](const error::OffloadError &Detail) {
        EXPECT_EQ(Detail.convertToErrorCode(),
                  error::make_error_code(error::ErrorCode::BACKEND_FAILURE));
      });
      EXPECT_EQ(Bindings.Heap->GetDesc().NumDescriptors, 2u);
    } else
      EXPECT_EQ(Bindings.Heap.Get(), nullptr);
    ASSERT_TRUE(succeeded(Bindings.record(*List.Get(), *Root.Get())));
    ASSERT_TRUE(
        succeeded(check(List->Close(), "close binding-test command list")));
    if (Layout == 3) {
      Resource.Reset();
      EXPECT_EQ(Bindings.Buffers[0].Resource->GetDesc().Width, 128u);
    }
  }
}

HRESULT WINAPI failRootSerialization(const D3D12_ROOT_SIGNATURE_DESC *,
                                     D3D_ROOT_SIGNATURE_VERSION Version,
                                     ID3DBlob **, ID3DBlob **) {
  EXPECT_EQ(Version, D3D_ROOT_SIGNATURE_VERSION_1);
  return E_FAIL;
}

HRESULT WINAPI omitSerializedRoot(const D3D12_ROOT_SIGNATURE_DESC *,
                                  D3D_ROOT_SIGNATURE_VERSION, ID3DBlob **,
                                  ID3DBlob **) {
  return S_OK;
}

TEST_F(DirectXDeviceTest, ReportsRootSerializationFailures) {
  using namespace llvm;
  using namespace llvm::omp::target::plugin::directx;
  void *Handle = nullptr;
  ASSERT_TRUE(succeeded(olGetDeviceInfo(Device, OL_DEVICE_INFO_NATIVE_HANDLE,
                                        sizeof(Handle), &Handle)));
  auto &NativeDevice = *static_cast<ID3D12Device *>(Handle);
  for (auto Serialize :
       {static_cast<PFN_D3D12_SERIALIZE_ROOT_SIGNATURE>(nullptr),
        failRootSerialization, omitSerializedRoot}) {
    Microsoft::WRL::ComPtr<ID3D12RootSignature> Root;
    auto Err = createRootSignature({}, NativeDevice, Serialize, Root);
    ASSERT_TRUE(static_cast<bool>(Err));
    EXPECT_EQ(Root.Get(), nullptr);
    handleAllErrors(std::move(Err), [](const error::OffloadError &Err) {
      EXPECT_EQ(Err.convertToErrorCode(),
                error::make_error_code(error::ErrorCode::BACKEND_FAILURE));
      EXPECT_TRUE(StringRef(Err.getMessage()).contains_insensitive("root"));
    });
  }
}

class DirectXProgramTest : public DirectXDeviceTest {
protected:
  ol_context_handle_t Context = nullptr;
  std::string Shader;

  void SetUp() override {
    DirectXDeviceTest::SetUp();
    if (HasFatalFailure() || IsSkipped())
      return;
    void *Handle = nullptr;
    ASSERT_TRUE(succeeded(olGetDeviceInfo(Device, OL_DEVICE_INFO_NATIVE_HANDLE,
                                          sizeof(Handle), &Handle)));
    D3D12_FEATURE_DATA_SHADER_MODEL Model{D3D_SHADER_MODEL_6_0};
    HRESULT Result = static_cast<ID3D12Device *>(Handle)->CheckFeatureSupport(
        D3D12_FEATURE_SHADER_MODEL, &Model, sizeof(Model));
    if (Result == E_INVALIDARG ||
        (SUCCEEDED(Result) && Model.HighestShaderModel < D3D_SHADER_MODEL_6_0))
      GTEST_SKIP() << "program tests require shader model 6.0";
    ASSERT_TRUE(SUCCEEDED(Result));
    ASSERT_TRUE(succeeded(olCreateContext(1, &Device, &Context)));
    auto Buffer = llvm::MemoryBuffer::getFile(
        DIRECTX_TEST_INPUTS_DIR "/buffers.dxil", false);
    ASSERT_TRUE(static_cast<bool>(Buffer)) << Buffer.getError().message();
    Shader = (*Buffer)->getBuffer().str();
  }

  void TearDown() override {
    if (Context)
      EXPECT_TRUE(succeeded(olDestroyContext(Context)));
    DirectXDeviceTest::TearDown();
  }

  static llvm::json::Object manifest() {
    using namespace llvm;
    return json::Object{
        {"version", 1},
        {"entry", "main"},
        {"shader_model", json::Array{6, 0}},
        {"numthreads", json::Array{8, 2, 1}},
        {"features", json::Array{}},
        {"constants", nullptr},
        {"arguments", json::Array{json::Object{{"index", 0},
                                               {"kind", "srv"},
                                               {"register", 2},
                                               {"space", 1},
                                               {"min_size", 4}},
                                  json::Object{{"index", 1},
                                               {"kind", "uav"},
                                               {"register", 3},
                                               {"space", 1},
                                               {"min_size", 4}}}}};
  }

  std::string package(llvm::json::Object Manifest = manifest(),
                      unsigned Count = 1) {
    using namespace llvm;
    std::string JSON;
    raw_string_ostream OS(JSON);
    OS << json::Value(std::move(Manifest));
    SmallVector<object::OffloadBinary::OffloadingImage> Images;
    for (unsigned I = 0; I < Count; ++I) {
      auto &Image = Images.emplace_back();
      Image.TheImageKind = object::IMG_DXIL;
      Image.StringData["directx.manifest"] = JSON;
      Image.Image = MemoryBuffer::getMemBufferCopy(Shader);
    }
    auto Bytes = object::OffloadBinary::write(Images);
    return std::string(Bytes.data(), Bytes.size());
  }

  void expectRejected(llvm::StringRef Bytes, ol_errc_t Code) {
    bool Valid = true;
    ASSERT_TRUE(
        succeeded(olIsValidBinary(Device, Bytes.data(), Bytes.size(), &Valid)));
    EXPECT_FALSE(Valid);
    ol_program_handle_t Program = nullptr;
    EXPECT_TRUE(failedWith(
        olCreateProgram(Context, Device, Bytes.data(), Bytes.size(), &Program),
        Code));
    EXPECT_EQ(Program, nullptr);
  }
};

TEST_F(DirectXProgramTest, OwnsBytecodeAndKeepsLoadingIndependent) {
  auto Bytes = package();
  std::string Unaligned = "x" + Bytes;
  bool Valid = false;
  ASSERT_TRUE(succeeded(
      olIsValidBinary(Device, Unaligned.data() + 1, Bytes.size(), &Valid)));
  ASSERT_TRUE(Valid);
  ol_program_handle_t Programs[2] = {};
  for (auto &Program : Programs)
    ASSERT_TRUE(succeeded(olCreateProgram(Context, Device, Bytes.data(),
                                          Bytes.size(), &Program)));
  Bytes.assign(Bytes.size(), 'x');
  for (auto Program : Programs) {
    void *Base = nullptr;
    size_t Size = 0;
    ASSERT_TRUE(succeeded(olGetProgramInfo(Program, OL_PROGRAM_INFO_IMAGE_BASE,
                                           sizeof(Base), &Base)));
    ASSERT_TRUE(succeeded(olGetProgramInfo(Program, OL_PROGRAM_INFO_IMAGE_SIZE,
                                           sizeof(Size), &Size)));
    ASSERT_EQ(Size, Shader.size());
    EXPECT_EQ(llvm::StringRef(static_cast<const char *>(Base), Size), Shader);
    ol_symbol_handle_t Symbol = nullptr;
    ASSERT_TRUE(succeeded(
        olGetSymbol(Program, "main", OL_SYMBOL_KIND_KERNEL, &Symbol)));
    EXPECT_NE(Symbol, nullptr);
    ASSERT_TRUE(succeeded(olDestroyProgram(Program)));
  }
  for (unsigned I = 0; I < 8; ++I) {
    Bytes = package();
    ol_program_handle_t Program = nullptr;
    ASSERT_TRUE(succeeded(olCreateProgram(Context, Device, Bytes.data(),
                                          Bytes.size(), &Program)));
    ol_symbol_handle_t Symbol = nullptr;
    ASSERT_TRUE(succeeded(
        olGetSymbol(Program, "main", OL_SYMBOL_KIND_KERNEL, &Symbol)));
    ASSERT_TRUE(succeeded(olDestroyProgram(Program)));
  }
}

TEST_F(DirectXProgramTest, LooksUpAndEnumeratesOnlyTheManifestEntry) {
  auto Bytes = package();
  ol_program_handle_t Programs[2] = {};
  ol_symbol_handle_t Kernels[2] = {};
  for (unsigned I = 0; I < 2; ++I) {
    ASSERT_TRUE(succeeded(olCreateProgram(Context, Device, Bytes.data(),
                                          Bytes.size(), &Programs[I])));
    ol_symbol_handle_t Missing = nullptr;
    for (unsigned Attempt = 0; Attempt < 2; ++Attempt) {
      EXPECT_TRUE(failedWith(
          olGetSymbol(Programs[I], "missing", OL_SYMBOL_KIND_KERNEL, &Missing),
          OL_ERRC_NOT_FOUND));
      EXPECT_EQ(Missing, nullptr);
    }
    ASSERT_TRUE(succeeded(
        olGetSymbol(Programs[I], "main", OL_SYMBOL_KIND_KERNEL, &Kernels[I])));
    ol_symbol_handle_t Repeated = nullptr;
    ASSERT_TRUE(succeeded(
        olGetSymbol(Programs[I], "main", OL_SYMBOL_KIND_KERNEL, &Repeated)));
    EXPECT_EQ(Repeated, Kernels[I]);
    ol_symbol_kind_t Kind{};
    ASSERT_TRUE(succeeded(
        olGetSymbolInfo(Kernels[I], OL_SYMBOL_INFO_KIND, sizeof(Kind), &Kind)));
    EXPECT_EQ(Kind, OL_SYMBOL_KIND_KERNEL);
    size_t Size = 0;
    ASSERT_TRUE(
        succeeded(olGetSymbolInfoSize(Kernels[I], OL_SYMBOL_INFO_NAME, &Size)));
    std::string Name(Size, '\0');
    ASSERT_TRUE(succeeded(
        olGetSymbolInfo(Kernels[I], OL_SYMBOL_INFO_NAME, Size, Name.data())));
    EXPECT_STREQ(Name.c_str(), "main");
    uint32_t Threads = 0, SharedBytes = 1;
    ASSERT_TRUE(
        succeeded(olGetSymbolInfo(Kernels[I], OL_SYMBOL_INFO_KERNEL_MAX_THREADS,
                                  sizeof(Threads), &Threads)));
    EXPECT_EQ(Threads, 16u);
    ASSERT_TRUE(succeeded(
        olGetSymbolInfo(Kernels[I], OL_SYMBOL_INFO_KERNEL_STATIC_BLOCK_MEM_SIZE,
                        sizeof(SharedBytes), &SharedBytes)));
    EXPECT_EQ(SharedBytes, 0u);

    std::vector<ol_symbol_handle_t> Enumerated;
    ASSERT_TRUE(succeeded(olIterateSymbols(
        Programs[I], OL_SYMBOL_KIND_KERNEL,
        [](ol_symbol_handle_t Symbol, void *Data) {
          static_cast<std::vector<ol_symbol_handle_t> *>(Data)->push_back(
              Symbol);
          return false;
        },
        &Enumerated)));
    ASSERT_EQ(Enumerated.size(), 1u);
    EXPECT_EQ(Enumerated[0], Kernels[I]);
    ol_symbol_handle_t Global = nullptr;
    EXPECT_TRUE(failedWith(olGetSymbol(Programs[I], "main",
                                       OL_SYMBOL_KIND_GLOBAL_VARIABLE, &Global),
                           OL_ERRC_UNSUPPORTED));
    EXPECT_TRUE(failedWith(olIterateSymbols(
                               Programs[I], OL_SYMBOL_KIND_GLOBAL_VARIABLE,
                               [](ol_symbol_handle_t, void *) {
                                 ADD_FAILURE() << "globals are unsupported";
                                 return true;
                               },
                               nullptr),
                           OL_ERRC_UNSUPPORTED));
  }
  EXPECT_NE(Kernels[0], Kernels[1]);
  ASSERT_TRUE(succeeded(olDestroyProgram(Programs[0])));
  uint32_t Threads = 0;
  ASSERT_TRUE(
      succeeded(olGetSymbolInfo(Kernels[1], OL_SYMBOL_INFO_KERNEL_MAX_THREADS,
                                sizeof(Threads), &Threads)));
  EXPECT_EQ(Threads, 16u);
  ASSERT_TRUE(succeeded(olDestroyProgram(Programs[1])));
}

TEST_F(DirectXProgramTest, EnumeratesBeforeLookupAndStillRejectsDispatch) {
  auto Bytes = package();
  ol_program_handle_t Program = nullptr;
  ASSERT_TRUE(succeeded(
      olCreateProgram(Context, Device, Bytes.data(), Bytes.size(), &Program)));
  std::vector<ol_symbol_handle_t> Enumerated;
  auto Collect = [](ol_symbol_handle_t Symbol, void *Data) {
    static_cast<std::vector<ol_symbol_handle_t> *>(Data)->push_back(Symbol);
    return true;
  };
  ASSERT_TRUE(succeeded(
      olIterateSymbols(Program, OL_SYMBOL_KIND_KERNEL, Collect, &Enumerated)));
  ASSERT_EQ(Enumerated.size(), 1u);
  ol_symbol_handle_t Kernel = nullptr;
  ASSERT_TRUE(
      succeeded(olGetSymbol(Program, "main", OL_SYMBOL_KIND_KERNEL, &Kernel)));
  EXPECT_EQ(Enumerated[0], Kernel);
  size_t GroupSize = 0;
  EXPECT_TRUE(
      failedWith(olCalculateOptimalOccupancy(Device, Kernel, 0, &GroupSize),
                 OL_ERRC_UNSUPPORTED));
  ol_kernel_launch_size_args_t Launch{};
  Launch.Dimensions = 3;
  Launch.NumGroups = {1, 1, 1};
  Launch.GroupSize = {8, 2, 1};
  uint32_t MaxGroups = 0;
  EXPECT_TRUE(failedWith(
      olGetKernelMaxCooperativeGroupCount(Device, Kernel, &Launch, &MaxGroups),
      OL_ERRC_UNSUPPORTED));
  void *Input = nullptr, *Output = nullptr;
  void *Args[] = {&Input, &Output};
  size_t Sizes[] = {sizeof(Input), sizeof(Output)};
  ol_queue_handle_t Queue = nullptr;
  ASSERT_TRUE(succeeded(olCreateQueue(Context, Device, &Queue)));
  for (auto Q : {static_cast<ol_queue_handle_t>(nullptr), Queue})
    EXPECT_TRUE(failedWith(olLaunchKernel(Q, Device, Kernel, &Launch, nullptr,
                                          std::size(Args), Args, Sizes),
                           OL_ERRC_UNSUPPORTED));
  ASSERT_TRUE(succeeded(olSyncQueue(Queue)));
  ASSERT_TRUE(succeeded(olDestroyQueue(Queue)));
  ASSERT_TRUE(succeeded(olDestroyProgram(Program)));
}

TEST_F(DirectXProgramTest, RejectsUnpackagedAndMismatchedImages) {
  expectRejected(Shader, OL_ERRC_INVALID_BINARY);
  auto Object = manifest();
  Object["entry"] = "wrong";
  expectRejected(package(std::move(Object)), OL_ERRC_INVALID_BINARY);
  Object = manifest();
  Object["features"] = llvm::json::Array{"ResourceDescriptorHeapIndexing"};
  expectRejected(package(std::move(Object)), OL_ERRC_UNSUPPORTED);
  expectRejected(package(manifest(), 2), OL_ERRC_UNSUPPORTED);
  auto Bytes = package();
  expectRejected(Bytes + Bytes, OL_ERRC_INVALID_BINARY);
  expectRejected(llvm::StringRef(Bytes).drop_back(), OL_ERRC_INVALID_BINARY);

  llvm::LLVMContext LLVMContext;
  llvm::Module M("raw DXIL", LLVMContext);
  M.setTargetTriple(llvm::Triple("dxil-pc-shadermodel6.0-compute"));
  llvm::SmallString<0> Bitcode;
  llvm::raw_svector_ostream OS(Bitcode);
  llvm::WriteBitcodeToFile(M, OS);
  expectRejected(Bitcode, OL_ERRC_UNSUPPORTED);
}

TEST_F(DirectXProgramTest, LoadsScalarShaderAccordingToNativeFeatureSupport) {
  auto Buffer = llvm::MemoryBuffer::getFile(
      DIRECTX_TEST_INPUTS_DIR "/manifest.dxil", false);
  ASSERT_TRUE(static_cast<bool>(Buffer)) << Buffer.getError().message();
  Shader = (*Buffer)->getBuffer().str();
  auto Object = manifest();
  Object["features"] = llvm::json::Array{"Int64Ops"};
  Object["constants"] =
      llvm::json::Object{{"register", 4}, {"space", 2}, {"size", 16}};
  auto *Arguments = Object.getArray("arguments");
  Arguments->push_back(llvm::json::Object{
      {"index", 2}, {"kind", "scalar"}, {"size", 4}, {"offset", 0}});
  Arguments->push_back(llvm::json::Object{
      {"index", 3}, {"kind", "scalar"}, {"size", 8}, {"offset", 8}});
  auto Bytes = package(std::move(Object));
  void *Handle = nullptr;
  ASSERT_TRUE(succeeded(olGetDeviceInfo(Device, OL_DEVICE_INFO_NATIVE_HANDLE,
                                        sizeof(Handle), &Handle)));
  D3D12_FEATURE_DATA_D3D12_OPTIONS1 Options{};
  HRESULT Result = static_cast<ID3D12Device *>(Handle)->CheckFeatureSupport(
      D3D12_FEATURE_D3D12_OPTIONS1, &Options, sizeof(Options));
  ASSERT_TRUE(SUCCEEDED(Result) || Result == E_INVALIDARG);
  if (Result == E_INVALIDARG || !Options.Int64ShaderOps) {
    expectRejected(Bytes, OL_ERRC_UNSUPPORTED);
    return;
  }
  bool Valid = false;
  ASSERT_TRUE(
      succeeded(olIsValidBinary(Device, Bytes.data(), Bytes.size(), &Valid)));
  ASSERT_TRUE(Valid);
  ol_program_handle_t Program = nullptr;
  ASSERT_TRUE(succeeded(
      olCreateProgram(Context, Device, Bytes.data(), Bytes.size(), &Program)));
  ol_symbol_handle_t Kernel = nullptr;
  ASSERT_TRUE(
      succeeded(olGetSymbol(Program, "main", OL_SYMBOL_KIND_KERNEL, &Kernel)));
  EXPECT_NE(Kernel, nullptr);
  ASSERT_TRUE(succeeded(olDestroyProgram(Program)));
}

TEST_F(DirectXProgramTest, RejectsMalformedPackageMetadataSafely) {
  using Binary = llvm::object::OffloadBinary;
  auto Bytes = package();
  Binary::Header Header;
  memcpy(&Header, Bytes.data(), sizeof(Header));
  Binary::Entry Entry;
  memcpy(&Entry, Bytes.data() + Header.EntriesOffset, sizeof(Entry));
  Binary::StringEntry String;
  memcpy(&String, Bytes.data() + Entry.StringOffset, sizeof(String));
  String.KeyOffset = Bytes.size();
  memcpy(Bytes.data() + Entry.StringOffset, &String, sizeof(String));
  expectRejected(Bytes, OL_ERRC_INVALID_BINARY);

  Bytes = package();
  String.KeyOffset = 0;
  String.ValueSize = std::numeric_limits<uint64_t>::max();
  memcpy(Bytes.data() + Entry.StringOffset, &String, sizeof(String));
  expectRejected(Bytes, OL_ERRC_INVALID_BINARY);
  Bytes = package();
  Entry.StringOffset = Bytes.size() + 1;
  memcpy(Bytes.data() + Header.EntriesOffset, &Entry, sizeof(Entry));
  expectRejected(Bytes, OL_ERRC_INVALID_BINARY);

  Bytes = package();
  Header.EntriesCount = 0;
  memcpy(Bytes.data(), &Header, sizeof(Header));
  expectRejected(Bytes, OL_ERRC_INVALID_BINARY);
  Bytes = package();
  Header.EntriesCount = 1;
  Header.InflatedSize = Bytes.size();
  memcpy(Bytes.data(), &Header, sizeof(Header));
  expectRejected(Bytes, OL_ERRC_UNSUPPORTED);
}

} // namespace
