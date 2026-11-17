/*
 * tspi-usbmon - USB secondary display daemon for LCKFB Taishan Pi (RK3566)
 * with Tianma TL060FVXS07/TL060FVXS08 1080x2160 MIPI-DSI panel.
 *
 * The device speaks the "udisp" protocol used by
 *   - Espressif esp-iot-solution usb_extend_screen (Windows IDD driver)
 *   - panyihang/USB-DRM-udisp-driver               (Linux DRM/KMS driver)
 * so both hosts work without host-side changes.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef USBMON_H
#define USBMON_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define UM_VERSION "1.0.0"

/* Protocol constants (must match the host drivers). */
#define UM_HDR_SIZE 16u
#define UM_TYPE_JPEG 3u
#define UM_TYPE_END 0xffu
#define UM_HID_MAX_CONTACTS 5
#define UM_HID_REPORT_SIZE 52u
#define UM_MAX_DIM 8192

/* ---------------------------------------------------------------- logging */

enum um_log_level {
    UM_LOG_ERROR = 0,
    UM_LOG_WARN = 1,
    UM_LOG_INFO = 2,
    UM_LOG_DEBUG = 3,
};

void um_log_set_level(enum um_log_level level);
void um_log(enum um_log_level level, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));

#define um_err(...) um_log(UM_LOG_ERROR, __VA_ARGS__)
#define um_warn(...) um_log(UM_LOG_WARN, __VA_ARGS__)
#define um_info(...) um_log(UM_LOG_INFO, __VA_ARGS__)
#define um_dbg(...) um_log(UM_LOG_DEBUG, __VA_ARGS__)

/* ------------------------------------------------------------------ config */

struct um_config {
    /* stream geometry advertised to the host */
    int stream_w;
    int stream_h;
    /* clockwise rotation applied to incoming frames to reach panel orientation */
    int rotate;
    /* rotation the *host driver* applies when it turns its framebuffer into
     * the stream we receive (Linux udisp_drm rotate=90 by default, Windows 0) */
    int host_rotate;
    /* host framebuffer size, derived from stream + host_rotate */
    int host_w, host_h;
    /* requested refresh rate advertised to the host (EDID / pacing) */
    int fps;
    /* JPEG quality advertised to the host, 1..10 */
    int jpeg_quality;
    /* largest accepted frame payload in bytes */
    uint32_t max_frame;
    /* DRM */
    char card[64];
    char connector[32];
    char mode[32]; /* "auto" or WxH[@R] */
    int brightness; /* -1 keeps current value */
    /* input */
    char touch[64]; /* "auto" or /dev/input/eventX */
    /* gadget */
    char udc[64]; /* "auto" or UDC name */
    int takeover_udc; /* unbind another gadget (adbd) occupying the UDC */
    int no_gadget;    /* skip USB gadget entirely (display-only bring-up) */
    int tcp_port;     /* >0: read frames from 127.0.0.1:<port> instead of USB */
    int attach;       /* attach to an existing configfs gadget (no bind) */
    char product[128];
    char interface[128];
    uint16_t vid;
    uint16_t pid;
    /* misc */
    char splash[128];
    int verbose;
};

void um_config_defaults(struct um_config *cfg);
/* Returns 0 on success, -1 on a malformed config file. */
int um_config_load(struct um_config *cfg, const char *path);
/* Apply "key=value" overrides from the command line. */
int um_config_set(struct um_config *cfg, const char *kv);
void um_config_build_strings(struct um_config *cfg);

/* ---------------------------------------------------------------- geometry */

/*
 * Describes how the host framebuffer (stream) is mapped onto the panel:
 * stream --rotate(CW)--> rotated image --scale+letterbox--> panel.
 * Used both to compose frames and to map touch coordinates back.
 */
struct um_geom {
    int sw, sh;   /* stream size */
    int rot;      /* 0, 90, 180, 270 clockwise */
    int rw, rh;   /* size after rotation */
    int pw, ph;   /* panel size */
    int dx, dy;   /* destination rect on panel */
    int dw, dh;
    int *xmap;    /* dw entries: destination x -> rotated x */
    int *ymap;    /* dh entries */
};

int um_geom_init(struct um_geom *g, int sw, int sh, int rot, int pw, int ph);
void um_geom_fini(struct um_geom *g);
/* Panel pixel -> stream pixel (touch), clamped into the stream rectangle. */
void um_geom_panel_to_stream(const struct um_geom *g, int px, int py, int *sx, int *sy);

/* Host framebuffer size for a stream geometry + host-side rotation. */
void um_geom_host_dims(int sw, int sh, int host_rotate, int *hw, int *hh);
/* Stream pixel -> host framebuffer pixel (inverse of the host rotation). */
void um_geom_stream_to_host(int sw, int sh, int host_rotate, int sx, int sy,
                            int *hx, int *hy);

/*
 * Compose one BGRX (little endian 0xXXRRGGBB in memory) frame into an
 * XRGB8888 framebuffer. Valid content of the source is the top-left
 * sw x sh area of the (possibly 16-pixel aligned) jpeg image.
 */
void um_compose(uint8_t *fb, int fb_pitch, int fb_w, int fb_h,
                const uint8_t *src, int src_w, int src_h, int src_pitch,
                const struct um_geom *g);

/* Fit an image (e.g. splash) into the whole panel, preserving aspect ratio. */
void um_compose_fit(uint8_t *fb, int fb_pitch, int fb_w, int fb_h,
                    const uint8_t *src, int src_w, int src_h, int src_pitch);

/* ----------------------------------------------------------------- protocol */

struct um_frame_hdr {
    uint8_t type;
    uint8_t cmd;
    uint16_t x, y;
    uint16_t width, height;
    uint16_t id;
    uint32_t payload;
};

struct um_proto {
    uint8_t hdr[UM_HDR_SIZE];
    size_t have;
    uint32_t limit;
    uint32_t frames;
    uint32_t resyncs;
};

enum um_proto_result {
    UM_PROTO_NEED_MORE = 0,
    UM_PROTO_HEADER = 1, /* header completed, payload follows */
    UM_PROTO_RESYNC = -1, /* invalid header, one byte was discarded */
};

void um_proto_init(struct um_proto *p, uint32_t limit);
void um_proto_reset(struct um_proto *p);
/* Validate the 16 raw header bytes. */
bool um_hdr_parse(const uint8_t raw[UM_HDR_SIZE], uint32_t limit, struct um_frame_hdr *out);
/*
 * Consume bytes until a header is complete. *used receives the number of
 * bytes consumed from data (>= 1). On UM_PROTO_HEADER the caller must then
 * read exactly out->payload bytes for the frame body.
 */
enum um_proto_result um_proto_scan(struct um_proto *p, const uint8_t *data, size_t len,
                                   size_t *used, struct um_frame_hdr *out);

/* ---------------------------------------------------------------------- HID */

size_t um_hid_descriptor(uint8_t *buf, size_t cap, uint16_t width, uint16_t height);

struct um_touch_point {
    uint8_t id;
    uint16_t x, y;
    uint16_t w, h;
};

size_t um_hid_touch_report(uint8_t out[UM_HID_REPORT_SIZE],
                           const struct um_touch_point *pts, int count);

#endif /* USBMON_H */
