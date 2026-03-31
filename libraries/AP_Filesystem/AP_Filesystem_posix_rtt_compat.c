#include "AP_Filesystem_config.h"

#if AP_FILESYSTEM_POSIX_ENABLED && CONFIG_HAL_BOARD == HAL_BOARD_RTT

#include <stdint.h>
#include "../../modules/rt-thread/components/libc/compilers/common/extension/sys/stat.h"

struct ap_rtt_stat_compat {
    uint16_t st_mode;
    uint32_t st_size;
    uint32_t st_atime;
    uint32_t st_mtime;
    uint32_t st_ctime;
    uint32_t st_blksize;
    uint32_t st_blocks;
};

int ap_rtt_posix_stat(const char *pathname, struct ap_rtt_stat_compat *out)
{
    struct stat st;
    const int ret = stat(pathname, &st);
    if (ret != 0 || out == 0) {
        return ret;
    }

    out->st_mode = st.st_mode;
    out->st_size = st.st_size;
    out->st_atime = (uint32_t)st.st_atime;
    out->st_mtime = (uint32_t)st.st_mtime;
    out->st_ctime = (uint32_t)st.st_ctime;
    out->st_blksize = st.st_blksize;
    out->st_blocks = st.st_blocks;
    return ret;
}

#endif
