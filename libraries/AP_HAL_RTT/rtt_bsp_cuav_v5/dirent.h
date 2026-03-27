/*
 * Minimal dirent.h shim for RT-Thread + ArduPilot WAF build.
 *
 * Newlib's bare-metal dirent.h (#error "<dirent.h> not supported") cannot
 * be used. RT-Thread DFS provides a full POSIX opendir/readdir/closedir
 * implementation. This shim exposes the ABI without pulling in <rtdef.h>
 * (which conflicts with newlib <sys/time.h> via struct timespec redefinition).
 *
 * The layout of struct dirent and DIR here MUST match RT-Thread's dfs_posix.h.
 */

#ifndef __AP_HAL_RTT_DIRENT_H__
#define __AP_HAL_RTT_DIRENT_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* File type constants - match RT-Thread dfs_posix.h */
#define DT_UNKNOWN  0x00
#define DT_FIFO     0x01
#define DT_CHR      0x02
#define DT_DIR      0x04
#define DT_BLK      0x06
#define DT_REG      0x08
#define DT_LNK      0x0a
#define DT_SOCK     0x0c
#define DT_SYMLINK  DT_LNK

#define DIRENT_NAME_MAX  256

#ifndef HAVE_DIRENT_STRUCTURE
#define HAVE_DIRENT_STRUCTURE
struct dirent {
    uint8_t  d_type;
    uint8_t  d_namlen;
    uint16_t d_reclen;
    char     d_name[DIRENT_NAME_MAX];
};
#endif

#ifndef HAVE_DIR_STRUCTURE
#define HAVE_DIR_STRUCTURE
typedef struct {
    int  fd;
    char buf[512];
    int  num;
    int  cur;
} DIR;
#endif

int            closedir(DIR *dirp);
DIR           *opendir(const char *name);
struct dirent *readdir(DIR *dirp);
int            readdir_r(DIR *dirp, struct dirent *entry, struct dirent **result);
void           rewinddir(DIR *dirp);
void           seekdir(DIR *dirp, long offset);
long           telldir(DIR *dirp);

#ifdef __cplusplus
}
#endif

#endif /* __AP_HAL_RTT_DIRENT_H__ */
