//===--- rtl.cpp - Target RTL for DirectX
//----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "DirectXArguments.h"
#include "DirectXFeatures.h"
#include "DirectXRootSignature.h"
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

using DirectXQueueHandleTy = std::shared_ptr<DirectXQueueTy>;

struct DirectXEventTimingTy {
  ComPtr<ID3D12QueryHeap> Heap;
  ComPtr<ID3D12Resource> Readback;
  uint64_t Timestamp = 0;
  uint64_t Frequency = 0;
#ifdef _WIN32
  uint64_t GPUCalibration = 0, CPUCalibration = 0;
  uint64_t CPUFrequency = 0;
#endif
};

struct DirectXEventTy {
  DirectXQueueHandleTy Queue;
  uint64_t Value = 0;
  std::shared_ptr<DirectXEventTimingTy> Timing;
};

Error unsupported(const char *Operation) {
  return Plugin::error(ErrorCode::UNSUPPORTED,
                       "DirectX prototype does not support %s", Operation);
}

struct DirectXProgramDataTy {
  DirectXManifestTy Manifest;
  std::unique_ptr<MemoryBuffer> Shader;
  uint32_t StaticSharedMemory = 0;
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
  uint32_t StaticSharedMemory = 0;
  if (auto Err = Manifest->validateShader(Shader->getMemBufferRef(),
                                          &StaticSharedMemory))
    return std::move(Err);
  return DirectXProgramDataTy{std::move(*Manifest), std::move(Shader),
                              StaticSharedMemory};
}

struct DirectXKernelTy;

struct DirectXImageTy final : DeviceImageTy {
  DirectXManifestTy Manifest;
  uint32_t StaticSharedMemory;
  ComPtr<ID3D12Device> NativeDevice;
  ComPtr<ID3D12RootSignature> RootSignature;
  PluginContextTy *Context;
  SmallVector<DirectXKernelTy *> Kernels;

  DirectXImageTy(int32_t Id, GenericDeviceTy &Device, DirectXProgramDataTy Data,
                 ID3D12Device *NativeDevice,
                 ComPtr<ID3D12RootSignature> RootSignature,
                 PluginContextTy *Context)
      : DeviceImageTy(Id, Device, std::move(Data.Shader)),
        Manifest(std::move(Data.Manifest)),
        StaticSharedMemory(Data.StaticSharedMemory), NativeDevice(NativeDevice),
        RootSignature(std::move(RootSignature)), Context(Context) {}

  ~DirectXImageTy() override;
};

struct DirectXKernelTy final : GenericKernelTy {
  ComPtr<ID3D12PipelineState> Pipeline;
  DirectXImageTy *Program = nullptr;
  uint32_t NumThreads = 0;

  DirectXKernelTy(StringRef Name) : GenericKernelTy(Name) {}

  Error initImpl(GenericDeviceTy &, DeviceImageTy &Image) override {
    auto &DXImage = static_cast<DirectXImageTy &>(Image);
    if (DXImage.Manifest.Entry != getName())
      return Plugin::error(ErrorCode::NOT_FOUND,
                           "DirectX kernel '%s' is not the program entry point",
                           getName());
    D3D12_COMPUTE_PIPELINE_STATE_DESC Desc{};
    Desc.pRootSignature = DXImage.RootSignature.Get();
    Desc.CS = {DXImage.getStart(), DXImage.getSize()};
    if (auto Err = check(DXImage.NativeDevice->CreateComputePipelineState(
                             &Desc, IID_PPV_ARGS(&Pipeline)),
                         "ID3D12Device::CreateComputePipelineState"))
      return Err;
    if (!Pipeline)
      return Plugin::error(ErrorCode::BACKEND_FAILURE,
                           "DirectX device returned no compute pipeline");
    const auto &Threads = DXImage.Manifest.NumThreads;
    NumThreads = Threads[0] * Threads[1] * Threads[2];
    StaticBlockMemSize = DXImage.StaticSharedMemory;
    Program = &DXImage;
    DXImage.Kernels.push_back(this);
    return Error::success();
  }

  Error launchImpl(GenericDeviceTy &Device, uint32_t NumThreads[3],
                   uint32_t NumBlocks[3], uint32_t DynBlockMemSize,
                   KernelLaunchArgsTy &Args, AsyncInfoWrapperTy &AsyncInfo,
                   GenericProfilerTy *) const override;

  Expected<uint64_t> maxGroupSize(GenericDeviceTy &, uint64_t) const override {
    return unsupported("occupancy calculations");
  }

  uint32_t getMaxThreads() const override { return NumThreads; }
};

DirectXImageTy::~DirectXImageTy() {
  // liboffload owns symbol wrappers, but not their backend kernel objects.
  for (auto *Kernel : Kernels)
    getDevice().Plugin.free(Kernel);
}

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
  PFN_D3D12_SERIALIZE_ROOT_SIGNATURE SerializeRootSignature = nullptr;
  std::string Name;
  LUID Luid;
};

constexpr GV DirectXGridValues = {
    0, 1, D3D12_CS_DISPATCH_MAX_THREAD_GROUPS_PER_DIMENSION,
    1, 0, D3D12_CS_THREAD_GROUP_MAX_THREADS_PER_GROUP,
    1};

struct DirectXPluginContextTy;

struct DirectXDeviceTy final : GenericDeviceTy {
  ComPtr<IDXCoreAdapter> Adapter;
  ComPtr<ID3D12Device> Device;
  PFN_D3D12_SERIALIZE_ROOT_SIGNATURE SerializeRootSignature;
  std::string Name;
  std::mutex ContextsMutex;
  SmallVector<DirectXPluginContextTy *> Contexts;
  std::mutex SynchronousQueueMutex;
  std::unique_ptr<DirectXQueueTy> SynchronousQueue;

  enum class CopyKind { Upload, Readback, Device };
  Expected<DirectXQueueTy *> getQueue(AsyncInfoWrapperTy &AsyncInfo) {
    if (AsyncInfo.hasQueue()) {
      auto *Queue = AsyncInfo.getQueueAs<DirectXQueueHandleTy *>()->get();
      if (Queue->Device.Get() != Device.Get())
        return Plugin::error(ErrorCode::INVALID_DEVICE,
                             "DirectX queue belongs to another device");
      return Queue;
    }
    if (!AsyncInfo.isSynchronous())
      return Plugin::error(ErrorCode::INVALID_ARGUMENT,
                           "DirectX operation has no native queue");
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
        Adapter(Adapter.Adapter), Device(Adapter.Device),
        SerializeRootSignature(Adapter.SerializeRootSignature),
        Name(Adapter.Name) {
    setDeviceUidFromVendorUid(
        formatv("{0:x8}:{1:x8}", static_cast<uint32_t>(Adapter.Luid.HighPart),
                Adapter.Luid.LowPart)
            .str());
  }

  Error initImpl(GenericPluginTy &, GenericProfilerTy *) override {
    MaxBlockSharedMemSize =
        uint64_t{D3D12_CS_TGSM_REGISTER_COUNT} * sizeof(uint32_t);
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
    uint32_t Vendor = 0;
    bool HasVendor = false;
    if (Adapter->IsPropertySupported(DXCoreAdapterProperty::HardwareIDParts)) {
      DXCoreHardwareIDParts ID{};
      if (auto Err =
              check(Adapter->GetProperty(DXCoreAdapterProperty::HardwareIDParts,
                                         sizeof(ID), &ID),
                    "IDXCoreAdapter::GetProperty(HardwareIDParts)"))
        return std::move(Err);
      Vendor = ID.vendorID;
      HasVendor = true;
    } else if (Adapter->IsPropertySupported(
                   DXCoreAdapterProperty::HardwareID)) {
      DXCoreHardwareID ID{};
      if (auto Err =
              check(Adapter->GetProperty(DXCoreAdapterProperty::HardwareID,
                                         sizeof(ID), &ID),
                    "IDXCoreAdapter::GetProperty(HardwareID)"))
        return std::move(Err);
      Vendor = ID.vendorID;
      HasVendor = true;
    }
    if (HasVendor) {
      std::string VendorName;
      switch (Vendor) {
      case 0x1002:
        VendorName = "AMD";
        break;
      case 0x10de:
        VendorName = "NVIDIA";
        break;
      case 0x1414:
        VendorName = "Microsoft";
        break;
      case 0x8086:
        VendorName = "Intel";
        break;
      default:
        VendorName = formatv("PCI vendor {0:x4}", Vendor).str();
        break;
      }
      Info.add("Vendor", VendorName, "", DeviceInfo::VENDOR);
      Info.add("Vendor ID", Vendor, "", DeviceInfo::VENDOR_ID);
    }
    if (Adapter->IsPropertySupported(DXCoreAdapterProperty::DriverVersion)) {
      uint64_t Version = 0;
      if (auto Err =
              check(Adapter->GetProperty(DXCoreAdapterProperty::DriverVersion,
                                         sizeof(Version), &Version),
                    "IDXCoreAdapter::GetProperty(DriverVersion)"))
        return std::move(Err);
      Info.add("Driver version",
               formatv("{0}.{1}.{2}.{3}", Version >> 48,
                       (Version >> 32) & 0xffff, (Version >> 16) & 0xffff,
                       Version & 0xffff)
                   .str(),
               "", DeviceInfo::DRIVER_VERSION);
    }
    Info.add("Maximum work group size",
             D3D12_CS_THREAD_GROUP_MAX_THREADS_PER_GROUP, "",
             DeviceInfo::MAX_WORK_GROUP_SIZE);
    auto &Group = *Info.add("Work group limits", std::monostate{}, "",
                            DeviceInfo::MAX_WORK_GROUP_SIZE_PER_DIMENSION);
    Group.add("x", D3D12_CS_THREAD_GROUP_MAX_X);
    Group.add("y", D3D12_CS_THREAD_GROUP_MAX_Y);
    Group.add("z", D3D12_CS_THREAD_GROUP_MAX_Z);
    auto &Grid = *Info.add("Work item limits", std::monostate{}, "",
                           DeviceInfo::MAX_WORK_SIZE_PER_DIMENSION);
    Grid.add("x", uint64_t{D3D12_CS_THREAD_GROUP_MAX_X} *
                      D3D12_CS_DISPATCH_MAX_THREAD_GROUPS_PER_DIMENSION);
    Grid.add("y", uint64_t{D3D12_CS_THREAD_GROUP_MAX_Y} *
                      D3D12_CS_DISPATCH_MAX_THREAD_GROUPS_PER_DIMENSION);
    Grid.add("z", uint64_t{D3D12_CS_THREAD_GROUP_MAX_Z} *
                      D3D12_CS_DISPATCH_MAX_THREAD_GROUPS_PER_DIMENSION);
    Info.add("Shared memory per work group",
             uint64_t{D3D12_CS_TGSM_REGISTER_COUNT} * sizeof(uint32_t), "bytes",
             DeviceInfo::WORK_GROUP_LOCAL_MEM_SIZE);
    Info.add("Address bits", uint64_t{64}, "", DeviceInfo::ADDRESS_BITS);
    Info.add("Cooperative launches", false, "",
             DeviceInfo::COOPERATIVE_LAUNCH_SUPPORT);
    D3D12_FEATURE_DATA_D3D12_OPTIONS Options{};
    if (auto Err =
            check(Device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS,
                                              &Options, sizeof(Options)),
                  "ID3D12Device::CheckFeatureSupport(Options)"))
      return std::move(Err);
    Info.add("Single precision", true, "", DeviceInfo::SINGLE_FP_SUPPORT);
    Info.add("Double precision", bool(Options.DoublePrecisionFloatShaderOps),
             "", DeviceInfo::DOUBLE_FP_SUPPORT);
    D3D12_FEATURE_DATA_D3D12_OPTIONS4 Options4{};
    HRESULT Status = Device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS4,
                                                 &Options4, sizeof(Options4));
    if (Status != E_INVALIDARG)
      if (auto Err =
              check(Status, "ID3D12Device::CheckFeatureSupport(Options4)"))
        return std::move(Err);
    Info.add("Native half precision",
             SUCCEEDED(Status) && bool(Options4.Native16BitShaderOpsSupported),
             "", DeviceInfo::HALF_FP_SUPPORT);
    D3D12_FEATURE_DATA_D3D12_OPTIONS1 Options1{};
    Status = Device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS1,
                                         &Options1, sizeof(Options1));
    if (Status != E_INVALIDARG) {
      if (auto Err =
              check(Status, "ID3D12Device::CheckFeatureSupport(Options1)"))
        return std::move(Err);
      if (Options1.WaveOps && Options1.WaveLaneCountMin &&
          Options1.WaveLaneCountMin == Options1.WaveLaneCountMax)
        Info.add("Wave lanes", Options1.WaveLaneCountMin, "",
                 DeviceInfo::NUM_LANES);
    }
    return Info;
  }
  Error getDeviceMemorySize(uint64_t &Size) override {
    bool Integrated = false;
    if (!Adapter->IsPropertySupported(DXCoreAdapterProperty::IsIntegrated))
      return unsupported("adapter memory model queries");
    if (auto Err =
            check(Adapter->GetProperty(DXCoreAdapterProperty::IsIntegrated,
                                       sizeof(Integrated), &Integrated),
                  "IDXCoreAdapter::GetProperty(IsIntegrated)"))
      return Err;
    uint64_t Dedicated = 0, System = 0, Shared = 0;
    for (auto [Property, Destination] :
         {std::pair{DXCoreAdapterProperty::DedicatedAdapterMemory, &Dedicated},
          std::pair{DXCoreAdapterProperty::DedicatedSystemMemory, &System},
          std::pair{DXCoreAdapterProperty::SharedSystemMemory, &Shared}}) {
      if (!Adapter->IsPropertySupported(Property))
        return unsupported("adapter memory capacity queries");
      if (auto Err = check(
              Adapter->GetProperty(Property, sizeof(uint64_t), Destination),
              "IDXCoreAdapter::GetProperty(memory capacity)"))
        return Err;
    }
    auto Total = checkedAddUnsigned(Dedicated, System);
    if (!Total || (!*Total && !Shared))
      return Plugin::error(ErrorCode::BACKEND_FAILURE,
                           "DirectX adapter returned invalid memory capacity");
    Size = (Integrated && Shared) || !*Total ? Shared : *Total;
    return Error::success();
  }
  uint64_t getClockFrequency() const override {
    // D3D12 queue timestamps are not the device-code clock queried by this API.
    return 0;
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
    ComPtr<ID3D12RootSignature> Root;
    if (auto Err = directx::createRootSignature(Data->Manifest, *Device.Get(),
                                                SerializeRootSignature, Root))
      return std::move(Err);
    return new DirectXImageTy(Id, *this, std::move(*Data), Device.Get(),
                              std::move(Root), Context);
  }
  Error unloadBinaryImpl(DeviceImageTy *Image) override {
    delete static_cast<DirectXImageTy *>(Image);
    return Error::success();
  }
  Expected<GenericKernelTy &> constructKernel(StringRef Name) override {
    auto *Kernel = Plugin.allocate<DirectXKernelTy>();
    if (!Kernel)
      return Plugin::error(ErrorCode::OUT_OF_RESOURCES,
                           "failed to allocate DirectX kernel");
    return *new (Kernel) DirectXKernelTy(Name);
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
  Error dataFillImpl(void *Ptr, const void *Pattern, int64_t PatternSize,
                     int64_t Size, AsyncInfoWrapperTy &AsyncInfo) override {
    if (PatternSize <= 0 || Size < 0 || Size % PatternSize)
      return Plugin::error(ErrorCode::INVALID_SIZE,
                           "DirectX fill requires a positive pattern size "
                           "that divides the representable fill size");
    auto Queue = getQueue(AsyncInfo);
    if (!Queue)
      return Queue.takeError();
    auto Destination = getBuffer(Ptr, Size, (*Queue)->Context);
    if (!Destination)
      return Destination.takeError();
    if (!Size)
      return Error::success();
    auto Staging = createBuffer(Device.Get(), Size, D3D12_HEAP_TYPE_UPLOAD);
    if (!Staging)
      return Staging.takeError();
    void *Mapped = nullptr;
    D3D12_RANGE None{0, 0};
    if (auto Err = check(Staging->Resource->Map(0, &None, &Mapped),
                         "ID3D12Resource::Map"))
      return Err;
    for (int64_t Offset = 0; Offset < Size; Offset += PatternSize)
      std::memcpy(static_cast<char *>(Mapped) + Offset, Pattern, PatternSize);
    D3D12_RANGE Written{0, static_cast<size_t>(Size)};
    Staging->Resource->Unmap(0, &Written);
    return (*Queue)->copy(*Destination, *Staging, Size,
                          AsyncInfo.isSynchronous());
  }
  Error dataPrefetchImpl(size_t, const void **, const size_t *, bool,
                         AsyncInfoWrapperTy &) override {
    return unsupported("memory prefetch");
  }
  Error enqueueHostCallImpl(void (*Callback)(void *), void *Data,
                            AsyncInfoWrapperTy &AsyncInfo) override {
    auto Queue = getQueue(AsyncInfo);
    if (!Queue)
      return Queue.takeError();
    return (*Queue)->enqueueHostCall(
        [Callback, Data] {
          Callback(Data);
          return Error::success();
        },
        AsyncInfo.isSynchronous());
  }
  Error synchronizeImpl(__tgt_async_info &AsyncInfo,
                        bool ReleaseQueue) override {
    auto *Handle = static_cast<DirectXQueueHandleTy *>(AsyncInfo.Queue);
    if (!Handle)
      return Error::success();
    if (auto Err = (*Handle)->synchronize())
      return Err;
    if (ReleaseQueue) {
      delete Handle;
      AsyncInfo.Queue = nullptr;
    }
    return Error::success();
  }
  Error queryAsyncImpl(__tgt_async_info &AsyncInfo, bool ReleaseQueue,
                       bool *IsQueueWorkCompleted) override {
    auto *Handle = static_cast<DirectXQueueHandleTy *>(AsyncInfo.Queue);
    bool Completed = true;
    if (Handle) {
      auto Pending = (*Handle)->hasPendingWork();
      if (!Pending)
        return Pending.takeError();
      Completed = !*Pending;
    }
    if (IsQueueWorkCompleted)
      *IsQueueWorkCompleted = Completed;
    if (Completed && ReleaseQueue) {
      delete Handle;
      AsyncInfo.Queue = nullptr;
    }
    return Error::success();
  }
  Expected<bool> hasPendingWorkImpl(AsyncInfoWrapperTy &AsyncInfo) override {
    auto *Handle = AsyncInfo.getQueueAs<DirectXQueueHandleTy *>();
    if (!Handle)
      return false;
    return (*Handle)->hasPendingWork();
  }
  Expected<void *> getNativeQueueHandle(__tgt_async_info *AsyncInfo) override {
    if (!AsyncInfo || !AsyncInfo->Queue)
      return Plugin::error(ErrorCode::INVALID_ARGUMENT,
                           "no DirectX queue associated with the async info");
    auto *Handle = static_cast<DirectXQueueHandleTy *>(AsyncInfo->Queue);
    return static_cast<void *>((*Handle)->Queue.Get());
  }
  Error createEventImpl(void **Storage, bool EnableProfiling) override {
    *Storage = new DirectXEventTy;
    return Error::success();
  }
  Error destroyEventImpl(void *Event, bool) override {
    delete static_cast<DirectXEventTy *>(Event);
    return Error::success();
  }
  Error recordEventImpl(void *Event, AsyncInfoWrapperTy &AsyncInfo,
                        bool EnableProfiling) override {
    if (!AsyncInfo.hasQueue())
      return unsupported("event recording without an explicit queue");
    auto Queue = getQueue(AsyncInfo);
    if (!Queue)
      return Queue.takeError();
    std::shared_ptr<DirectXEventTimingTy> Timing;
    auto Record = [&]() -> Expected<uint64_t> {
      if (!EnableProfiling)
        return (*Queue)->signal();
      Timing = std::make_shared<DirectXEventTimingTy>();
      if (auto Err =
              check((*Queue)->Queue->GetTimestampFrequency(&Timing->Frequency),
                    "ID3D12CommandQueue::GetTimestampFrequency"))
        return std::move(Err);
      if (!Timing->Frequency)
        return Plugin::error(ErrorCode::BACKEND_FAILURE,
                             "DirectX queue returned zero timestamp frequency");
      D3D12_QUERY_HEAP_DESC Desc{};
      Desc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
      Desc.Count = 1;
      Desc.NodeMask = 1;
      if (auto Err =
              check(Device->CreateQueryHeap(&Desc, IID_PPV_ARGS(&Timing->Heap)),
                    "ID3D12Device::CreateQueryHeap"))
        return std::move(Err);
      auto Readback = createBuffer(Device.Get(), sizeof(uint64_t),
                                   D3D12_HEAP_TYPE_READBACK);
      if (!Readback)
        return Readback.takeError();
      Timing->Readback = Readback->Resource;
      DirectXQueueTy::SubmissionTy Submission{};
      if (auto Err = (*Queue)->createCommandList(Submission))
        return std::move(Err);
      Submission.List->EndQuery(Timing->Heap.Get(), D3D12_QUERY_TYPE_TIMESTAMP,
                                0);
      Submission.List->ResolveQueryData(Timing->Heap.Get(),
                                        D3D12_QUERY_TYPE_TIMESTAMP, 0, 1,
                                        Timing->Readback.Get(), 0);
      Submission.Complete = [Timing, NativeQueue = (*Queue)->Queue]() -> Error {
        void *Mapped = nullptr;
        D3D12_RANGE Range{0, sizeof(uint64_t)};
        if (auto Err = check(Timing->Readback->Map(0, &Range, &Mapped),
                             "ID3D12Resource::Map(timestamp)"))
          return Err;
        std::memcpy(&Timing->Timestamp, Mapped, sizeof(uint64_t));
        D3D12_RANGE None{0, 0};
        Timing->Readback->Unmap(0, &None);
#ifdef _WIN32
        if (auto Err =
                check(NativeQueue->GetClockCalibration(&Timing->GPUCalibration,
                                                       &Timing->CPUCalibration),
                      "ID3D12CommandQueue::GetClockCalibration"))
          return Err;
        LARGE_INTEGER Frequency;
        if (!QueryPerformanceFrequency(&Frequency) || Frequency.QuadPart <= 0)
          return Plugin::error(ErrorCode::BACKEND_FAILURE,
                               "QueryPerformanceFrequency failed");
        Timing->CPUFrequency = Frequency.QuadPart;
#endif
        return Error::success();
      };
      return (*Queue)->submitWithValue(std::move(Submission));
    };
    auto Value = Record();
    if (!Value)
      return Value.takeError();
    auto &DXEvent = *static_cast<DirectXEventTy *>(Event);
    DXEvent.Queue = *AsyncInfo.getQueueAs<DirectXQueueHandleTy *>();
    DXEvent.Value = *Value;
    DXEvent.Timing = std::move(Timing);
    return Error::success();
  }
  Error waitEventImpl(void *Event, AsyncInfoWrapperTy &AsyncInfo) override {
    auto &DXEvent = *static_cast<DirectXEventTy *>(Event);
    if (!DXEvent.Queue)
      return Plugin::error(ErrorCode::INVALID_ARGUMENT,
                           "DirectX event has not been recorded");
    auto Queue = getQueue(AsyncInfo);
    if (!Queue)
      return Queue.takeError();
    if ((*Queue)->Device.Get() != DXEvent.Queue->Device.Get())
      return Plugin::error(ErrorCode::INVALID_DEVICE,
                           "DirectX event belongs to another device");
    if ((*Queue)->Context != DXEvent.Queue->Context)
      return Plugin::error(ErrorCode::INVALID_ARGUMENT,
                           "DirectX event and queue contexts differ");
    if (auto Err = (*Queue)->enqueueWait(DXEvent.Queue, DXEvent.Value))
      return Err;
    return AsyncInfo.isSynchronous() ? (*Queue)->synchronize()
                                     : Error::success();
  }
  Expected<bool> isEventCompleteImpl(void *Event,
                                     AsyncInfoWrapperTy &) override {
    auto &DXEvent = *static_cast<DirectXEventTy *>(Event);
    if (!DXEvent.Queue)
      return Plugin::error(ErrorCode::INVALID_ARGUMENT,
                           "DirectX event has not been recorded");
    return DXEvent.Queue->isComplete(DXEvent.Value);
  }
  Error syncEventImpl(void *Event) override {
    auto &DXEvent = *static_cast<DirectXEventTy *>(Event);
    if (!DXEvent.Queue)
      return Plugin::error(ErrorCode::INVALID_ARGUMENT,
                           "DirectX event has not been recorded");
    return DXEvent.Queue->wait(DXEvent.Value);
  }
  Expected<float> getEventElapsedTimeImpl(void *Start, void *End) override {
    auto &First = *static_cast<DirectXEventTy *>(Start);
    auto &Last = *static_cast<DirectXEventTy *>(End);
    for (auto *Event : {&First, &Last}) {
      if (!Event->Queue || !Event->Timing)
        return Plugin::error(ErrorCode::INVALID_ARGUMENT,
                             "DirectX event has no recorded profiling data");
      auto Complete = Event->Queue->isComplete(Event->Value);
      if (!Complete)
        return Complete.takeError();
      if (!*Complete)
        return Plugin::error(ErrorCode::INVALID_ARGUMENT,
                             "DirectX profiling event is not complete");
    }
    const auto &A = *First.Timing;
    const auto &B = *Last.Timing;
    double Seconds;
    if (First.Queue == Last.Queue) {
      if (Last.Value < First.Value || B.Timestamp < A.Timestamp)
        return Plugin::error(ErrorCode::INVALID_ARGUMENT,
                             "DirectX profiling events are reversed");
      Seconds = double(B.Timestamp - A.Timestamp) / A.Frequency;
    } else {
#ifdef _WIN32
      // Queue clocks need not share an epoch. Correlate each to Windows QPC.
      auto Difference = [](uint64_t X, uint64_t Y) {
        return X >= Y ? double(X - Y) : -double(Y - X);
      };
      Seconds =
          Difference(B.CPUCalibration, A.CPUCalibration) / A.CPUFrequency +
          Difference(B.Timestamp, B.GPUCalibration) / B.Frequency -
          Difference(A.Timestamp, A.GPUCalibration) / A.Frequency;
      if (Seconds < 0)
        return Plugin::error(ErrorCode::INVALID_ARGUMENT,
                             "DirectX profiling events are reversed or "
                             "their clock calibrations overlap");
#else
      return unsupported("cross-queue timing without a Windows QPC clock");
#endif
    }
    return static_cast<float>(Seconds * 1000.0);
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
    auto Queue = std::make_shared<DirectXQueueTy>(Device.Get(), this);
    if (auto Err = Queue->init())
      return Err;
    __tgt_async_info *Info = AsyncInfo;
    Info->Queue = new DirectXQueueHandleTy(std::move(Queue));
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

Error DirectXKernelTy::launchImpl(GenericDeviceTy &Device,
                                  uint32_t NumThreads[3], uint32_t NumBlocks[3],
                                  uint32_t DynBlockMemSize,
                                  KernelLaunchArgsTy &Args,
                                  AsyncInfoWrapperTy &AsyncInfo,
                                  GenericProfilerTy *) const {
  if (&Device != &Program->getDevice())
    return Plugin::error(ErrorCode::INVALID_DEVICE,
                         "DirectX kernel belongs to another device");
  if (Args.Flags.Cooperative)
    return unsupported("cooperative kernel dispatch");
  if (DynBlockMemSize)
    return unsupported("dynamic shared memory");
  for (unsigned I = 0; I < 3; ++I) {
    if (NumThreads[I] != Program->Manifest.NumThreads[I])
      return Plugin::error(ErrorCode::INVALID_ARGUMENT,
                           "DirectX group size must match compiled numthreads");
    if (NumBlocks[I] > D3D12_CS_DISPATCH_MAX_THREAD_GROUPS_PER_DIMENSION)
      return Plugin::error(ErrorCode::INVALID_ARGUMENT,
                           "DirectX dispatch group count exceeds 65535");
  }
  if (Args.NumArgs != Program->Manifest.getNumLaunchArguments() ||
      (Args.NumArgs && (!Args.Args || !Args.ArgSizes)))
    return Plugin::error(ErrorCode::INVALID_ARGUMENT,
                         "DirectX launch argument count or arrays mismatch");
  auto &DXDevice = static_cast<DirectXDeviceTy &>(Device);
  auto Queue = DXDevice.getQueue(AsyncInfo);
  if (!Queue)
    return Queue.takeError();
  if ((*Queue)->Context && (*Queue)->Context != Program->Context)
    return Plugin::error(ErrorCode::INVALID_ARGUMENT,
                         "DirectX queue and program contexts differ");
  auto &Context = *static_cast<DirectXPluginContextTy *>(Program->Context);
  auto Bindings = std::make_shared<directx::DirectXArgumentBindingsTy>();
  if (auto Err = Bindings->prepare(
          Program->Manifest, ArrayRef(Args.Args, Args.NumArgs),
          ArrayRef(Args.ArgSizes, Args.NumArgs),
          [&](const void *Pointer)
              -> Expected<directx::DirectXResolvedBufferTy> {
            auto Allocation = Context.getAllocInfo(Pointer);
            if (!Allocation)
              return Allocation.takeError();
            size_t Remaining = Allocation->Size -
                               (reinterpret_cast<uintptr_t>(Pointer) -
                                reinterpret_cast<uintptr_t>(Allocation->Base));
            auto Buffer = Context.getBuffer(Pointer, Remaining);
            if (!Buffer)
              return Buffer.takeError();
            return directx::DirectXResolvedBufferTy{std::move(*Buffer),
                                                    Remaining};
          }))
    return Err;

  std::map<ID3D12Resource *, D3D12_RESOURCE_STATES> States;
  for (const auto &Buffer : Bindings->Buffers) {
    auto State =
        std::holds_alternative<D3D12_SHADER_RESOURCE_VIEW_DESC>(Buffer.View)
            ? D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE
            : D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    auto [It, Inserted] = States.emplace(Buffer.Resource.Get(), State);
    if (!Inserted && It->second != State)
      return unsupported("simultaneous SRV/UAV views of one buffer resource");
  }
  if (!NumBlocks[0] || !NumBlocks[1] || !NumBlocks[2])
    return AsyncInfo.isSynchronous() ? (*Queue)->synchronize()
                                     : Error::success();
  if (auto Err = Bindings->createDescriptorHeap(*Program->NativeDevice.Get()))
    return Err;
  DirectXQueueTy::SubmissionTy Submission{};
  Submission.Pipeline = Pipeline;
  Submission.RootSignature = Program->RootSignature;
  Submission.Arguments = std::move(Bindings);
  if (auto Err = (*Queue)->createCommandList(Submission))
    return Err;
  SmallVector<D3D12_RESOURCE_BARRIER> Barriers;
  for (auto [Resource, State] : States) {
    D3D12_RESOURCE_BARRIER Barrier{};
    Barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    Barrier.Transition.pResource = Resource;
    Barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    Barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
    Barrier.Transition.StateAfter = State;
    Barriers.push_back(Barrier);
  }
  if (!Barriers.empty())
    Submission.List->ResourceBarrier(static_cast<UINT>(Barriers.size()),
                                     Barriers.data());
  if (auto Err = Submission.Arguments->record(*Submission.List.Get(),
                                              *Submission.RootSignature.Get()))
    return Err;
  Submission.List->Dispatch(NumBlocks[0], NumBlocks[1], NumBlocks[2]);
  for (auto &Barrier : Barriers)
    std::swap(Barrier.Transition.StateBefore, Barrier.Transition.StateAfter);
  if (!Barriers.empty())
    Submission.List->ResourceBarrier(static_cast<UINT>(Barriers.size()),
                                     Barriers.data());
  return (*Queue)->submit(std::move(Submission), AsyncInfo.isSynchronous());
}

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
  bool Blocking = AsyncInfo.isSynchronous();
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
      if (auto Err = (*Queue)->copy(*Temporary, Source, Size, Blocking))
        return Err;
      Source = std::move(*Temporary);
    }
    return (*Queue)->copy(Destination, Source, Size, Blocking);
  }

  bool Upload = Kind == CopyKind::Upload;
  auto Staging =
      createBuffer(Device.Get(), Size,
                   Upload ? D3D12_HEAP_TYPE_UPLOAD : D3D12_HEAP_TYPE_READBACK);
  if (!Staging)
    return Staging.takeError();
  if (!Upload)
    return (*Queue)->copy(*Staging, Source, Size, Blocking,
                          [Resource = Staging->Resource, Dst, Size]() -> Error {
                            void *Mapped = nullptr;
                            D3D12_RANGE ReadRange{0, static_cast<size_t>(Size)};
                            if (auto Err =
                                    check(Resource->Map(0, &ReadRange, &Mapped),
                                          "ID3D12Resource::Map"))
                              return Err;
                            std::memcpy(Dst, Mapped, Size);
                            D3D12_RANGE None{0, 0};
                            Resource->Unmap(0, &None);
                            return Error::success();
                          });

  // Pageable upload staging must observe earlier readbacks and event waits.
  if (auto Err = (*Queue)->waitForHostWork())
    return Err;
  void *Mapped = nullptr;
  D3D12_RANGE ReadRange{0, 0};
  if (auto Err = check(Staging->Resource->Map(0, &ReadRange, &Mapped),
                       "ID3D12Resource::Map"))
    return Err;
  std::memcpy(Mapped, Src, Size);
  D3D12_RANGE WrittenRange{0, static_cast<size_t>(Size)};
  Staging->Resource->Unmap(0, &WrittenRange);
  return (*Queue)->copy(Destination, *Staging, Size, Blocking);
}

struct DirectXGlobalHandlerTy final : GenericGlobalHandlerTy {
  Error getGlobalMetadataFromDevice(GenericDeviceTy &, DeviceImageTy &,
                                    GlobalTy &) override {
    return unsupported("device globals");
  }
  Error iterateSymbols(DeviceImageTy &Image, SymbolKindTy Kind,
                       function_ref<bool(StringRef)> Callback) override {
    if (Kind != SymbolKindTy::Kernel)
      return unsupported("global symbol enumeration");
    Callback(static_cast<DirectXImageTy &>(Image).Manifest.Entry);
    return Error::success();
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
    auto SerializeRootSignature =
        reinterpret_cast<PFN_D3D12_SERIALIZE_ROOT_SIGNATURE>(
            D3D12->getAddressOfSymbol("D3D12SerializeRootSignature"));
    if (!CreateFactory)
      return Plugin::error(
          ErrorCode::BACKEND_FAILURE,
          "DirectX DXCore runtime is missing DXCoreCreateAdapterFactory");
    if (!CreateDevice)
      return Plugin::error(
          ErrorCode::BACKEND_FAILURE,
          "DirectX D3D12 runtime is missing D3D12CreateDevice");
    if (!SerializeRootSignature)
      return Plugin::error(
          ErrorCode::BACKEND_FAILURE,
          "DirectX D3D12 runtime is missing D3D12SerializeRootSignature");

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
      Entry.SerializeRootSignature = SerializeRootSignature;
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
