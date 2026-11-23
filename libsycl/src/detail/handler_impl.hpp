//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file contains the declaration of the HandlerImpl, which stores
/// command group function, argument data, and kernel execution range in
/// liboffload style for deferred submission.
///
//===----------------------------------------------------------------------===//

#ifndef _LIBSYCL_SRC_DETAIL_HANDLER_IMPL_HPP
#define _LIBSYCL_SRC_DETAIL_HANDLER_IMPL_HPP

#include <detail/buffer_impl.hpp>
#include <sycl/__impl/detail/config.hpp>

#include <OffloadAPI.h>

#include <functional>
#include <memory>
#include <vector>

_LIBSYCL_BEGIN_NAMESPACE_SYCL

namespace detail {

class EventImpl;
class QueueImpl;

/// Stores the deferred command group state for a sycl::handler submission.
struct HandlerImpl {
  explicit HandlerImpl(QueueImpl &Queue) : MQueue(Queue) {}

  HandlerImpl(const HandlerImpl &) = delete;
  HandlerImpl(HandlerImpl &&) = delete;
  HandlerImpl &operator=(const HandlerImpl &) = delete;
  HandlerImpl &operator=(HandlerImpl &&) = delete;

  ~HandlerImpl() {
    for (const auto &Requirement : MBufferRequirements)
      Requirement.Buffer->releaseRequirement();
  }

  // Queue this handler is attached to.
  QueueImpl &MQueue;

  /// The command group function to execute at finalize time.
  std::function<std::shared_ptr<EventImpl>()> MCGF;

  /// Captured kernel argument data.
  std::vector<char> MArgData;

  /// Pattern used for fill and memset operations.
  std::vector<unsigned char> MFillPattern;

  /// Kernel execution range in liboffload format, set by setKernelRange().
  ol_kernel_launch_size_args_t MRange = {};

  struct BufferRequirement {
    BufferImpl *Buffer;
    bool Writes;
  };
  std::vector<BufferRequirement> MBufferRequirements;
};

} // namespace detail

_LIBSYCL_END_NAMESPACE_SYCL

#endif // _LIBSYCL_SRC_DETAIL_HANDLER_IMPL_HPP
