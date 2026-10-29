// SPDX-License-Identifier: MIT
/* Opt-in, process-local KMSRO render-device selection for MV100. */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

struct mv100_drm_version {
    int major, minor, patch;
    size_t name_len; char *name;
    size_t date_len; char *date;
    size_t desc_len; char *desc;
};
static int (*original_open)(const char *, const char *, int);
static pthread_once_t once = PTHREAD_ONCE_INIT;
static void resolve_open(void)
{
    original_open = dlsym(RTLD_NEXT, "drmOpenWithType");
}

int drmOpenWithType(const char *name, const char *busid, int type)
{
    const char *enabled = getenv("MV100_KMSRO_LIMA_ONLY");
    if (!enabled || strcmp(enabled, "1") || type != 2 || !name || (busid && *busid)) {
        pthread_once(&once, resolve_open);
        if (original_open) return original_open(name, busid, type);
        errno = ENOSYS; return -1;
    }
    if (getenv("MV100_DRM_TRACE"))
        fprintf(stderr, "mv100-kmsro: render probe %s\n", name);
    if (strcmp(name, "lima")) {
        errno = ENODEV; return -1;
    }
    int fd = open("/dev/dri/renderD128", O_RDWR | O_CLOEXEC);
    if (fd < 0) return -1;
    char driver[64] = {0};
    struct mv100_drm_version version = { .name_len = sizeof(driver)-1, .name = driver };
    if (ioctl(fd, _IOWR('d', 0x00, struct mv100_drm_version), &version) ||
            version.name_len != 4 || memcmp(driver, "lima", 4)) {
        close(fd); errno = ENODEV; return -1;
    }
    return fd;
}
