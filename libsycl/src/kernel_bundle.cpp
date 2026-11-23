//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include <detail/context_impl.hpp>
#include <detail/device_impl.hpp>
#include <detail/program_manager.hpp>
#include <sycl/__impl/kernel_bundle.hpp>

_LIBSYCL_BEGIN_NAMESPACE_SYCL

namespace detail {
class KernelImpl {
public:
  context Context;
  kernel_id ID;
  KernelImpl(context Context, kernel_id ID)
      : Context(std::move(Context)), ID(std::move(ID)) {}
};

kernel_id makeKernelID(std::string_view Name) {
  ProgramAndKernelManager::getInstance().getDeviceKernelInfo(Name);
  return createSyclObjFromImpl<kernel_id>(
      std::make_shared<const std::string>(Name));
}

bool kernelIsCompatible(const kernel_id &ID, const device &Device) {
  return ProgramAndKernelManager::getInstance().isKernelCompatible(
      ID.get_name(), *getSyclObjImpl(Device));
}

std::shared_ptr<KernelBundleImpl>
makeKernelBundle(bundle_state State, const context &Context,
                 const std::vector<device> &Devices,
                 const std::vector<kernel_id> &Kernels, bool SelectAll) {
  if (State != bundle_state::executable)
    throw exception(make_error_code(errc::feature_not_supported),
                    "Only executable kernel bundles are supported");
  if (Devices.empty())
    throw exception(make_error_code(errc::invalid),
                    "A kernel bundle requires at least one device");
  auto ContextDevices = Context.get_devices();
  for (const device &Device : Devices)
    if (std::find(ContextDevices.begin(), ContextDevices.end(), Device) ==
        ContextDevices.end())
      throw exception(make_error_code(errc::invalid),
                      "Kernel bundle device does not belong to its context");
  auto Result = std::make_shared<KernelBundleImpl>(
      KernelBundleImpl{Context, Devices, {}});
  auto Candidates = SelectAll ? sycl::get_kernel_ids() : Kernels;
  auto &Manager = ProgramAndKernelManager::getInstance();
  for (const kernel_id &ID : Candidates) {
    bool Compatible = false;
    for (const device &Device : Devices) {
      if (!kernelIsCompatible(ID, Device))
        continue;
      Compatible = true;
      auto &Info = Manager.getDeviceKernelInfo(ID.get_name());
      Manager.getOrCreateKernel(Info, getSyclObjImpl(Context),
                                *getSyclObjImpl(Device));
    }
    if (!Compatible) {
      if (!SelectAll)
        throw exception(make_error_code(errc::kernel_not_supported),
                        "Requested kernel has no executable image for the "
                        "bundle devices");
      continue;
    }
    if (std::find(Result->Kernels.begin(), Result->Kernels.end(), ID) ==
        Result->Kernels.end())
      Result->Kernels.push_back(ID);
  }
  return Result;
}

kernel makeKernel(const KernelBundleImpl &Bundle, const kernel_id &ID) {
  if (std::find(Bundle.Kernels.begin(), Bundle.Kernels.end(), ID) ==
      Bundle.Kernels.end())
    throw exception(make_error_code(errc::invalid),
                    "Kernel does not belong to this bundle");
  return createSyclObjFromImpl<kernel>(
      std::make_shared<KernelImpl>(Bundle.Context, ID));
}
} // namespace detail

std::vector<kernel_id> get_kernel_ids() {
  std::vector<kernel_id> Result;
  for (const std::string &Name :
       detail::ProgramAndKernelManager::getInstance().getKernelNames())
    Result.push_back(detail::makeKernelID(Name));
  return Result;
}

context kernel::get_context() const { return impl->Context; }
backend kernel::get_backend() const noexcept {
  return impl->Context.get_backend();
}
std::string kernel::get_function_name() const { return impl->ID.get_name(); }

_LIBSYCL_END_NAMESPACE_SYCL
