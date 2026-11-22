//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include <common/scoped_binary_registration.hpp>
#include <detail/offload/offload_utils.hpp>
#include <mock/helpers.hpp>

#include <sycl/__impl/queue.hpp>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cstring>
#include <limits>

using namespace sycl;
using namespace ::testing;

class sycl::detail::MockQueue : public sycl::queue {
public:
  using sycl::queue::setKernelLaunchParams;
  using sycl::queue::sycl_kernel_launch;
};

struct KernelData {
  char Arr[16];
  int SomeInt;
  double DoubleArr[8];
};

TEST(Queue, KernelLaunch) {
  mock::MockWrapper Mock;
  sycl::unittests::ScopedKernelRegistration Registration{"TestKernel"};
  sycl::detail::MockQueue Q;
  KernelData Data{};
  Data.Arr[0] = 'A';
  Data.Arr[1] = 'I';
  Data.SomeInt = 42;
  Data.DoubleArr[0] = 3.14;
  Data.DoubleArr[7] = -1.25;

  EXPECT_CALL(Mock.get(), olLaunchKernel(_, _, _, _, _, 1, _, _))
      .WillOnce([&Data](ol_queue_handle_t Queue, ol_device_handle_t Device,
                        ol_symbol_handle_t Kernel,
                        const ol_kernel_launch_size_args_t *LaunchSizeArgs,
                        const ol_kernel_launch_prop_t *Properties,
                        size_t NumArgs, void **ArgPtrs,
                        const size_t *ArgSizes) -> ol_result_t {
        EXPECT_NE(Queue, nullptr);
        EXPECT_NE(Device, nullptr);
        EXPECT_NE(Kernel, nullptr);
        EXPECT_NE(LaunchSizeArgs, nullptr);
        std::ignore = Properties;
        EXPECT_EQ(NumArgs, 1u);
        EXPECT_NE(ArgPtrs, nullptr);
        EXPECT_NE(ArgSizes, nullptr);
        EXPECT_EQ(ArgSizes[0], sizeof(KernelData));

        auto PayloadPtr = static_cast<const KernelData *>(ArgPtrs[0]);
        EXPECT_NE(PayloadPtr, nullptr);
        EXPECT_EQ(std::memcmp(PayloadPtr, &Data, sizeof(KernelData)), 0);
        return OL_SUCCESS;
      });
  Q.sycl_kernel_launch<class TestKernel>("TestKernel", Data);

  EXPECT_CALL(Mock.get(), olSyncQueue(_)).Times(1);
  Q.wait();
}

// Captures the ol_kernel_launch_size_args_t passed to olLaunchKernel.
// SetParams is called with the queue to invoke setKernelParameters.
static ol_kernel_launch_size_args_t captureKernelLaunchArgs(
    mock::MockWrapper &Mock,
    std::function<void(sycl::detail::MockQueue &)> SetParams) {
  // Keep registration alive across all parameterized runs.
  static sycl::unittests::ScopedKernelRegistration Reg{"DimSwapTestKernel"};
  sycl::detail::MockQueue Q;
  ol_kernel_launch_size_args_t Captured{};
  EXPECT_CALL(Mock.get(), olLaunchKernel(_, _, _, _, _, 1, _, _))
      .WillOnce([&Captured](ol_queue_handle_t, ol_device_handle_t,
                            ol_symbol_handle_t,
                            const ol_kernel_launch_size_args_t *Args,
                            const ol_kernel_launch_prop_t *, size_t, void **,
                            const size_t *) -> ol_result_t {
        Captured = *Args;
        return OL_SUCCESS;
      });
  SetParams(Q);
  KernelData Data{};
  Q.sycl_kernel_launch<class DimSwapTestKernel>("DimSwapTestKernel", Data);
  return Captured;
}

struct DimSwapParam {
  // Name used by the test runner to identify the case.
  const char *Description;
  // Calls kernel dependency/range setup on Q with the appropriate range.
  std::function<void(sycl::detail::MockQueue &)> SetParams;
  // Expected fields of ol_kernel_launch_size_args_t after the swap.
  uint32_t ExpDims;
  uint32_t ExpNGx, ExpNGy, ExpNGz;
  uint32_t ExpGSx, ExpGSy, ExpGSz;
};

class DimSwapTest : public ::testing::TestWithParam<DimSwapParam> {};

// Verifies that setKernelLaunchArgs correctly maps SYCL range dimensions to
// liboffload's x/y/z axes.
TEST_P(DimSwapTest, CheckLaunchArgs) {
  mock::MockWrapper Mock;
  const auto &P = GetParam();
  auto Args = captureKernelLaunchArgs(Mock, P.SetParams);

  EXPECT_EQ(Args.Dimensions, P.ExpDims);
  EXPECT_EQ(Args.NumGroups.x, P.ExpNGx);
  EXPECT_EQ(Args.NumGroups.y, P.ExpNGy);
  EXPECT_EQ(Args.NumGroups.z, P.ExpNGz);
  EXPECT_EQ(Args.GroupSize.x, P.ExpGSx);
  EXPECT_EQ(Args.GroupSize.y, P.ExpGSy);
  EXPECT_EQ(Args.GroupSize.z, P.ExpGSz);
}

INSTANTIATE_TEST_SUITE_P(
    DimensionSwap, DimSwapTest,
    ::testing::Values(
        // 1D nd_range: no swap.
        // global={8}, local={2} -> x=4 groups of 2
        DimSwapParam{"1D_NdRange",
                     [](sycl::detail::MockQueue &Q) {
                       sycl::nd_range<1> NDR(sycl::range<1>{8},
                                             sycl::range<1>{2});
                       Q.setKernelLaunchParams(std::vector<sycl::event>{}, NDR);
                     },
                     /*Dims=*/1, /*NG=*/4, 1, 1, /*GS=*/2, 1, 1},
        // 2D nd_range: swap [0]<->[1].
        // global={4,6}, local={2,3} -> after swap: global={6,4}, local={3,2}
        DimSwapParam{"2D_NdRange",
                     [](sycl::detail::MockQueue &Q) {
                       sycl::nd_range<2> NDR(sycl::range<2>{4, 6},
                                             sycl::range<2>{2, 3});
                       Q.setKernelLaunchParams(std::vector<sycl::event>{}, NDR);
                     },
                     /*Dims=*/2, /*NG=*/2, 2, 1, /*GS=*/3, 2, 1},
        // 3D nd_range: swap [0]<->[2].
        // global={2,4,6}, local={1,2,3} -> after swap: global={6,4,2},
        // local={3,2,1}
        DimSwapParam{"3D_NdRange",
                     [](sycl::detail::MockQueue &Q) {
                       sycl::nd_range<3> NDR(sycl::range<3>{2, 4, 6},
                                             sycl::range<3>{1, 2, 3});
                       Q.setKernelLaunchParams(std::vector<sycl::event>{}, NDR);
                     },
                     /*Dims=*/3, /*NG=*/2, 2, 2, /*GS=*/3, 2, 1},
        // 2D range (no local): swap [0]<->[1], GroupSize stays {1,1,1}.
        // global={4,6} -> after swap: global={6,4}
        DimSwapParam{"2D_Range",
                     [](sycl::detail::MockQueue &Q) {
                       sycl::range<2> R(4, 6);
                       Q.setKernelLaunchParams(std::vector<sycl::event>{}, R);
                     },
                     /*Dims=*/2, /*NG=*/6, 4, 1, /*GS=*/1, 1, 1}),
    [](const ::testing::TestParamInfo<DimSwapParam> &Info) {
      return Info.param.Description;
    });

TEST(Queue, TransportsGlobalOffsetsAndResetsThemForSingleTask) {
  mock::MockWrapper Mock;
  sycl::unittests::ScopedKernelRegistration Registration{"OffsetKernel"};
  sycl::detail::MockQueue Q;
  KernelData Data{};
  ol_kernel_global_offset_t Captured{};
  EXPECT_CALL(Mock.get(), olLaunchKernel(_, _, _, _, _, 1, _, _))
      .Times(2)
      .WillRepeatedly([&](ol_queue_handle_t, ol_device_handle_t,
                          ol_symbol_handle_t,
                          const ol_kernel_launch_size_args_t *,
                          const ol_kernel_launch_prop_t *Properties, size_t,
                          void **, const size_t *) -> ol_result_t {
        EXPECT_NE(Properties, nullptr);
        if (!Properties)
          return Mock.get().makeEmptyStrError(OL_ERRC_INVALID_ARGUMENT);
        EXPECT_EQ(Properties[0].type, OL_KERNEL_LAUNCH_PROP_TYPE_GLOBAL_OFFSET);
        Captured =
            *static_cast<const ol_kernel_global_offset_t *>(Properties[0].data);
        EXPECT_EQ(Properties[1].type, OL_KERNEL_LAUNCH_PROP_TYPE_NONE);
        return OL_SUCCESS;
      });
  sycl::nd_range<3> Range{sycl::range<3>{2, 4, 12}, sycl::range<3>{1, 2, 4},
                          sycl::id<3>{7, 11, (size_t{1} << 32) + 13}};
  Q.setKernelLaunchParams(std::vector<sycl::event>{}, Range);
  Q.sycl_kernel_launch<class OffsetKernel>("OffsetKernel", Data);
  EXPECT_EQ(Captured.x, (uint64_t{1} << 32) + 13);
  EXPECT_EQ(Captured.y, 11u);
  EXPECT_EQ(Captured.z, 7u);
  Q.setKernelLaunchParams(std::vector<sycl::event>{}, {});
  Q.sycl_kernel_launch<class OffsetKernel>("OffsetKernel", Data);
  EXPECT_EQ(Captured.x, 0u);
  EXPECT_EQ(Captured.y, 0u);
  EXPECT_EQ(Captured.z, 0u);
}

TEST(Queue, RejectsUnrepresentableRangesAndOffsets) {
  size_t Global[] = {size_t{1} << 32};
  size_t Local[] = {1};
  size_t Offset[] = {0};
  detail::UnifiedRangeView View(Global, Local, Offset, 1);
  EXPECT_THROW(detail::convertToOlRange(View), sycl::exception);
  Local[0] = 2;
  auto Converted = detail::convertToOlRange(View);
  EXPECT_EQ(Converted.NumGroups.x, uint32_t{1} << 31);
  EXPECT_EQ(Converted.GroupSize.x, 2u);
  Local[0] = size_t{1} << 32;
  EXPECT_THROW(detail::convertToOlRange(View), sycl::exception);
  Global[0] = 4;
  Local[0] = 0;
  EXPECT_THROW(detail::convertToOlRange(View), sycl::exception);
  Local[0] = 3;
  EXPECT_THROW(detail::convertToOlRange(View), sycl::exception);
  Local[0] = 1;
  Offset[0] = std::numeric_limits<size_t>::max();
  EXPECT_THROW(detail::convertToOlOffset(View), sycl::exception);
}
