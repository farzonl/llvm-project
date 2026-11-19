//===- ArgumentsTest.cpp - DirectX argument binding tests
//------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "DirectXArguments.h"
#include "gtest/gtest.h"

#include <array>
#include <cstring>

using namespace llvm;
using namespace llvm::omp::target::plugin::directx;

namespace {

void expectError(Error Err, error::ErrorCode Code, StringRef Message) {
  ASSERT_TRUE(static_cast<bool>(Err));
  handleAllErrors(std::move(Err), [&](const error::OffloadError &Detail) {
    EXPECT_EQ(Detail.convertToErrorCode(), error::make_error_code(Code));
    EXPECT_TRUE(StringRef(Detail.getMessage()).contains(Message))
        << Detail.getMessage();
  });
}

Expected<DirectXResolvedBufferTy> noBuffers(const void *) {
  ADD_FAILURE() << "unexpected buffer resolution";
  return error::createOffloadError(error::ErrorCode::NOT_FOUND,
                                   "unexpected buffer resolution");
}

TEST(DirectXArgumentsTest, RawViewsUseOnlyRemainingWholeDwords) {
  auto View = getRawBufferView(80, 35, 32);
  ASSERT_TRUE(static_cast<bool>(View)) << toString(View.takeError());
  EXPECT_EQ(View->FirstElement, 20u);
  EXPECT_EQ(View->NumElements, 8u);
  View = getRawBufferView(0, 4, 4);
  ASSERT_TRUE(static_cast<bool>(View)) << toString(View.takeError());
  EXPECT_EQ(View->FirstElement, 0u);
  EXPECT_EQ(View->NumElements, 1u);
}

TEST(DirectXArgumentsTest, RejectsMisalignedAndUndersizedViews) {
  for (uint64_t Offset : {1, 4, 8, 12, 20}) {
    auto View = getRawBufferView(Offset, 32, 4);
    ASSERT_FALSE(static_cast<bool>(View));
    expectError(View.takeError(), error::ErrorCode::INVALID_ARGUMENT,
                "16-byte aligned");
  }
  for (uint64_t Remaining : {0, 1, 3, 7}) {
    auto View = getRawBufferView(16, Remaining, 8);
    ASSERT_FALSE(static_cast<bool>(View));
    expectError(View.takeError(), error::ErrorCode::INVALID_SIZE, "minimum");
  }
}

TEST(DirectXArgumentsTest, EnforcesViewLimitWithoutTruncating) {
  uint64_t Limit = uint64_t{1}
                   << D3D12_REQ_BUFFER_RESOURCE_TEXEL_COUNT_2_TO_EXP;
  auto View = getRawBufferView(16, Limit * 4 + 3, 4);
  ASSERT_TRUE(static_cast<bool>(View)) << toString(View.takeError());
  EXPECT_EQ(View->NumElements, Limit);
  View = getRawBufferView(16, (Limit + 1) * 4, 4);
  ASSERT_FALSE(static_cast<bool>(View));
  expectError(View.takeError(), error::ErrorCode::UNSUPPORTED, "element limit");
}

TEST(DirectXArgumentsTest, PacksScalarBitsAndZeroFillsPadding) {
  DirectXManifestTy Manifest;
  Manifest.Constants = DirectXConstantBindingTy{4, 2, 24};
  Manifest.Arguments = {{0, DirectXScalarBindingTy{4, 0}},
                        {1, DirectXScalarBindingTy{8, 8}}};
  alignas(8) std::array<uint8_t, 17> Values{0,    0x34, 0x12, 0xc0, 0x7f,
                                            0x21, 0x43, 0x65, 0x87, 0xa9,
                                            0xcb, 0xed, 0xff};
  void *Args[] = {Values.data() + 1, Values.data() + 5};
  int64_t Sizes[] = {4, 8};
  DirectXArgumentBindingsTy Bindings;
  auto Err = Bindings.prepare(Manifest, Args, Sizes, noBuffers);
  ASSERT_FALSE(static_cast<bool>(Err)) << toString(std::move(Err));
  Values.fill(0);
  EXPECT_EQ(ArrayRef(Bindings.Constants),
            (ArrayRef<uint32_t>{0x7fc01234, 0, 0x87654321, 0xffedcba9, 0, 0}));
  EXPECT_TRUE(Bindings.Buffers.empty());
  EXPECT_EQ(Bindings.Heap.Get(), nullptr);
}

TEST(DirectXArgumentsTest,
     RequiresExactArgumentShapesAndPreservesPreviousData) {
  DirectXManifestTy Manifest;
  Manifest.Constants = DirectXConstantBindingTy{0, 0, 4};
  Manifest.Arguments = {{0, DirectXScalarBindingTy{4, 0}}};
  uint8_t Value[] = {1, 2, 3, 4};
  void *Args[] = {Value};
  int64_t Sizes[] = {4};
  DirectXArgumentBindingsTy Bindings;
  auto Err = Bindings.prepare(Manifest, Args, Sizes, noBuffers);
  ASSERT_FALSE(static_cast<bool>(Err)) << toString(std::move(Err));
  expectError(Bindings.prepare(Manifest, {}, {}, noBuffers),
              error::ErrorCode::INVALID_ARGUMENT, "per manifest argument");
  expectError(Bindings.prepare(Manifest, Args, {}, noBuffers),
              error::ErrorCode::INVALID_ARGUMENT, "per manifest argument");
  for (int64_t Size : {-1, 0, 3, 8}) {
    Sizes[0] = Size;
    expectError(Bindings.prepare(Manifest, Args, Sizes, noBuffers),
                error::ErrorCode::INVALID_SIZE, "size mismatch");
  }
  Args[0] = nullptr;
  expectError(Bindings.prepare(Manifest, Args, Sizes, noBuffers),
              error::ErrorCode::INVALID_ARGUMENT, "has no value");
  ASSERT_EQ(Bindings.Constants.size(), 1u);
  EXPECT_EQ(Bindings.Constants[0], 0x04030201u);
}

TEST(DirectXArgumentsTest, DecodesPointerSlotsAndPropagatesResolverErrors) {
  DirectXManifestTy Manifest;
  Manifest.Arguments = {
      {0, DirectXBufferBindingTy{DirectXBufferBindingTy::Kind::SRV, 0, 0, 4}}};
  const void *Pointer = reinterpret_cast<const void *>(uintptr_t{0x1000});
  alignas(void *) std::array<char, sizeof(Pointer) + 1> Slot{};
  std::memcpy(Slot.data() + 1, &Pointer, sizeof(Pointer));
  void *Args[] = {Slot.data() + 1};
  int64_t Sizes[] = {sizeof(Pointer)};
  DirectXArgumentBindingsTy Bindings;
  unsigned Calls = 0;
  auto Resolve = [&](const void *Address) -> Expected<DirectXResolvedBufferTy> {
    ++Calls;
    EXPECT_EQ(Address, Pointer);
    return error::createOffloadError(error::ErrorCode::NOT_FOUND,
                                     "not a live allocation in this context");
  };
  expectError(Bindings.prepare(Manifest, Args, Sizes, Resolve),
              error::ErrorCode::NOT_FOUND, "this context");
  EXPECT_EQ(Calls, 1u);
  Sizes[0] = sizeof(Pointer) - 1;
  expectError(Bindings.prepare(Manifest, Args, Sizes, Resolve),
              error::ErrorCode::INVALID_SIZE, "pointer-sized");
  Sizes[0] = sizeof(Pointer);
  Slot.fill(0);
  expectError(Bindings.prepare(Manifest, Args, Sizes, Resolve),
              error::ErrorCode::INVALID_ARGUMENT, "is null");
  EXPECT_EQ(Calls, 1u);
  EXPECT_TRUE(Bindings.Buffers.empty());
  std::memcpy(Slot.data() + 1, &Pointer, sizeof(Pointer));
  expectError(
      Bindings.prepare(Manifest, Args, Sizes,
                       [](const void *) -> Expected<DirectXResolvedBufferTy> {
                         return DirectXResolvedBufferTy{};
                       }),
      error::ErrorCode::BACKEND_FAILURE, "no resource");
}

TEST(DirectXArgumentsTest, EmptyKernelDoesNotResolveOrAllocate) {
  DirectXArgumentBindingsTy Bindings;
  auto Err = Bindings.prepare({}, {}, {}, noBuffers);
  ASSERT_FALSE(static_cast<bool>(Err)) << toString(std::move(Err));
  EXPECT_TRUE(Bindings.Constants.empty());
  EXPECT_TRUE(Bindings.Buffers.empty());
  EXPECT_EQ(Bindings.Heap.Get(), nullptr);
}

} // namespace
