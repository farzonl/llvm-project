//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

// REQUIRES: any-device
// RUN: %clangxx -fsycl %s -o %t.out
// RUN: %t.out

#include <sycl/sycl.hpp>

void initialize(sycl::queue &Queue, sycl::buffer<int> &Buffer) {
  Queue.submit([&](sycl::handler &Handler) {
    auto Output = Buffer.get_access<sycl::access_mode::write>(Handler);
    Handler.single_task<class BufferInitialize>([=] {
      for (unsigned I = 0; I < 8; ++I)
        Output[I] = static_cast<int>(I * 3);
    });
  });
}

void transform(sycl::queue &Queue, sycl::buffer<int> &Input,
               sycl::buffer<float> &Output) {
  Queue.submit([&](sycl::handler &Handler) {
    auto Read = Input.get_access<sycl::access_mode::read>(Handler);
    sycl::accessor Write(Output, Handler);
    Handler.single_task<class BufferTransform>([=] {
      for (unsigned I = 0; I < 8; ++I)
        Write[sycl::id<1>(I)] = static_cast<float>(Read[I]) + 0.5f;
    });
  });
}

void increment(sycl::queue &Queue, sycl::buffer<int> &Buffer) {
  Queue.submit([&](sycl::handler &Handler) {
    auto First = Buffer.get_access(Handler);
    auto Second = First;
    Handler.single_task<class BufferIncrement>([=] {
      for (unsigned I = 0; I < 8; ++I)
        Second[I] = First[I] + 1;
    });
  });
}

void copyWide(sycl::queue &Queue, sycl::buffer<long long> &Input,
              sycl::buffer<long long> &Output) {
  Queue.submit([&](sycl::handler &Handler) {
    sycl::accessor Read(Input, Handler, sycl::read_only);
    sycl::accessor Write(Output, Handler, sycl::write_only);
    Handler.single_task<class BufferWideCopy>([=] {
      for (unsigned I = 0; I < 3; ++I)
        Write[I] = Read[I] + 0x100000003LL;
    });
  });
}

void writeID(sycl::queue &Queue, sycl::buffer<sycl::id<3>> &Buffer,
             const sycl::id<3> &Values) {
  Queue.submit([&](sycl::handler &Handler) {
    sycl::accessor Output(Buffer, Handler, sycl::write_only);
    Handler.single_task<class BufferID>([=] {
      Output[0] = sycl::id<3>(0x123456789ABCDEF0ULL, 0xFEDCBA9876543210ULL,
                              0x100000003ULL);
      auto Modified = Values;
      Modified[0] = 7;
      Output[1] = Modified;
      Output[2] = sycl::id<3>(0, 0, 0);
    });
  });
}

void writeTemporaryBuffer(sycl::queue &Queue, int *Host) {
  Queue.submit([&](sycl::handler &Handler) {
    sycl::buffer<int> Local(Host, sycl::range<1>(8));
    sycl::accessor Access(Local, Handler);
    Handler.single_task<class BufferTemporaryLifetime>([=] {
      for (unsigned I = 0; I < 8; ++I)
        Access[I] = static_cast<int>(I * 7 + 1);
    });
  });
}

void readCapturedID(sycl::queue &Queue,
                    sycl::buffer<unsigned long long> &Buffer,
                    const sycl::id<3> &Values, int Index) {
  Queue.submit([&](sycl::handler &Handler) {
    sycl::accessor Output(Buffer, Handler, sycl::write_only);
    Handler.single_task<class BufferCapturedIDIndex>(
        [=] { Output[0] = Values[Index]; });
  });
}

template <int Dimensions> class BufferMultidimensional;

template <int Dimensions>
void updateMultidimensional(sycl::queue &Queue,
                            sycl::buffer<int, Dimensions> &Buffer) {
  Queue.submit([&](sycl::handler &Handler) {
    sycl::accessor Output(Buffer, Handler);
    Handler.parallel_for<BufferMultidimensional<Dimensions>>(
        Buffer.get_range(), [=](sycl::item<Dimensions> Item) {
          auto Index = Item.get_id();
          int Value = Output[Index] + static_cast<int>(Item.get_linear_id());
          if constexpr (Dimensions == 2)
            Output[Index[0]][Index[1]] = Value;
          else
            Output[Index[0]][Index[1]][Index[2]] = Value;
        });
  });
}

template void updateMultidimensional<2>(sycl::queue &, sycl::buffer<int, 2> &);
template void updateMultidimensional<3>(sycl::queue &, sycl::buffer<int, 3> &);

template <typename T, unsigned Count> class BufferByteUpdate;

template <typename T, unsigned Count>
void updateBytes(sycl::queue &Queue, sycl::buffer<T> &Buffer) {
  Queue.submit([&](sycl::handler &Handler) {
    auto Access = Buffer.get_access(Handler);
    Handler.parallel_for<BufferByteUpdate<T, Count>>(
        sycl::nd_range<1>{sycl::range<1>{(Count + 63) / 64 * 64},
                          sycl::range<1>{64}},
        [=](sycl::nd_item<1> Item)
            __attribute__((reqd_work_group_size(64, 1, 1))) {
              size_t I = Item.get_global_linear_id();
              if (I < Count) {
                if constexpr (std::is_same_v<T, bool>)
                  Access[I] = !Access[I];
                else if (I % 4 != 3)
                  Access[I] = static_cast<T>(Access[I] + I * 17 + 1);
              }
            });
  });
}

template void updateBytes<bool, 1>(sycl::queue &, sycl::buffer<bool> &);
template void updateBytes<bool, 5>(sycl::queue &, sycl::buffer<bool> &);
template void updateBytes<unsigned char, 1>(sycl::queue &,
                                            sycl::buffer<unsigned char> &);
template void updateBytes<unsigned char, 5>(sycl::queue &,
                                            sycl::buffer<unsigned char> &);
template void updateBytes<unsigned char, 1025>(sycl::queue &,
                                               sycl::buffer<unsigned char> &);
template void updateBytes<signed char, 5>(sycl::queue &,
                                          sycl::buffer<signed char> &);

struct CaptureValues {
  unsigned long long Values[2][2];
  float Scale;
  long long SignedValue;
  signed char SignedByte;
  unsigned char UnsignedByte;
  short SignedShort;
  bool Flag;
  wchar_t WideChar;
  unsigned char Sentinel;
};

void submitUnusedCapture(sycl::queue &Queue, sycl::buffer<int> &Buffer) {
  Queue.submit([&](sycl::handler &Handler) {
    auto Output = Buffer.get_access(Handler);
    Handler.single_task<class BufferUnusedCapture>([Output] { (void)Output; });
  });
}

void writeCapturedValues(sycl::queue &Queue, sycl::buffer<unsigned> &Buffer,
                         unsigned Bias, const CaptureValues &Values) {
  Queue.submit([&](sycl::handler &Handler) {
    auto Output = Buffer.get_access(Handler);
    Handler.single_task<class BufferArrayCapture>([=] {
      Output[0] = Bias + static_cast<unsigned>(Values.Values[0][0]);
      Output[1] = static_cast<unsigned>(Values.Values[0][1] >> 32);
      Output[2] = static_cast<unsigned>(Values.Values[1][0]);
      Output[3] = static_cast<unsigned>(Values.Values[1][1] >> 32);
      Output[4] = static_cast<unsigned>(Values.Scale * Bias);
      Output[5] = Values.SignedValue < -1
                      ? static_cast<unsigned>(Values.SignedValue >> 32)
                      : 0;
      Output[6] = static_cast<unsigned>(Values.SignedByte);
      Output[7] = Values.UnsignedByte;
      Output[8] = static_cast<unsigned>(Values.SignedShort);
      Output[9] = Values.Flag ? 1 : 0;
      Output[10] =
          static_cast<unsigned>(Values.Values[(Bias / 4) % 2][(Bias / 2) % 2]);
      Output[11] = static_cast<unsigned>(Values.WideChar);
      Output[12] = Values.Sentinel;
      Output[13] = static_cast<unsigned>(
          Values.SignedValue / static_cast<long long>(Values.WideChar));
    });
  });
}

#ifndef __SYCL_DEVICE_ONLY__
#  include <array>
#  include <atomic>
#  include <cassert>
#  include <chrono>
#  include <future>
#  include <iostream>
#  include <optional>
#  include <thread>

template <typename T, unsigned Count>
void checkBytes(sycl::queue &Queue, unsigned Repeats) {
  struct {
    unsigned char Before = 0xAB;
    std::array<T, Count> Values{};
    unsigned char After = 0xCD;
  } Host;
  for (unsigned I = 0; I < Count; ++I) {
    if constexpr (std::is_same_v<T, bool>)
      Host.Values[I] = I % 2 != 0;
    else
      Host.Values[I] = static_cast<T>(I % 2 ? 0x81 : 0xFE);
  }
  const auto Initial = Host.Values;
  {
    sycl::buffer<T> Buffer(Host.Values.data(), sycl::range<1>(Count));
    assert(Buffer.byte_size() == Count);
    for (unsigned I = 0; I < Repeats; ++I)
      updateBytes<T, Count>(Queue, Buffer);
  }
  for (unsigned I = 0; I < Count; ++I) {
    T Expected;
    if constexpr (std::is_same_v<T, bool>)
      Expected = Repeats % 2 ? !Initial[I] : Initial[I];
    else
      Expected = I % 4 == 3
                     ? Initial[I]
                     : static_cast<T>(Initial[I] + Repeats * (I * 17 + 1));
    assert(Host.Values[I] == Expected);
  }
  assert(Host.Before == 0xAB && Host.After == 0xCD);
}

void run(int Argc, char **Argv) {
  auto Devices = sycl::device::get_devices(sycl::info::device_type::gpu);
  auto Device = Devices.at(Argc > 1 ? std::stoul(Argv[1]) : 0);
  std::atomic<bool> AsyncFailed{false};
  const auto OnError = [&](sycl::exception_list Errors) {
    AsyncFailed = true;
    for (const auto &Error : Errors) {
      try {
        std::rethrow_exception(Error);
      } catch (const sycl::exception &Exception) {
        std::cerr << "Asynchronous buffer error: " << Exception.what() << '\n';
      }
    }
  };
  sycl::queue First(Device, OnError);
  sycl::queue Second(First.get_context(), Device, OnError);
  {
    int Values[3][5]{};
    {
      sycl::buffer<int, 2> Buffer(&Values[0][0], sycl::range<2>(3, 5));
      {
        sycl::host_accessor Host(Buffer, sycl::write_only);
        for (unsigned I = 0; I < 3; ++I)
          for (unsigned J = 0; J < 5; ++J)
            Host[I][J] = static_cast<int>(I * 100 + J * 7);
      }
      updateMultidimensional(First, Buffer);
      sycl::host_accessor Read(Buffer, sycl::read_only);
      static_assert(std::is_same_v<decltype(Read[0][0]), const int &>);
      assert(Read.get_range() == sycl::range<2>(3, 5));
      assert(Read.size() == 15);
      for (unsigned I = 0; I < 3; ++I)
        for (unsigned J = 0; J < 5; ++J)
          assert(Read[sycl::id<2>(I, J)] ==
                 static_cast<int>(I * 100 + J * 7 + I * 5 + J));
    }
    assert(Values[2][4] == 242);
  }
  {
    sycl::buffer<int, 3> Buffer(sycl::range<3>(2, 3, 5));
    {
      sycl::host_accessor Host(Buffer);
      for (unsigned I = 0; I < 2; ++I)
        for (unsigned J = 0; J < 3; ++J)
          for (unsigned K = 0; K < 5; ++K)
            Host[I][J][K] = 17;
    }
    updateMultidimensional(Second, Buffer);
    sycl::host_accessor Read(Buffer, sycl::read_only);
    static_assert(std::is_same_v<decltype(Read[0][0][0]), const int &>);
    assert(Read.get_range() == sycl::range<3>(2, 3, 5));
    for (unsigned I = 0; I < 2; ++I)
      for (unsigned J = 0; J < 3; ++J)
        for (unsigned K = 0; K < 5; ++K)
          assert(Read[I][J][K] == static_cast<int>(17 + (I * 3 + J) * 5 + K));
  }
  for (auto Range :
       {sycl::range<2>(0, 5),
        sycl::range<2>(std::numeric_limits<std::size_t>::max(), 2),
        sycl::range<2>(std::numeric_limits<std::size_t>::max() / 2 + 1, 3)}) {
    bool Rejected = false;
    try {
      sycl::buffer<unsigned char, 2> Invalid(Range);
    } catch (const sycl::exception &Error) {
      Rejected = Error.code() == sycl::make_error_code(sycl::errc::invalid);
    }
    assert(Rejected);
  }
  {
    sycl::buffer<int> Owned(sycl::range<1>(8));
    {
      sycl::host_accessor Host(Owned, sycl::write_only);
      static_assert(std::is_same_v<decltype(Host[0]), int &>);
      for (unsigned I = 0; I < 8; ++I)
        Host[I] = static_cast<int>(I * 9);
    }
    increment(First, Owned);
    {
      sycl::host_accessor Read(Owned, sycl::read_only);
      static_assert(std::is_same_v<decltype(Read[0]), const int &>);
      for (unsigned I = 0; I < 8; ++I)
        assert(Read[I] == static_cast<int>(I * 9 + 1));
      assert(Read.byte_size() == 8 * sizeof(int));
      assert(Read.get_range() == sycl::range<1>(8));
      const auto Copy = Read;
      assert(Copy.get_pointer() == Read.get_pointer());
    }
    using HostAccess = sycl::host_accessor<int>;
    std::optional<HostAccess> Host(std::in_place, Owned);
    std::optional<HostAccess> Retained(*Host);
    std::optional<HostAccess> Unrelated;
    std::optional<sycl::buffer<int>> Other(std::in_place, sycl::range<1>(1));
    Unrelated.emplace(*Other);
    (*Unrelated)[0] = 91;
    Host->operator[](0) = 123;
    std::promise<void> Started;
    auto Start = Started.get_future();
    auto Submission = std::async(std::launch::async, [&] {
      Started.set_value();
      increment(Second, Owned);
    });
    Start.get();
    bool Blocked = Submission.wait_for(std::chrono::milliseconds(100)) ==
                   std::future_status::timeout;
    Other.reset();
    Unrelated.reset();
    Host.reset();
    bool CopyBlocked = Submission.wait_for(std::chrono::milliseconds(100)) ==
                       std::future_status::timeout;
    Retained.reset();
    Submission.get();
    assert(Blocked && CopyBlocked);
    {
      sycl::host_accessor Result(Owned, sycl::read_only);
      assert(Result[0] == 124);
    }
    using ReadAccess = sycl::host_accessor<int, 1, sycl::access_mode::read>;
    std::optional<ReadAccess> ReadA(std::in_place, Owned);
    std::optional<ReadAccess> ReadB;
    std::promise<void> ReadersStarted;
    auto ReadersStart = ReadersStarted.get_future();
    auto ReadersSubmission = std::async(std::launch::async, [&] {
      ReadersStarted.set_value();
      increment(First, Owned);
    });
    ReadersStart.get();
    bool FirstReaderBlocked =
        ReadersSubmission.wait_for(std::chrono::milliseconds(100)) ==
        std::future_status::timeout;
    ReadB.emplace(Owned);
    ReadA.reset();
    bool ReaderBlocked =
        ReadersSubmission.wait_for(std::chrono::milliseconds(100)) ==
        std::future_status::timeout;
    ReadB.reset();
    ReadersSubmission.get();
    assert(FirstReaderBlocked && ReaderBlocked);
    sycl::host_accessor Result(Owned, sycl::read_only);
    assert(Result[0] == 125);
  }
  {
    std::optional<sycl::host_accessor<int>> Retained;
    int HostValue = 9;
    {
      sycl::buffer<int> Buffer(&HostValue, sycl::range<1>(1));
      Retained.emplace(Buffer);
      (*Retained)[0] = 47;
    }
    assert((*Retained)[0] == 47);
    Retained.reset();
    assert(HostValue == 47);
  }
  {
    sycl::buffer<sycl::id<3>> Buffer{3};
    const sycl::id<3> CapturedID(0x8765432112345678ULL, 0xABCDEF0123456789ULL,
                                 0x200000005ULL);
    writeID(First, Buffer, CapturedID);
    sycl::host_accessor Result(Buffer, sycl::read_only);
    if (Result[0] != sycl::id<3>(0x123456789ABCDEF0ULL, 0xFEDCBA9876543210ULL,
                                 0x100000003ULL))
      std::cerr << "Wide ID transport mismatch: " << std::hex << Result[0][0]
                << ", " << Result[0][1] << ", " << Result[0][2] << std::dec
                << '\n';
    assert(Result[0] == sycl::id<3>(0x123456789ABCDEF0ULL,
                                    0xFEDCBA9876543210ULL, 0x100000003ULL));
    assert(Result[1] == sycl::id<3>(7, CapturedID[1], CapturedID[2]));
    assert(Result[2] == sycl::id<3>(0, 0, 0));
    sycl::buffer<unsigned long long> Indexed{1};
    for (int I = 0; I < 3; ++I) {
      readCapturedID(First, Indexed, CapturedID, I);
      sycl::host_accessor Element(Indexed, sycl::read_only);
      assert(Element[0] == CapturedID[I]);
    }
  }
  {
    long long Values[] = {0x123456789ABCDE0LL, -0x123456789ABCDE0LL, -1};
    sycl::buffer<long long> Input(Values, sycl::range<1>(3));
    sycl::buffer<long long> Output{3};
    copyWide(First, Input, Output);
    sycl::host_accessor Result(Output, sycl::read_only);
    for (unsigned I = 0; I < 3; ++I)
      assert(Result[I] == Values[I] + 0x100000003LL);
  }
  checkBytes<bool, 1>(First, 1);
  checkBytes<bool, 5>(First, 3);
  checkBytes<unsigned char, 1>(First, 2);
  checkBytes<unsigned char, 5>(First, 3);
  checkBytes<unsigned char, 1025>(First, 16);
  checkBytes<signed char, 5>(First, 3);
  int Unchanged = 73;
  {
    sycl::buffer<int> Buffer(&Unchanged, sycl::range<1>(1));
    submitUnusedCapture(First, Buffer);
  }
  assert(Unchanged == 73);
  CaptureValues Captured{{{0x123456789ABCDEF0ULL, 0xFEDCBA9876543210ULL},
                          {0x0BADF00DDEADBEEFULL, 0x89ABCDEF01234567ULL}},
                         0.5f,
                         -0x100000003LL,
                         -101,
                         203,
                         -12345,
                         true,
                         wchar_t(0xFFF1),
                         0xA5};
  for (unsigned Bias : {6u, 10u}) {
    Captured.Flag = Bias == 6;
    std::array<unsigned, 14> Result{};
    {
      sycl::buffer<unsigned> Buffer(Result.data(), sycl::range<1>(14));
      writeCapturedValues(First, Buffer, Bias, Captured);
    }
    assert(Result[0] == Bias + static_cast<unsigned>(Captured.Values[0][0]));
    assert(Result[1] == static_cast<unsigned>(Captured.Values[0][1] >> 32));
    assert(Result[2] == static_cast<unsigned>(Captured.Values[1][0]));
    assert(Result[3] == static_cast<unsigned>(Captured.Values[1][1] >> 32));
    assert(Result[4] == static_cast<unsigned>(Captured.Scale * Bias));
    assert(Result[5] == static_cast<unsigned>(Captured.SignedValue >> 32));
    assert(Result[6] == static_cast<unsigned>(Captured.SignedByte));
    assert(Result[7] == Captured.UnsignedByte);
    assert(Result[8] == static_cast<unsigned>(Captured.SignedShort));
    assert(Result[9] == static_cast<unsigned>(Captured.Flag));
    assert(Result[10] == static_cast<unsigned>(
                             Captured.Values[(Bias / 4) % 2][(Bias / 2) % 2]));
    assert(Result[11] == static_cast<unsigned>(Captured.WideChar));
    assert(Result[12] == Captured.Sentinel);
    assert(Result[13] ==
           static_cast<unsigned>(Captured.SignedValue /
                                 static_cast<long long>(Captured.WideChar)));
    Captured.Values[0][0] += 7;
    Captured.Values[0][1] -= 0x100000000ULL;
    Captured.SignedValue -= 0x100000000LL;
  }
  std::array<int, 8> Input{};
  std::array<float, 8> Output{};
  for (unsigned I = 0; I < 8; ++I)
    Input[I] = static_cast<int>(I + 9);
  {
    sycl::buffer<int> InputBuffer(Input.data(), sycl::range<1>(8));
    sycl::buffer<float> OutputBuffer(Output.data(), sycl::range<1>(8));
    transform(Second, InputBuffer, OutputBuffer);
  }
  for (unsigned I = 0; I < 8; ++I) {
    assert(Input[I] == static_cast<int>(I + 9));
    assert(Output[I] == static_cast<float>(I) + 9.5f);
  }
  {
    std::array<int, 8> Concurrent{};
    {
      sycl::buffer<int> Buffer(Concurrent.data(), sycl::range<1>(8));
      std::exception_ptr Errors[2];
      auto Worker = [&](sycl::queue &Queue, unsigned Index) {
        try {
          for (unsigned I = 0; I < 4; ++I)
            increment(Queue, Buffer);
        } catch (const sycl::exception &) {
          Errors[Index] = std::current_exception();
        }
      };
      std::thread A(Worker, std::ref(First), 0);
      std::thread B(Worker, std::ref(Second), 1);
      A.join();
      B.join();
      for (const auto &Error : Errors)
        if (Error)
          std::rethrow_exception(Error);
    }
    for (int Value : Concurrent)
      assert(Value == 8);
  }
  {
    sycl::buffer<int> InputBuffer(Input.data(), sycl::range<1>(8));
    sycl::buffer<float> OutputBuffer(Output.data(), sycl::range<1>(8));
    initialize(First, InputBuffer);
    increment(Second, InputBuffer);
    transform(First, InputBuffer, OutputBuffer);
    auto Copy = InputBuffer;
    assert(Copy.size() == 8 && Copy.byte_size() == sizeof(Input));
  }
  for (unsigned I = 0; I < 8; ++I) {
    assert(Input[I] == static_cast<int>(I * 3 + 1));
    assert(Output[I] == static_cast<float>(I * 3) + 1.5f);
  }
  {
    std::array<int, 8> Host{};
    writeTemporaryBuffer(First, Host.data());
    for (unsigned I = 0; I < 8; ++I)
      assert(Host[I] == static_cast<int>(I * 7 + 1));
  }
  {
    std::optional<sycl::accessor<int>> Retained;
    std::array<int, 8> Host{};
    {
      sycl::buffer<int> Buffer(Host.data(), sycl::range<1>(8));
      First.submit(
          [&](sycl::handler &Handler) { Retained.emplace(Buffer, Handler); });
      initialize(First, Buffer);
    }
    for (unsigned I = 0; I < 8; ++I)
      assert(Host[I] == static_cast<int>(I * 3));
    Host[0] = 99;
    Retained.reset();
    assert(Host[0] == 99);
  }
  {
    std::array<int, 8> Migrated{};
    {
      sycl::buffer<int> Buffer(Migrated.data(), sycl::range<1>(8));
      initialize(First, Buffer);
      // A second context cannot consume native events from the first context.
      sycl::queue OtherContext(Device, OnError);
      increment(OtherContext, Buffer);
      increment(First, Buffer);
    }
    for (unsigned I = 0; I < 8; ++I)
      assert(Migrated[I] == static_cast<int>(I * 3 + 2));
  }
  std::cout << Device.get_info<sycl::info::device::name>()
            << ": buffer dependencies, host access, wide/byte storage and "
               "writeback passed\n";
  assert(!AsyncFailed.load());
}

int main(int Argc, char **Argv) {
  try {
    run(Argc, Argv);
  } catch (const sycl::exception &Error) {
    std::cerr << "Buffer test failed: " << Error.what() << '\n';
    return 1;
  }
}
#endif
