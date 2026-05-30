/*
  RT-Thread DFS stat() ABI bridge (C compilation unit only).
 */

#include "AP_Filesystem_config.h"

#if defined(HAL_BOARD_RTT) && CONFIG_HAL_BOARD == HAL_BOARD_RTT && AP_FILESYSTEM_POSIX_ENABLED

#include "ap_rtt_posix_stat.h"

#include <string.h>
#include <sys/types.h>
#include <time.h>

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
#endif
#include "../../modules/rt-thread/components/libc/compilers/common/extension/sys/stat.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

typedef struct {
    dev_t st_dev;
    ino_t st_ino;
    mode_t st_mode;
    nlink_t st_nlink;
    uid_t st_uid;
    gid_t st_gid;
    dev_t st_rdev;
    off_t st_size;
    struct timespec st_atim;
    struct timespec st_mtim;
    struct timespec st_ctim;
    blksize_t st_blksize;
    blkcnt_t st_blocks;
    long st_spare4[2];
} ap_stat_out_t;

static void ap_stat_copy_from_rtt(const struct stat *src, ap_stat_out_t *dst)
{
    memset(dst, 0, sizeof(*dst));
    dst->st_dev = src->st_dev;
    dst->st_ino = (ino_t)src->st_ino;
    dst->st_mode = (mode_t)src->st_mode;
    dst->st_nlink = (nlink_t)src->st_nlink;
    dst->st_uid = (uid_t)src->st_uid;
    dst->st_gid = (gid_t)src->st_gid;
    dst->st_rdev = (dev_t)(uintptr_t)src->st_rdev;
    dst->st_size = (off_t)src->st_size;
    dst->st_atim.tv_sec = src->st_atime;
    dst->st_mtim.tv_sec = src->st_mtime;
    dst->st_ctim.tv_sec = src->st_ctime;
    dst->st_blksize = (blksize_t)src->st_blksize;
    dst->st_blocks = (blkcnt_t)src->st_blocks;
    dst->st_spare4[0] = src->st_spare4[0];
    dst->st_spare4[1] = src->st_spare4[1];
}

int ap_rtt_posix_stat(const char *pathname, void *stbuf, size_t stbuf_size)
{
    if (pathname == NULL || stbuf == NULL || stbuf_size == 0) {
        return -1;
    }

    struct stat st_native;
    const int ret = stat(pathname, &st_native);
    if (ret != 0) {
        return ret;
    }

    ap_stat_out_t st_out;
    ap_stat_copy_from_rtt(&st_native, &st_out);

    const size_t copy_len = stbuf_size < sizeof(st_out) ? stbuf_size : sizeof(st_out);
    memcpy(stbuf, &st_out, copy_len);
    return 0;
}

#endif
