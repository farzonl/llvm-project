// RUN: %clang_cl -### --target=x86_64-pc-windows-msvc /MD /EHsc -fsycl --offload-targets=dxil-pc-shadermodel6.0-compute /Fo%t.obj /Fe%t.exe %s 2>&1 | FileCheck %s --check-prefix=PIPELINE
// RUN: %clang_cl -### --target=x86_64-pc-windows-msvc /MD -fsycl --offload-targets=dxil-pc-shadermodel6.0-compute -c /Fo%t.obj %s 2>&1 | FileCheck %s --check-prefix=COMPILE
// RUN: touch %t.bc
// RUN: %clangxx -### --target=dxil-pc-shadermodel6.0-compute --sycl-link -Xlinker --dxil-validator=custom-dxv %t.bc 2>&1 | FileCheck %s --check-prefix=LINK
// RUN: not %clangxx -### --target=x86_64-unknown-linux-gnu -fsycl --offload-targets=dxil-pc-shadermodel6.0-compute %s 2>&1 | FileCheck %s --check-prefix=INVALID
// RUN: not %clang_cl -### --target=x86_64-pc-windows-msvc /MD -fsycl --offload-targets=dxil-pc-shadermodel6.0-pixel %s 2>&1 | FileCheck %s --check-prefix=INVALID

// PIPELINE: "-cc1" "-triple" "dxilv1.0-pc-shadermodel6.0-compute"
// PIPELINE-SAME: "-fsycl-is-device"
// PIPELINE-SAME: "-emit-llvm-bc"
// PIPELINE-SAME: "-D_WIN32" "-D_WIN64"
// PIPELINE-NOT: "-fcxx-exceptions"
// PIPELINE: llvm-offload-binary
// PIPELINE: "-fsycl-is-host"
// PIPELINE-SAME: "-fembed-offload-object=
// PIPELINE: clang-linker-wrapper
// PIPELINE-SAME: "--host-triple=x86_64-pc-windows-msvc

// /Fo applies only to the host output, not intermediate device files.
// COMPILE: "-fsycl-is-device"
// COMPILE-SAME: "-o" "[[DEVICE:[^"]*sycl-directx-[^"]+]]"
// COMPILE: llvm-offload-binary{{.*}}" "-o" "[[PACKAGE:[^"]*sycl-directx-[^"]+]]"
// COMPILE-SAME: "--image=file=[[DEVICE]],triple=dxilv1.0-pc-shadermodel6.0-compute,arch=generic,kind=sycl"
// COMPILE: "-fsycl-is-host"
// COMPILE-SAME: "-fembed-offload-object=[[PACKAGE]]"
// COMPILE-SAME: "-o" "{{.*}}.obj"

// LINK: clang-sycl-linker{{.*}}" "-triple=dxilv1.0-pc-shadermodel6.0-compute" "--dxil-validator=custom-dxv"
// INVALID: error: {{.*}}offload target

void f() {}
