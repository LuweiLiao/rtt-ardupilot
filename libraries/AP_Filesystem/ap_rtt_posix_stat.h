/*
  RT-Thread DFS stat() ABI bridge for AP_Filesystem_Posix.
 */
#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

int ap_rtt_posix_stat(const char *pathname, void *stbuf, size_t stbuf_size);

#ifdef __cplusplus
}
#endif
