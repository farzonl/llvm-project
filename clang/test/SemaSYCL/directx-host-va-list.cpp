// RUN: %clang_cc1 -fsycl-is-device -triple dxil-pc-shadermodel6.0-compute -aux-triple x86_64-pc-windows-msvc -fms-compatibility -DWCHAR_SIZE=2 -fsyntax-only -verify %s
// RUN: %clang_cc1 -fsycl-is-device -triple dxil-pc-shadermodel6.0-compute -aux-triple x86_64-pc-windows-msvc -fms-compatibility -fwchar-type=int -fsigned-wchar -DWCHAR_SIZE=4 -DSIGNED_WCHAR -fsyntax-only -verify %s
// expected-no-diagnostics

static_assert(__is_same(__builtin_va_list, char *));
static_assert(sizeof(wchar_t) == WCHAR_SIZE);
#ifdef SIGNED_WCHAR
static_assert(wchar_t(-1) < 0);
#else
static_assert(wchar_t(-1) > 0);
#endif

// Windows CRT headers contain unused inline variadic wrappers. Parsing these
// requires the host va_list type even though device varargs are unsupported.
inline void wrapper(const char *Format, ...) {
  char *Args;
  __builtin_va_start(Args, Format);
  __builtin_va_end(Args);
}
