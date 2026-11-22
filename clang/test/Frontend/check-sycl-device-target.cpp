// RUN: not %clang_cc1 -triple x86_64-unknown-unknown -fsycl-is-device %s 2>&1 | FileCheck --check-prefixes=INVALID %s
// RUN: %clang_cc1 -triple spirv64-unknown-unknown -fsyntax-only -fsycl-is-device -verify=valid %s
// RUN: %clang_cc1 -fsyntax-only -fsycl-is-device -verify=valid %s
// RUN: %clang_cc1 -triple dxil-pc-shadermodel6.0-compute -aux-triple x86_64-pc-windows-msvc -fsyntax-only -fsycl-is-device -verify=valid %s
// RUN: not %clang_cc1 -triple dxil-pc-shadermodel6.0-pixel -fsycl-is-device %s 2>&1 | FileCheck --check-prefix=INVALID-DXIL %s

// These tests validate the target for SYCL device compilation

// INVALID: x86_64-unknown-unknown is not a supported SYCL device target
// INVALID-DXIL: is not a supported SYCL device target

// valid-no-diagnostics

