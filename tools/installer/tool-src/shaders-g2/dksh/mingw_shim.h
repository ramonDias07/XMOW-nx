/* Windows (llvm-mingw) shim for uamlib's Mesa sources, force-included (-include): strchrnul is a glibc function. */
#ifndef XMOW_MINGW_SHIM_H
#define XMOW_MINGW_SHIM_H
#ifdef __MINGW32__
#include <string.h>
/* mingw-w64 already declares strndup; Mesa's strndup.h would redefine it (skip it with its include guard). */
#define STRNDUP_H 1
#ifdef __cplusplus
extern "C" {
#endif
static inline char *strchrnul(const char *s, int c) {
  while (*s && *s != (char)c) ++s;
  return (char *)s;
}
#ifdef __cplusplus
}
#endif
#endif
#endif
