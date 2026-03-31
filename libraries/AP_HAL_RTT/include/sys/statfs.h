/*
 * sys/statfs.h shim for AP_HAL_RTT (newlib + RT-Thread DFS)
 */
#pragma once

#include <sys/types.h>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"

#ifdef __cplusplus
extern "C" {
#endif

struct statfs
{
    long f_type;
    long f_bsize;
    long f_blocks;
    long f_bfree;
    long f_bavail;
    long f_files;
    long f_ffree;
    long f_namelen;
};

int statfs(const char *path, struct statfs *buf);
int fstatfs(int fd, struct statfs *buf);

#ifdef __cplusplus
}
#endif

#pragma GCC diagnostic pop
