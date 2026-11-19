//===--- rtl.cpp - Target RTL for DirectX
//----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "GlobalHandler.h"
#include "PluginInterface.h"

#include "llvm/Frontend/OpenMP/OMPGridValues.h"
#include "llvm/Support/DynamicLibrary.h"
#include "llvm/Support/FormatVariadic.h"

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

#include <cstdlib>
#include <limits>

namespace llvm::omp::target::plugin {
namespace {

using error::ErrorCode;
using Microsoft::WRL::ComPtr;
using sys::DynamicLibrary;

Error unsupported(const char *Operation) {
  return Plugin::error(ErrorCode::UNSUPPORTED,
                       "DirectX prototype does not support %s", Operation);
}

Error check(HRESULT Result, const char *Operation) {
  if (SUCCEEDED(Result))
    return Error::success();
  return Plugin::error(ErrorCode::BACKEND_FAILURE, "%s failed (HRESULT 0x%08x)",
                       Operation, static_cast<uint32_t>(Result));
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

struct DirectXDeviceTy final : GenericDeviceTy {
  ComPtr<ID3D12Device> Device;
  std::string Name;

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

  Expected<DeviceImageTy *> loadBinaryImpl(std::unique_ptr<MemoryBuffer> &&,
                                           int32_t,
                                           PluginContextTy *) override {
    return unsupported("image loading");
  }
  Error unloadBinaryImpl(DeviceImageTy *) override {
    return unsupported("image unloading");
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
  Error dataSubmitImpl(void *, const void *, int64_t, AsyncInfoWrapperTy &,
                       GenericProfilerTy *) override {
    return unsupported("host-to-device copies");
  }
  Error dataRetrieveImpl(void *, const void *, int64_t, AsyncInfoWrapperTy &,
                         GenericProfilerTy *) override {
    return unsupported("device-to-host copies");
  }
  Error dataMemcpyImpl(void *, const void *, int64_t,
                       AsyncInfoWrapperTy &) override {
    return unsupported("memory copies");
  }
  Error dataExchangeImpl(const void *, GenericDeviceTy &, void *, int64_t,
                         AsyncInfoWrapperTy &, GenericProfilerTy *) override {
    return unsupported("peer copies");
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
  Error synchronizeImpl(__tgt_async_info &, bool) override {
    return unsupported("queue synchronization");
  }
  Error queryAsyncImpl(__tgt_async_info &, bool, bool *) override {
    return unsupported("queue queries");
  }
  Expected<bool> hasPendingWorkImpl(AsyncInfoWrapperTy &) override {
    return unsupported("pending work queries");
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
  ComPtr<ID3D12Device> Device;

  DirectXPluginContextTy(GenericPluginTy &Plugin,
                         ArrayRef<GenericDeviceTy *> Devices,
                         const ComPtr<ID3D12Device> &Device)
      : PluginContextTy(Plugin, Devices), Device(Device) {}

  Error deinit() override {
    Device.Reset();
    return Error::success();
  }
  Expected<void *> getNativeHandle() override {
    return static_cast<void *>(Device.Get());
  }
  Error initAsyncInfoImpl(GenericDeviceTy &, AsyncInfoWrapperTy &) override {
    return unsupported("queue creation");
  }
  Expected<PluginAllocInfoTy> getAllocInfo(const void *) override {
    return Plugin::error(
        ErrorCode::NOT_FOUND,
        "pointer is not a known DirectX allocation in this context");
  }
};

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
  Expected<bool> isELFCompatible(uint32_t, StringRef) const override {
    return false;
  }
  bool isDataExchangable(int32_t, int32_t) override { return false; }
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
