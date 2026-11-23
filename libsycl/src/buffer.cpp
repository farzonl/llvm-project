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

BufferImpl::BufferImpl(void *Host, std::size_t Bytes)
    : MHost(Host), MStorage(Bytes) {
  std::memcpy(MStorage.data(), Host, Bytes);
}

void BufferImpl::synchronize() {
  if (MQueue)
    MQueue->waitAndThrow();
  if (MDirty) {
    MQueue->memcpyImpl(MStorage.data(), MDevicePointer, MStorage.size(), {})
        ->waitAndThrow();
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
  std::lock_guard<std::recursive_mutex> Lock(getBufferSubmissionMutex());
  try {
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
  if (MQueue && (&MQueue->getContext() != &Queue.getContext() ||
                 &MQueue->getDevice() != &Queue.getDevice())) {
    synchronize();
    freeDevice();
    MLastEvent.reset();
    MQueue.reset();
    MNeedsUpload = true;
  }
  if (!MDevicePointer) {
    std::size_t DeviceBytes = MStorage.size();
    if (Queue.getBackend() == backend::ext_llvm_directx) {
      // Raw views address whole words, including the final partial word.
      std::size_t Padding = (4 - DeviceBytes % 4) % 4;
      if (DeviceBytes > std::numeric_limits<std::size_t>::max() - Padding)
        throw exception(make_error_code(errc::memory_allocation),
                        "DirectX buffer allocation size overflow");
      DeviceBytes += Padding;
    }
    MQueue = Queue.shared_from_this();
    callAndThrow(Queue.getContext(), olMemAlloc,
                 Queue.getContext().getOLHandleRef(),
                 Queue.getDevice().getOLHandle(), OL_ALLOC_TYPE_DEVICE,
                 DeviceBytes, &MDevicePointer);
  }
  if (MNeedsUpload) {
    MLastEvent =
        Queue.memcpyImpl(MDevicePointer, MStorage.data(), MStorage.size(), {});
    MNeedsUpload = false;
  }
  return MDevicePointer;
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
  releaseBuffer(this);
}

BufferImpl *makeBuffer(void *Host, std::size_t Bytes) {
  // The scheduler must outlive buffers with static storage duration.
  getBufferSubmissionMutex();
  return new BufferImpl(Host, Bytes);
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
  std::lock_guard<std::recursive_mutex> Lock(getBufferSubmissionMutex());
  ++Buffer->MBufferReferences;
  retainBuffer(Buffer);
}

void releaseBufferOwner(BufferImpl *Buffer) noexcept {
  if (!Buffer)
    return;
  std::lock_guard<std::recursive_mutex> Lock(getBufferSubmissionMutex());
  if (--Buffer->MBufferReferences == 0) {
    try {
      if (Buffer->MPendingRequirements)
        throw exception(make_error_code(errc::feature_not_supported),
                        "Destroying the last buffer inside a command group "
                        "before submission is not supported");
      Buffer->synchronize();
      std::memcpy(Buffer->MHost, Buffer->MStorage.data(),
                  Buffer->MStorage.size());
    } catch (const sycl::exception &) {
      recordAsyncException(Buffer->MQueue, std::current_exception());
    }
    Buffer->MHost = nullptr;
    if (Buffer->MQueue)
      Buffer->MQueue->throwAsynchronous();
  }
  releaseBuffer(Buffer);
}
} // namespace detail
_LIBSYCL_END_NAMESPACE_SYCL
