//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBSYCL_SRC_DETAIL_BUFFER_IMPL_HPP
#define _LIBSYCL_SRC_DETAIL_BUFFER_IMPL_HPP

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <sycl/__impl/buffer.hpp>
#include <vector>

_LIBSYCL_BEGIN_NAMESPACE_SYCL
namespace detail {
class QueueImpl;
class EventImpl;

// Serialize requirement acquisition and submission, not GPU completion.
std::recursive_mutex &getBufferSubmissionMutex();

class BufferImpl {
  std::atomic<std::size_t> MReferences{1};
  std::atomic<std::size_t> MBufferReferences{1};
  std::atomic<std::size_t> MPendingRequirements{0};
  void *MHost;
  std::vector<unsigned char> MStorage;
  std::size_t MBytes;
  std::size_t MAlignment;
  unsigned char *MData;
  void *MDevicePointer = nullptr;
  std::shared_ptr<QueueImpl> MQueue;
  std::shared_ptr<EventImpl> MLastEvent;
  bool MDirty = false;
  bool MNeedsUpload = true;
  std::mutex MHostAccessMutex;
  std::condition_variable MHostAccessCondition;
  std::size_t MHostReaders = 0;
  bool MHostWriter = false;
  bool MHostStorageFinalized = false;

  void synchronize();
  void waitForHostAccess();
  void finalizeHostStorage() noexcept;
  void freeDevice();
  friend void retainBuffer(BufferImpl *) noexcept;
  friend void releaseBuffer(BufferImpl *) noexcept;
  friend void retainBufferOwner(BufferImpl *) noexcept;
  friend void releaseBufferOwner(BufferImpl *) noexcept;

public:
  BufferImpl(void *Host, std::size_t Bytes, std::size_t Alignment);
  ~BufferImpl();
  void *acquire(QueueImpl &Queue);
  std::shared_ptr<EventImpl> getLastEvent() const { return MLastEvent; }
  void submitted(std::shared_ptr<EventImpl> Event, bool Writes);
  void retainRequirement();
  void releaseRequirement();
  void *acquireHost(access_mode Mode);
  void releaseHost(access_mode Mode) noexcept;
};
} // namespace detail
_LIBSYCL_END_NAMESPACE_SYCL

#endif
