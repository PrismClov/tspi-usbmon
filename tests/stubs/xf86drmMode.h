/* Compile-check stub, not shipped. */
#ifndef _STUB_XF86DRM_MODE_H
#define _STUB_XF86DRM_MODE_H
#include <stdint.h>
typedef struct _drmModeModeInfo {
    uint32_t clock; uint16_t hdisplay, hsync_start, hsync_end, htotal, hskew;
    uint16_t vdisplay, vsync_start, vsync_end, vtotal, vscan; uint32_t vrefresh;
    uint32_t flags, type; char name[32];
} drmModeModeInfo, *drmModeModeInfoPtr;
typedef struct _drmModeRes { int count_fbs; uint32_t *fbs; int count_crtcs; uint32_t *crtcs;
    int count_connectors; uint32_t *connectors; int count_encoders; uint32_t *encoders;
    uint32_t min_width, max_width, min_height, max_height; } drmModeRes, *drmModeResPtr;
typedef struct _drmModeConnector { uint32_t connector_id, encoder_id, connector_type,
    connector_type_id; uint32_t connection, mmWidth, mmHeight, subpixel;
    int count_modes; drmModeModeInfoPtr modes; int count_props; uint32_t *props; uint64_t *prop_values;
    int count_encoders; uint32_t *encoders; } drmModeConnector, *drmModeConnectorPtr;
typedef struct _drmModeEncoder { uint32_t encoder_id, encoder_type, crtc_id, possible_crtcs, possible_clones; }
    drmModeEncoder, *drmModeEncoderPtr;
typedef struct _drmModeCrtc { uint32_t crtc_id, buffer_id; uint32_t x, y, width, height;
    int mode_valid; drmModeModeInfo mode; int gamma_size; } drmModeCrtc, *drmModeCrtcPtr;
#define DRM_MODE_CONNECTED 1
#define DRM_MODE_CONNECTOR_Unknown 0
#define DRM_MODE_CONNECTOR_VGA 1
#define DRM_MODE_CONNECTOR_HDMIA 11
#define DRM_MODE_CONNECTOR_HDMIB 12
#define DRM_MODE_CONNECTOR_LVDS 13
#define DRM_MODE_CONNECTOR_eDP 14
#define DRM_MODE_CONNECTOR_DPI 15
#define DRM_MODE_CONNECTOR_DSI 16
#define DRM_MODE_CONNECTOR_DisplayPort 10
#define DRM_MODE_PAGE_FLIP_EVENT 0x01
#define DRM_MODE_ATOMIC_ALLOW_MODESET 0x0400
drmModeResPtr drmModeGetResources(int fd);
drmModeConnectorPtr drmModeGetConnector(int fd, uint32_t connectorId);
drmModeEncoderPtr drmModeGetEncoder(int fd, uint32_t encoder_id);
void drmModeFreeConnector(drmModeConnectorPtr ptr);
void drmModeFreeEncoder(drmModeEncoderPtr ptr);
void drmModeFreeResources(drmModeResPtr ptr);
int drmModeAddFB(int fd, uint32_t width, uint32_t height, uint8_t depth, uint8_t bpp,
                 uint32_t pitch, uint32_t bo_handle, uint32_t *buf_id);
int drmModeRmFB(int fd, uint32_t bufferId);
int drmModeSetCrtc(int fd, uint32_t crtcId, uint32_t bufferId, uint32_t x, uint32_t y,
                   uint32_t *connectors, int count, drmModeModeInfoPtr mode);
int drmModePageFlip(int fd, uint32_t crtc_id, uint32_t fb_id, uint32_t flags, void *user_data);
#endif
