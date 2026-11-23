#include <sycl/sycl.hpp>

void submitKernels(sycl::queue &Queue, unsigned *Output, unsigned Count) {
  sycl::nd_range<1> Range{sycl::range<1>{(Count + 63u) / 64u * 64u},
                          sycl::range<1>{64}};
  Queue.parallel_for<class Initialize>(
      Range, [=](sycl::nd_item<1> Item)
                 __attribute__((reqd_work_group_size(64, 1, 1))) {
                   unsigned I =
                       static_cast<unsigned>(Item.get_global_linear_id());
                   if (I < Count)
                     Output[I] = I + 1;
                 });
  Queue.parallel_for<class Scale>(
      Range, [=](sycl::nd_item<1> Item)
                 __attribute__((reqd_work_group_size(64, 1, 1))) {
                   unsigned I =
                       static_cast<unsigned>(Item.get_global_linear_id());
                   if (I < Count)
                     Output[I] *= 3;
                 });
}
