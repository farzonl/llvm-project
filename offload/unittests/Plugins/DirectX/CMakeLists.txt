add_unittest(OffloadUnitTests directx.unittests DirectXTest.cpp)
target_link_libraries(directx.unittests PRIVATE LLVMOffload)
