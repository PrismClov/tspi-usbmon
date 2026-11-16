/* Compile-check stub, not shipped. */
#ifndef _STUB_XF86DRM_H
#define _STUB_XF86DRM_H
#include <stdint.h>
#include <sys/ioctl.h>
#define DRM_IOCTL_BASE 'd'
struct drm_mode_create_dumb { uint32_t height, width, bpp, flags, handle, pitch; uint64_t size; };
struct drm_mode_map_dumb { uint32_t handle, pad; uint64_t offset; };
struct drm_mode_destroy_dumb { uint32_t handle; };
#define DRM_IOCTL_MODE_CREATE_DUMB _IOWR(DRM_IOCTL_BASE, 0xb2, struct drm_mode_create_dumb)
#define DRM_IOCTL_MODE_MAP_DUMB _IOWR(DRM_IOCTL_BASE, 0xb3, struct drm_mode_map_dumb)
#define DRM_IOCTL_MODE_DESTROY_DUMB _IOWR(DRM_IOCTL_BASE, 0xb4, struct drm_mode_destroy_dumb)
int drmSetMaster(int fd);
int drmDropMaster(int fd);
typedef struct _drmEventContext {
    int version;
    void (*vblank_handler)(int fd, unsigned int sequence, unsigned int tv_sec,
                           unsigned int tv_usec, void *user_data);
    void (*page_flip_handler)(int fd, unsigned int sequence, unsigned int tv_sec,
                              unsigned int tv_usec, void *user_data);
} drmEventContext, *drmEventContextPtr;
#define DRM_EVENT_CONTEXT_VERSION 4
int drmHandleEvent(int fd, drmEventContextPtr evctx);
#endif
