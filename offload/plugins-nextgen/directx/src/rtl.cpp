//===--- rtl.cpp - Target RTL for DirectX
//----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "DirectXFeatures.h"
#include "GlobalHandler.h"
#include "PluginInterface.h"

#include "llvm/Frontend/OpenMP/OMPGridValues.h"
#include "llvm/Object/OffloadBinary.h"
#include "llvm/Support/CheckedArithmetic.h"
#include "llvm/Support/DynamicLibrary.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/MathExtras.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <limits>
#include <map>
#include <mutex>

namespace llvm::omp::target::plugin {
namespace {

using directx::check;
using directx::DirectXBufferTy;
using directx::DirectXManifestTy;
using directx::DirectXQueueTy;
using error::ErrorCode;
using Microsoft::WRL::ComPtr;
using sys::DynamicLibrary;

Error unsupported(const char *Operation) {
  return Plugin::error(ErrorCode::UNSUPPORTED,
                       "DirectX prototype does not support %s", Operation);
}

struct DirectXProgramDataTy {
  DirectXManifestTy Manifest;
  std::unique_ptr<MemoryBuffer> Shader;
};

Expected<DirectXProgramDataTy> readProgram(StringRef Input) {
  using object::OffloadBinary;
  auto Invalid = [](const char *Message) {
    return Plugin::error(ErrorCode::INVALID_BINARY,
                         "invalid DirectX package: %s", Message);
  };
  auto Buffer = MemoryBuffer::getMemBufferCopy(Input);
  auto Header = OffloadBinary::extractHeader(Buffer->getMemBufferRef());
  if (!Header)
    return Plugin::error(ErrorCode::INVALID_BINARY, Header.takeError(),
                         "expected a DirectX OffloadBinary package");
  if ((*Header)->Version != OffloadBinary::Version ||
      OffloadBinary::isCompressed(Buffer->getMemBufferRef()))
    return unsupported("compressed or legacy OffloadBinary packages");
  if ((*Header)->Size != Input.size())
    return Invalid("trailing bytes or concatenated packages");
  if (!(*Header)->EntriesCount)
    return Invalid("package has no entries");
  if ((*Header)->EntriesCount != 1)
    return unsupported("OffloadBinary packages with more than one entry");

  // OffloadBinary's string-map views assume valid offsets and terminated keys.
  // Validate those before constructing the entry, including its alignment.
  uint64_t Offset = (*Header)->EntriesOffset;
  if (Offset % OffloadBinary::getAlignment() || Offset > Input.size() ||
      sizeof(OffloadBinary::Entry) > Input.size() - Offset)
    return Invalid("invalid entry offset");
  OffloadBinary::Entry Entry;
  memcpy(&Entry, Buffer->getBufferStart() + Offset, sizeof(Entry));
  if (Entry.StringOffset % OffloadBinary::getAlignment() ||
      Entry.StringOffset > Input.size() ||
      Entry.NumStrings > (Input.size() - Entry.StringOffset) /
                             sizeof(OffloadBinary::StringEntry))
    return Invalid("invalid string table");
  for (uint64_t I = 0; I < Entry.NumStrings; ++I) {
    OffloadBinary::StringEntry String;
    memcpy(&String,
           Buffer->getBufferStart() + Entry.StringOffset + I * sizeof(String),
           sizeof(String));
    if (String.KeyOffset >= Input.size() ||
        Input.drop_front(String.KeyOffset).find('\0') == StringRef::npos ||
        String.ValueOffset > Input.size() ||
        String.ValueSize > Input.size() - String.ValueOffset)
      return Invalid("invalid metadata string");
  }
  auto Entries = OffloadBinary::create(Buffer->getMemBufferRef());
  if (!Entries)
    return Plugin::error(ErrorCode::INVALID_BINARY, Entries.takeError(),
                         "cannot decode DirectX OffloadBinary package");
  auto &Binary = *Entries->front();
  auto Manifest = DirectXManifestTy::parse(Binary);
  if (!Manifest)
    return Manifest.takeError();
  auto Shader = MemoryBuffer::getMemBufferCopy(Binary.getImage());
  if (auto Err = Manifest->validateShader(Shader->getMemBufferRef()))
    return std::move(Err);
  return DirectXProgramDataTy{std::move(*Manifest), std::move(Shader)};
}

struct DirectXImageTy final : DeviceImageTy {
  DirectXManifestTy Manifest;
  ComPtr<ID3D12Device> NativeDevice;

  DirectXImageTy(int32_t Id, GenericDeviceTy &Device, DirectXProgramDataTy Data,
                 ID3D12Device *NativeDevice)
      : DeviceImageTy(Id, Device, std::move(Data.Shader)),
        Manifest(std::move(Data.Manifest)), NativeDevice(NativeDevice) {}
};

Expected<DirectXBufferTy> createBuffer(ID3D12Device *Device, uint64_t Size,
                                       D3D12_HEAP_TYPE Type) {
  D3D12_HEAP_PROPERTIES Heap{};
  Heap.Type = Type;
  Heap.CreationNodeMask = 1;
  Heap.VisibleNodeMask = 1;
  D3D12_RESOURCE_DESC Desc{};
  Desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  Desc.Width = Size;
  Desc.Height = 1;
  Desc.DepthOrArraySize = 1;
  Desc.MipLevels = 1;
  Desc.SampleDesc.Count = 1;
  Desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  DirectXBufferTy Buffer;
  if (Type == D3D12_HEAP_TYPE_DEFAULT)
    Desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
  else
    Buffer.State = Type == D3D12_HEAP_TYPE_UPLOAD
                       ? D3D12_RESOURCE_STATE_GENERIC_READ
                       : D3D12_RESOURCE_STATE_COPY_DEST;
  if (auto Err = check(Device->CreateCommittedResource(
                           &Heap, D3D12_HEAP_FLAG_NONE, &Desc, Buffer.State,
                           nullptr, IID_PPV_ARGS(&Buffer.Resource)),
                       "ID3D12Device::CreateCommittedResource"))
    return std::move(Err);
  return Buffer;
}

Expected<DynamicLibrary> loadRuntime(const char *Envar,
                                     const char *DefaultName) {
  const char *Name = std::getenv(Envar);
  if (!Name)
    Name = DefaultName;
  if (!*Name)
    return Plugin::error(ErrorCode::INVALID_ARGUMENT, "%s must not be empty",
                         Envar);

  std::string Message;
  // COM objects may outlive individual API calls. Keep their implementation
  // loaded for the process lifetime, as with the other driver-backed plugins.
  auto Library = DynamicLibrary::getPermanentLibrary(Name, &Message);
  if (!Library.isValid())
    return Plugin::error(ErrorCode::BACKEND_FAILURE,
                         "cannot load DirectX runtime '%s' (%s): %s", Name,
                         Envar, Message.c_str());
  return Library;
}

struct DirectXAdapterTy {
  ComPtr<IDXCoreAdapter> Adapter;
  ComPtr<ID3D12Device> Device;
  std::string Name;
  LUID Luid;
};

// These internal launch defaults are unused while execution is unsupported.
constexpr GV DirectXGridValues = {
    0, 1, D3D12_CS_DISPATCH_MAX_THREAD_GROUPS_PER_DIMENSION,
    1, 0, D3D12_CS_THREAD_GROUP_MAX_THREADS_PER_GROUP,
    1};

struct DirectXPluginContextTy;

struct DirectXDeviceTy final : GenericDeviceTy {
  ComPtr<ID3D12Device> Device;
  std::string Name;
  std::mutex ContextsMutex;
  SmallVector<DirectXPluginContextTy *> Contexts;
  std::mutex SynchronousQueueMutex;
  std::unique_ptr<DirectXQueueTy> SynchronousQueue;

  enum class CopyKind { Upload, Readback, Device };
  Expected<DirectXQueueTy *> getQueue(AsyncInfoWrapperTy &AsyncInfo) {
    if (AsyncInfo.hasQueue()) {
      auto *Queue = AsyncInfo.getQueueAs<DirectXQueueTy *>();
      if (Queue->Device.Get() != Device.Get())
        return Plugin::error(ErrorCode::INVALID_DEVICE,
                             "DirectX copy queue belongs to another device");
      return Queue;
    }
    if (!AsyncInfo.isSynchronous())
      return Plugin::error(ErrorCode::INVALID_ARGUMENT,
                           "DirectX copy has no native queue");
    std::lock_guard<std::mutex> Lock(SynchronousQueueMutex);
    if (!SynchronousQueue) {
      auto Queue = std::make_unique<DirectXQueueTy>(Device.Get());
      if (auto Err = Queue->init())
        return std::move(Err);
      SynchronousQueue = std::move(Queue);
    }
    return SynchronousQueue.get();
  }

  Expected<DirectXBufferTy> getBuffer(const void *Ptr, size_t Size,
                                      PluginContextTy *Context);
  Error copy(void *Dst, const void *Src, int64_t Size,
             AsyncInfoWrapperTy &AsyncInfo, CopyKind Kind);

  DirectXDeviceTy(GenericPluginTy &Plugin, int32_t DeviceId, int32_t NumDevices,
                  const DirectXAdapterTy &Adapter)
      : GenericDeviceTy(Plugin, DeviceId, NumDevices, DirectXGridValues),
        Device(Adapter.Device), Name(Adapter.Name) {
    setDeviceUidFromVendorUid(
        formatv("{0:x8}:{1:x8}", static_cast<uint32_t>(Adapter.Luid.HighPart),
                Adapter.Luid.LowPart)
            .str());
  }

  Error initImpl(GenericPluginTy &, GenericProfilerTy *) override {
    return Error::success();
  }
  Error deinitImpl() override {
    if (SynchronousQueue) {
      if (auto Err = SynchronousQueue->synchronize())
        return Err;
      SynchronousQueue.reset();
    }
    Device.Reset();
    return Error::success();
  }
  Error setContext() override { return Error::success(); }
  uint32_t getDriverId() const override { return getDeviceId(); }

  Expected<InfoTreeNode> obtainInfoImpl() override {
    InfoTreeNode Info;
    Info.add("Name", Name, "", DeviceInfo::NAME);
    Info.add("Product name", Name, "", DeviceInfo::PRODUCT_NAME);
    return Info;
  }
  Expected<void *> getNativeHandle() override {
    return static_cast<void *>(Device.Get());
  }
  Error getDeviceStackSize(uint64_t &Value) override {
    Value = 0;
    return Error::success();
  }
  Error setDeviceStackSize(uint64_t) override { return Error::success(); }

  Expected<DeviceImageTy *>
  loadBinaryImpl(std::unique_ptr<MemoryBuffer> &&Buffer, int32_t Id,
                 PluginContextTy *Context) override {
    if (!Context)
      return unsupported("image loading without a user context");
    auto Data = readProgram(Buffer->getBuffer());
    if (!Data)
      return Data.takeError();
    if (auto Err =
            directx::checkDeviceCapabilities(Data->Manifest, *Device.Get()))
      return std::move(Err);
    return new DirectXImageTy(Id, *this, std::move(*Data), Device.Get());
  }
  Error unloadBinaryImpl(DeviceImageTy *Image) override {
    delete static_cast<DirectXImageTy *>(Image);
    return Error::success();
  }
  Expected<GenericKernelTy &> constructKernel(StringRef) override {
    return unsupported("kernels");
  }
  Expected<void *> allocate(size_t, ol_alloc_type_t, size_t) override {
    return unsupported("memory allocation");
  }
  Error free(void *, ol_alloc_type_t) override {
    return unsupported("memory deallocation");
  }
  Expected<void *> dataLockImpl(void *, int64_t) override {
    return unsupported("host memory registration");
  }
  Error dataUnlockImpl(void *) override {
    return unsupported("host memory unregistration");
  }
  Expected<bool> isPinnedPtrImpl(void *, void *&, void *&,
                                 size_t &) const override {
    return unsupported("pinned memory queries");
  }
  Error dataSubmitImpl(void *Dst, const void *Src, int64_t Size,
                       AsyncInfoWrapperTy &AsyncInfo,
                       GenericProfilerTy *) override {
    return copy(Dst, Src, Size, AsyncInfo, CopyKind::Upload);
  }
  Error dataRetrieveImpl(void *Dst, const void *Src, int64_t Size,
                         AsyncInfoWrapperTy &AsyncInfo,
                         GenericProfilerTy *) override {
    return copy(Dst, Src, Size, AsyncInfo, CopyKind::Readback);
  }
  Error dataMemcpyImpl(void *Dst, const void *Src, int64_t Size,
                       AsyncInfoWrapperTy &AsyncInfo) override {
    if (Size < 0)
      return Plugin::error(ErrorCode::INVALID_SIZE,
                           "DirectX copy size is not representable");
    auto Queue = getQueue(AsyncInfo);
    if (!Queue)
      return Queue.takeError();
    if (auto Err = (*Queue)->synchronize())
      return Err;
    std::memcpy(Dst, Src, Size);
    return Error::success();
  }
  Error dataExchangeImpl(const void *Src, GenericDeviceTy &DstDevice, void *Dst,
                         int64_t Size, AsyncInfoWrapperTy &AsyncInfo,
                         GenericProfilerTy *) override {
    if (&DstDevice != this)
      return unsupported("peer copies");
    return copy(Dst, Src, Size, AsyncInfo, CopyKind::Device);
  }
  Error dataFillImpl(void *, const void *, int64_t, int64_t,
                     AsyncInfoWrapperTy &) override {
    return unsupported("memory fills");
  }
  Error dataPrefetchImpl(size_t, const void **, const size_t *, bool,
                         AsyncInfoWrapperTy &) override {
    return unsupported("memory prefetch");
  }
  Error enqueueHostCallImpl(void (*)(void *), void *,
                            AsyncInfoWrapperTy &) override {
    return unsupported("queued host calls");
  }
  Error synchronizeImpl(__tgt_async_info &AsyncInfo,
                        bool ReleaseQueue) override {
    auto *Queue = static_cast<DirectXQueueTy *>(AsyncInfo.Queue);
    if (!Queue)
      return Error::success();
    if (auto Err = Queue->synchronize())
      return Err;
    if (ReleaseQueue) {
      delete Queue;
      AsyncInfo.Queue = nullptr;
    }
    return Error::success();
  }
  Error queryAsyncImpl(__tgt_async_info &AsyncInfo, bool ReleaseQueue,
                       bool *IsQueueWorkCompleted) override {
    auto *Queue = static_cast<DirectXQueueTy *>(AsyncInfo.Queue);
    bool Completed = true;
    if (Queue) {
      auto Pending = Queue->hasPendingWork();
      if (!Pending)
        return Pending.takeError();
      Completed = !*Pending;
    }
    if (IsQueueWorkCompleted)
      *IsQueueWorkCompleted = Completed;
    if (Completed && ReleaseQueue) {
      delete Queue;
      AsyncInfo.Queue = nullptr;
    }
    return Error::success();
  }
  Expected<bool> hasPendingWorkImpl(AsyncInfoWrapperTy &AsyncInfo) override {
    auto *Queue = AsyncInfo.getQueueAs<DirectXQueueTy *>();
    if (!Queue)
      return false;
    return Queue->hasPendingWork();
  }
  Expected<void *> getNativeQueueHandle(__tgt_async_info *AsyncInfo) override {
    if (!AsyncInfo || !AsyncInfo->Queue)
      return Plugin::error(ErrorCode::INVALID_ARGUMENT,
                           "no DirectX queue associated with the async info");
    return static_cast<void *>(
        static_cast<DirectXQueueTy *>(AsyncInfo->Queue)->Queue.Get());
  }
  Error createEventImpl(void **, bool) override {
    return unsupported("event creation");
  }
  Error destroyEventImpl(void *, bool) override {
    return unsupported("event destruction");
  }
  Error recordEventImpl(void *, AsyncInfoWrapperTy &, bool) override {
    return unsupported("event recording");
  }
  Error waitEventImpl(void *, AsyncInfoWrapperTy &) override {
    return unsupported("event waits");
  }
  Expected<bool> isEventCompleteImpl(void *, AsyncInfoWrapperTy &) override {
    return unsupported("event queries");
  }
  Error syncEventImpl(void *) override {
    return unsupported("event synchronization");
  }
  Expected<float> getEventElapsedTimeImpl(void *, void *) override {
    return unsupported("event timing");
  }
};

struct DirectXPluginContextTy final : PluginContextTy {
  DirectXDeviceTy &Owner;
  ComPtr<ID3D12Device> Device;
  std::mutex AllocationsMutex;
  // Pool backing resources and live user allocations have different lifetimes.
  std::map<uintptr_t, ComPtr<ID3D12Resource>> Resources;
  std::map<uintptr_t, PluginAllocInfoTy> Allocations;

  DirectXPluginContextTy(GenericPluginTy &Plugin,
                         ArrayRef<GenericDeviceTy *> Devices,
                         const ComPtr<ID3D12Device> &Device)
      : PluginContextTy(Plugin, Devices),
        Owner(static_cast<DirectXDeviceTy &>(*Devices.front())),
        Device(Device) {
    std::lock_guard<std::mutex> Lock(Owner.ContextsMutex);
    Owner.Contexts.push_back(this);
  }

  ~DirectXPluginContextTy() override {
    std::lock_guard<std::mutex> Lock(Owner.ContextsMutex);
    llvm::erase(Owner.Contexts, this);
  }

  Error deinit() override {
    std::lock_guard<std::mutex> Lock(AllocationsMutex);
    Allocations.clear();
    Resources.clear();
    Device.Reset();
    return Error::success();
  }
  Expected<void *> getNativeHandle() override {
    return static_cast<void *>(Device.Get());
  }
  Error initAsyncInfoImpl(GenericDeviceTy &,
                          AsyncInfoWrapperTy &AsyncInfo) override {
    auto Queue = std::make_unique<DirectXQueueTy>(Device.Get(), this);
    if (auto Err = Queue->init())
      return Err;
    __tgt_async_info *Info = AsyncInfo;
    Info->Queue = Queue.release();
    return Error::success();
  }
  Expected<void *> allocateImpl(GenericDeviceTy &, int64_t Size,
                                ol_alloc_type_t Kind, size_t Alignment,
                                GenericProfilerTy *) override {
    if (Kind != OL_ALLOC_TYPE_DEVICE)
      return unsupported("host or managed memory allocation");
    if (Size <= 0)
      return Plugin::error(ErrorCode::INVALID_SIZE,
                           "DirectX buffer size must be positive");
    Alignment = std::max(Alignment, alignof(std::max_align_t));
    auto Width = checkedAddUnsigned<uint64_t>(static_cast<uint64_t>(Size),
                                              Alignment - 1);
    if (!Width)
      return Plugin::error(ErrorCode::INVALID_SIZE,
                           "DirectX buffer size and alignment overflow");

    auto Buffer = createBuffer(Device.Get(), *Width, D3D12_HEAP_TYPE_DEFAULT);
    if (!Buffer)
      return Buffer.takeError();
    auto &Resource = Buffer->Resource;

    uint64_t Address = Resource->GetGPUVirtualAddress();
    if (!Address || *Width > std::numeric_limits<uintptr_t>::max() ||
        Address > std::numeric_limits<uintptr_t>::max() - (*Width - 1))
      return Plugin::error(ErrorCode::BACKEND_FAILURE,
                           "DirectX buffer address is not representable");
    uintptr_t AlignedAddress = alignTo(Address, Alignment);
    {
      std::lock_guard<std::mutex> Lock(AllocationsMutex);
      if (!Resources.emplace(AlignedAddress, std::move(Resource)).second)
        return Plugin::error(ErrorCode::BACKEND_FAILURE,
                             "DirectX runtime returned a duplicate address");
    }
    return reinterpret_cast<void *>(AlignedAddress);
  }

  Error deallocateImpl(GenericDeviceTy &, void *Ptr, ol_alloc_type_t,
                       GenericProfilerTy *) override {
    std::lock_guard<std::mutex> Lock(AllocationsMutex);
    auto It = Resources.find(reinterpret_cast<uintptr_t>(Ptr));
    if (It == Resources.end())
      return Plugin::error(ErrorCode::NOT_FOUND,
                           "pointer is not a DirectX buffer allocation base");
    Resources.erase(It);
    return Error::success();
  }

  void trackAllocation(GenericDeviceTy &AllocDevice, void *Ptr, int64_t Size,
                       ol_alloc_type_t Kind) override {
    std::lock_guard<std::mutex> Lock(AllocationsMutex);
    Allocations[reinterpret_cast<uintptr_t>(Ptr)] =
        PluginAllocInfoTy{&AllocDevice, Kind, Ptr, static_cast<size_t>(Size)};
  }

  void untrackAllocation(void *Ptr) override {
    std::lock_guard<std::mutex> Lock(AllocationsMutex);
    Allocations.erase(reinterpret_cast<uintptr_t>(Ptr));
  }

  Expected<PluginAllocInfoTy> getAllocInfo(const void *Ptr) override {
    uintptr_t Address = reinterpret_cast<uintptr_t>(Ptr);
    std::lock_guard<std::mutex> Lock(AllocationsMutex);
    auto It = Allocations.upper_bound(Address);
    if (It != Allocations.begin()) {
      --It;
      if (Address - It->first < It->second.Size)
        return It->second;
    }
    return Plugin::error(
        ErrorCode::NOT_FOUND,
        "pointer is not a known DirectX allocation in this context");
  }

  Expected<DirectXBufferTy> getBuffer(const void *Ptr, size_t Size) {
    uintptr_t Address = reinterpret_cast<uintptr_t>(Ptr);
    std::lock_guard<std::mutex> Lock(AllocationsMutex);
    auto Allocation = Allocations.upper_bound(Address);
    if (Allocation == Allocations.begin() ||
        Address - std::prev(Allocation)->first >=
            std::prev(Allocation)->second.Size)
      return Plugin::error(ErrorCode::NOT_FOUND,
                           "pointer is not a live DirectX allocation");
    --Allocation;
    if (Size > Allocation->second.Size - (Address - Allocation->first))
      return Plugin::error(
          ErrorCode::INVALID_SIZE,
          "DirectX copy exceeds the requested allocation size");
    auto Resource = Resources.upper_bound(Address);
    if (Resource == Resources.begin())
      return Plugin::error(ErrorCode::BACKEND_FAILURE,
                           "DirectX allocation has no backing resource");
    --Resource;
    return directx::getBufferFromAddress(Resource->second, Address, Size);
  }
};

Expected<DirectXBufferTy> DirectXDeviceTy::getBuffer(const void *Ptr,
                                                     size_t Size,
                                                     PluginContextTy *Context) {
  if (Context)
    return static_cast<DirectXPluginContextTy *>(Context)->getBuffer(Ptr, Size);
  std::lock_guard<std::mutex> Lock(ContextsMutex);
  for (auto *Candidate : Contexts) {
    auto Buffer = Candidate->getBuffer(Ptr, Size);
    if (Buffer)
      return std::move(*Buffer);
    if (auto Err =
            error::consumeErrorIfCode(Buffer.takeError(), ErrorCode::NOT_FOUND))
      return std::move(Err);
  }
  return Plugin::error(
      ErrorCode::NOT_FOUND,
      "pointer is not a live allocation on this DirectX device");
}

Error DirectXDeviceTy::copy(void *Dst, const void *Src, int64_t Size,
                            AsyncInfoWrapperTy &AsyncInfo, CopyKind Kind) {
  if (Size < 0)
    return Plugin::error(ErrorCode::INVALID_SIZE,
                         "DirectX copy size is not representable");
  if (Size == 0)
    return Error::success();
  auto Queue = getQueue(AsyncInfo);
  if (!Queue)
    return Queue.takeError();
  DirectXBufferTy Destination, Source;
  if (Kind != CopyKind::Readback) {
    auto Buffer = getBuffer(Dst, Size, (*Queue)->Context);
    if (!Buffer)
      return Buffer.takeError();
    Destination = std::move(*Buffer);
  }
  if (Kind != CopyKind::Upload) {
    auto Buffer = getBuffer(Src, Size, (*Queue)->Context);
    if (!Buffer)
      return Buffer.takeError();
    Source = std::move(*Buffer);
  }
  if (Kind == CopyKind::Device) {
    if (Destination.Resource.Get() == Source.Resource.Get()) {
      auto Temporary =
          createBuffer(Device.Get(), Size, D3D12_HEAP_TYPE_DEFAULT);
      if (!Temporary)
        return Temporary.takeError();
      if (auto Err = (*Queue)->copy(*Temporary, Source, Size))
        return Err;
      Source = std::move(*Temporary);
    }
    return (*Queue)->copy(Destination, Source, Size);
  }

  bool Upload = Kind == CopyKind::Upload;
  auto Staging =
      createBuffer(Device.Get(), Size,
                   Upload ? D3D12_HEAP_TYPE_UPLOAD : D3D12_HEAP_TYPE_READBACK);
  if (!Staging)
    return Staging.takeError();
  if (!Upload)
    if (auto Err = (*Queue)->copy(*Staging, Source, Size))
      return Err;
  void *Mapped = nullptr;
  D3D12_RANGE ReadRange{0, Upload ? 0 : static_cast<size_t>(Size)};
  if (auto Err = check(Staging->Resource->Map(0, &ReadRange, &Mapped),
                       "ID3D12Resource::Map"))
    return Err;
  if (Upload)
    std::memcpy(Mapped, Src, Size);
  else
    std::memcpy(Dst, Mapped, Size);
  D3D12_RANGE WrittenRange{0, Upload ? static_cast<size_t>(Size) : 0};
  Staging->Resource->Unmap(0, &WrittenRange);
  if (Upload)
    return (*Queue)->copy(Destination, *Staging, Size);
  return Error::success();
}

struct DirectXGlobalHandlerTy final : GenericGlobalHandlerTy {
  Error getGlobalMetadataFromDevice(GenericDeviceTy &, DeviceImageTy &,
                                    GlobalTy &) override {
    return unsupported("device globals");
  }
  Error iterateSymbols(DeviceImageTy &, SymbolKindTy,
                       function_ref<bool(StringRef)>) override {
    return unsupported("symbol enumeration");
  }
};

struct DirectXPluginTy final : GenericPluginTy {
  SmallVector<DirectXAdapterTy> Adapters;

  DirectXPluginTy() : GenericPluginTy(Triple::dxil) {}

  Expected<int32_t> initImpl() override {
#ifdef _WIN32
    constexpr const char *DefaultD3D12 = "d3d12.dll";
    constexpr const char *DefaultDXCore = "dxcore.dll";
#else
    constexpr const char *DefaultD3D12 = "libnacelle_d3d12.so";
    constexpr const char *DefaultDXCore = "libnacelle_d3d12.so";
#endif
    auto DXCore = loadRuntime("OFFLOAD_DIRECTX_DXCORE_LIBRARY", DefaultDXCore);
    if (!DXCore)
      return DXCore.takeError();
    auto D3D12 = loadRuntime("OFFLOAD_DIRECTX_D3D12_LIBRARY", DefaultD3D12);
    if (!D3D12)
      return D3D12.takeError();

    using CreateFactoryTy = HRESULT(STDAPICALLTYPE *)(REFIID, void **);
    auto CreateFactory = reinterpret_cast<CreateFactoryTy>(
        DXCore->getAddressOfSymbol("DXCoreCreateAdapterFactory"));
    auto CreateDevice = reinterpret_cast<PFN_D3D12_CREATE_DEVICE>(
        D3D12->getAddressOfSymbol("D3D12CreateDevice"));
    if (!CreateFactory)
      return Plugin::error(
          ErrorCode::BACKEND_FAILURE,
          "DirectX DXCore runtime is missing DXCoreCreateAdapterFactory");
    if (!CreateDevice)
      return Plugin::error(
          ErrorCode::BACKEND_FAILURE,
          "DirectX D3D12 runtime is missing D3D12CreateDevice");

    ComPtr<IDXCoreAdapterFactory> Factory;
    if (auto Err = check(CreateFactory(IID_PPV_ARGS(&Factory)),
                         "DXCoreCreateAdapterFactory"))
      return std::move(Err);
    const GUID Attributes[] = {DXCORE_ADAPTER_ATTRIBUTE_D3D12_GRAPHICS};
    ComPtr<IDXCoreAdapterList> List;
    if (auto Err = check(
            Factory->CreateAdapterList(1, Attributes, IID_PPV_ARGS(&List)),
            "IDXCoreAdapterFactory::CreateAdapterList"))
      return std::move(Err);
    uint32_t Count = List->GetAdapterCount();
    if (Count > static_cast<uint32_t>(std::numeric_limits<int32_t>::max()))
      return Plugin::error(ErrorCode::OUT_OF_RESOURCES,
                           "too many DirectX adapters");

    SmallVector<DirectXAdapterTy> Found;
    for (uint32_t I = 0; I < Count; ++I) {
      DirectXAdapterTy Entry;
      if (auto Err = check(List->GetAdapter(I, IID_PPV_ARGS(&Entry.Adapter)),
                           "IDXCoreAdapterList::GetAdapter"))
        return std::move(Err);
      if (auto Err =
              check(CreateDevice(Entry.Adapter.Get(), D3D_FEATURE_LEVEL_11_0,
                                 IID_PPV_ARGS(&Entry.Device)),
                    "D3D12CreateDevice"))
        return std::move(Err);

      size_t NameSize;
      if (auto Err =
              check(Entry.Adapter->GetPropertySize(
                        DXCoreAdapterProperty::DriverDescription, &NameSize),
                    "IDXCoreAdapter::GetPropertySize(DriverDescription)"))
        return std::move(Err);
      SmallVector<char> Name(NameSize);
      if (Name.empty())
        return Plugin::error(ErrorCode::BACKEND_FAILURE,
                             "DirectX adapter has an empty description");
      if (auto Err = check(Entry.Adapter->GetProperty(
                               DXCoreAdapterProperty::DriverDescription,
                               Name.size(), Name.data()),
                           "IDXCoreAdapter::GetProperty(DriverDescription)"))
        return std::move(Err);
      if (Name.back() != '\0')
        return Plugin::error(ErrorCode::BACKEND_FAILURE,
                             "DirectX adapter description is not terminated");
      Entry.Name.assign(Name.data(), Name.size() - 1);
      if (auto Err = check(
              Entry.Adapter->GetProperty(DXCoreAdapterProperty::InstanceLuid,
                                         sizeof(Entry.Luid), &Entry.Luid),
              "IDXCoreAdapter::GetProperty(InstanceLuid)"))
        return std::move(Err);
      Found.push_back(std::move(Entry));
    }
    Adapters = std::move(Found);
    return static_cast<int32_t>(Adapters.size());
  }

  Error deinitImpl() override {
    Adapters.clear();
    return Error::success();
  }
  GenericDeviceTy *createDevice(GenericPluginTy &Plugin, int32_t DeviceId,
                                int32_t NumDevices) override {
    return new DirectXDeviceTy(Plugin, DeviceId, NumDevices,
                               Adapters[DeviceId]);
  }
  GenericGlobalHandlerTy *createGlobalHandler() override {
    return new DirectXGlobalHandlerTy;
  }
  uint16_t getMagicElfBits() const override { return ELF::EM_NONE; }
  Triple::ArchType getTripleArch() const override { return Triple::dxil; }
  const char *getName() const override { return "DIRECTX"; }
  bool supportsJIT() const override { return false; }
  Expected<bool> isELFCompatible(uint32_t, StringRef) const override {
    return false;
  }
  Expected<bool> isImageCompatible(StringRef Image) const override {
    auto Data = readProgram(Image);
    if (!Data)
      return Data.takeError();
    return true;
  }
  Expected<bool> isImageCompatible(uint32_t DeviceId,
                                   StringRef Image) const override {
    if (DeviceId >= Adapters.size())
      return Plugin::error(ErrorCode::INVALID_DEVICE, "invalid DirectX device");
    auto Data = readProgram(Image);
    if (!Data)
      return Data.takeError();
    if (auto Err = directx::checkDeviceCapabilities(
            Data->Manifest, *Adapters[DeviceId].Device.Get()))
      return std::move(Err);
    return true;
  }
  bool isDataExchangable(int32_t SrcDeviceId, int32_t DstDeviceId) override {
    return SrcDeviceId == DstDeviceId && isValidDeviceId(SrcDeviceId);
  }
  Expected<std::unique_ptr<PluginContextTy>>
  createPluginContext(ArrayRef<GenericDeviceTy *> Devices) override {
    if (Devices.empty())
      return Plugin::error(ErrorCode::INVALID_ARGUMENT,
                           "createPluginContext called with no devices");
    if (Devices.size() != 1)
      return unsupported("contexts with more than one device entry");

    auto &Device = static_cast<DirectXDeviceTy &>(*Devices.front());
    return std::make_unique<DirectXPluginContextTy>(*this, Devices,
                                                    Device.Device);
  }
};

} // namespace
} // namespace llvm::omp::target::plugin

extern "C" llvm::omp::target::plugin::GenericPluginTy *createPlugin_directx() {
  return new llvm::omp::target::plugin::DirectXPluginTy;
}
