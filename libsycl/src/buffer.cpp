//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include <cstring>
#include <detail/buffer_impl.hpp>
#include <detail/context_impl.hpp>
#include <detail/device_impl.hpp>
#include <detail/event_impl.hpp>
#include <detail/global_objects.hpp>
#include <detail/queue_impl.hpp>
#include <limits>

_LIBSYCL_BEGIN_NAMESPACE_SYCL
namespace detail {

std::recursive_mutex &getBufferSubmissionMutex() {
  static std::recursive_mutex Mutex;
  return Mutex;
}

BufferImpl::BufferImpl(void *Host, std::size_t Bytes, std::size_t Alignment)
    : MHost(Host), MBytes(Bytes), MAlignment(Alignment) {
  if (!Bytes || !Alignment || (Alignment & (Alignment - 1)))
    throw exception(make_error_code(errc::invalid),
                    "Buffer storage requires a nonzero size and "
                    "power-of-two alignment");
  if (Bytes > std::numeric_limits<std::size_t>::max() - (Alignment - 1))
    throw exception(make_error_code(errc::memory_allocation),
                    "Aligned buffer storage size overflow");
  MStorage.resize(Bytes + Alignment - 1);
  void *Data = MStorage.data();
  std::size_t Space = MStorage.size();
  MData =
      static_cast<unsigned char *>(std::align(Alignment, Bytes, Data, Space));
  if (!MData)
    throw exception(make_error_code(errc::memory_allocation),
                    "Buffer storage alignment failed");
  if (Host)
    std::memcpy(MData, Host, Bytes);
}

void BufferImpl::waitForHostAccess() {
  std::unique_lock<std::mutex> Lock(MHostAccessMutex);
  MHostAccessCondition.wait(Lock,
                            [&] { return !MHostWriter && MHostReaders == 0; });
}

void BufferImpl::synchronize() {
  if (MLastEvent)
    MLastEvent->waitAndThrow();
  if (MQueue)
    MQueue->waitAndThrow();
  if (MDirty) {
    MQueue->memcpyImpl(MData, MDevicePointer, MBytes, {})->waitAndThrow();
    MDirty = false;
  }
}

void BufferImpl::freeDevice() {
  if (MDevicePointer) {
    callAndThrow(MQueue->getContext(), olMemFree,
                 MQueue->getContext().getOLHandleRef(), MDevicePointer);
    MDevicePointer = nullptr;
  }
}

BufferImpl::~BufferImpl() {
  // Only the final reference destroys storage; no submission can still use it.
  try {
    waitForHostAccess();
    synchronize();
  } catch (const sycl::exception &) {
    recordAsyncException(MQueue, std::current_exception());
  }
  try {
    freeDevice();
  } catch (const sycl::exception &) {
    recordAsyncException(MQueue, std::current_exception());
  }
  if (MQueue)
    MQueue->throwAsynchronous();
}

void *BufferImpl::acquire(QueueImpl &Queue) {
  waitForHostAccess();
  if (MQueue && (&MQueue->getContext() != &Queue.getContext() ||
                 &MQueue->getDevice() != &Queue.getDevice())) {
    synchronize();
    freeDevice();
    MLastEvent.reset();
    MQueue.reset();
    MNeedsUpload = true;
  }
  if (!MDevicePointer) {
    std::size_t DeviceBytes = MBytes;
    if (Queue.getBackend() == backend::ext_llvm_directx) {
      // Raw views address whole words, including the final partial word.
      std::size_t Padding = (4 - DeviceBytes % 4) % 4;
      if (DeviceBytes > std::numeric_limits<std::size_t>::max() - Padding)
        throw exception(make_error_code(errc::memory_allocation),
                        "DirectX buffer allocation size overflow");
      DeviceBytes += Padding;
    }
    MQueue = Queue.shared_from_this();
    if (MAlignment > alignof(std::max_align_t))
      callAndThrow(Queue.getContext(), olMemAllocAligned,
                   Queue.getContext().getOLHandleRef(),
                   Queue.getDevice().getOLHandle(), OL_ALLOC_TYPE_DEVICE,
                   DeviceBytes, MAlignment, &MDevicePointer);
    else
      callAndThrow(Queue.getContext(), olMemAlloc,
                   Queue.getContext().getOLHandleRef(),
                   Queue.getDevice().getOLHandle(), OL_ALLOC_TYPE_DEVICE,
                   DeviceBytes, &MDevicePointer);
  }
  if (MNeedsUpload) {
    MLastEvent = Queue.memcpyImpl(MDevicePointer, MData, MBytes, {});
    MNeedsUpload = false;
  }
  return MDevicePointer;
}

void *BufferImpl::acquireHost(access_mode Mode) {
  if (Mode == access_mode::read) {
    std::lock_guard<std::mutex> Lock(MHostAccessMutex);
    if (MHostReaders) {
      ++MHostReaders;
      return MData;
    }
  }
  std::lock_guard<std::recursive_mutex> SubmissionLock(
      getBufferSubmissionMutex());
  if (MPendingRequirements)
    throw exception(make_error_code(errc::invalid),
                    "A host accessor cannot be acquired while its buffer "
                    "has an unsubmitted command-group requirement");
  std::unique_lock<std::mutex> Lock(MHostAccessMutex);
  bool Writes = Mode != access_mode::read;
  MHostAccessCondition.wait(
      Lock, [&] { return !MHostWriter && (!Writes || MHostReaders == 0); });
  synchronize();
  if (Writes) {
    MHostWriter = true;
    MNeedsUpload = true;
  } else
    ++MHostReaders;
  return MData;
}

void BufferImpl::releaseHost(access_mode Mode) noexcept {
  {
    std::lock_guard<std::mutex> Lock(MHostAccessMutex);
    if (Mode == access_mode::read)
      --MHostReaders;
    else
      MHostWriter = false;
  }
  MHostAccessCondition.notify_all();
  if (MBufferReferences == 0)
    finalizeHostStorage();
}

void BufferImpl::finalizeHostStorage() noexcept {
  {
    std::lock_guard<std::mutex> Lock(MHostAccessMutex);
    if (MBufferReferences || MPendingRequirements || MHostWriter ||
        MHostReaders || MHostStorageFinalized)
      return;
    MHostStorageFinalized = true;
  }
  try {
    synchronize();
    if (MHost)
      std::memcpy(MHost, MData, MBytes);
  } catch (const sycl::exception &) {
    recordAsyncException(MQueue, std::current_exception());
  }
  MHost = nullptr;
  if (MQueue)
    MQueue->throwAsynchronous();
}

class HostAccessImpl {
  BufferImpl *MBuffer;
  access_mode MMode;

public:
  void *Pointer;
  HostAccessImpl(BufferImpl *Buffer, access_mode Mode)
      : MBuffer(Buffer), MMode(Mode), Pointer(Buffer->acquireHost(Mode)) {
    retainBuffer(MBuffer);
  }
  ~HostAccessImpl() {
    MBuffer->releaseHost(MMode);
    releaseBuffer(MBuffer);
  }
};

std::shared_ptr<HostAccessImpl> acquireHostAccess(BufferImpl *Buffer,
                                                  access_mode Mode) {
  if (!Buffer)
    throw exception(make_error_code(errc::invalid),
                    "A host accessor requires a valid buffer");
  return std::make_shared<HostAccessImpl>(Buffer, Mode);
}

void *getHostAccessPointer(const HostAccessImpl &Access) {
  return Access.Pointer;
}

void BufferImpl::submitted(std::shared_ptr<EventImpl> Event, bool Writes) {
  MLastEvent = std::move(Event);
  MDirty |= Writes;
}

void BufferImpl::retainRequirement() {
  ++MPendingRequirements;
  retainBuffer(this);
}

void BufferImpl::releaseRequirement() {
  --MPendingRequirements;
  if (MBufferReferences == 0)
    finalizeHostStorage();
  releaseBuffer(this);
}

BufferImpl *makeBuffer(void *Host, std::size_t Bytes) {
  // Keep the factory used by existing normally aligned buffer binaries.
  return makeBuffer(Host, Bytes, alignof(std::max_align_t));
}

BufferImpl *makeBuffer(void *Host, std::size_t Bytes, std::size_t Alignment) {
  // The scheduler must outlive buffers with static storage duration.
  getBufferSubmissionMutex();
  return new BufferImpl(Host, Bytes, Alignment);
}
void retainBuffer(BufferImpl *Buffer) noexcept {
  if (Buffer)
    ++Buffer->MReferences;
}
void releaseBuffer(BufferImpl *Buffer) noexcept {
  if (Buffer && --Buffer->MReferences == 0)
    delete Buffer;
}
void retainBufferOwner(BufferImpl *Buffer) noexcept {
  if (!Buffer)
    return;
  ++Buffer->MBufferReferences;
  retainBuffer(Buffer);
}

void releaseBufferOwner(BufferImpl *Buffer) noexcept {
  if (!Buffer)
    return;
  if (--Buffer->MBufferReferences == 0)
    Buffer->finalizeHostStorage();
  releaseBuffer(Buffer);
}
} // namespace detail
_LIBSYCL_END_NAMESPACE_SYCL
