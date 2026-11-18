/*
 * DRM/KMS output on the MIPI-DSI panel (triple buffered, page flipped).
 * SPDX-License-Identifier: MIT
 */
#define _GNU_SOURCE
#include "display.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#ifdef UM_USE_RGA
#include <RgaApi.h>
#endif

struct dbuf {
    int dma_fd;
    uint32_t fb;
    uint32_t handle;
    uint32_t pitch;
    uint64_t size;
    uint8_t *map;
};

struct um_display {
    int fd;
    uint32_t crtc;
    uint32_t conn;
    drmModeModeInfo mode;
    int w, h;
    struct dbuf buf[3];
    int front, render, pending;
    int flip_done;
    int use_flip;
    int rga_ready;
    int rga_initialized;
    /* state saved at open so we can hand the CRTC back cleanly on exit */
    int saved_valid;
    drmModeCrtc *saved_crtc;
};

static const char *conn_type_name(uint32_t type)
{
    switch (type) {
    case DRM_MODE_CONNECTOR_DSI:
        return "DSI";
    case DRM_MODE_CONNECTOR_eDP:
        return "eDP";
    case DRM_MODE_CONNECTOR_HDMIA:
        return "HDMI-A";
    case DRM_MODE_CONNECTOR_HDMIB:
        return "HDMI-B";
    case DRM_MODE_CONNECTOR_LVDS:
        return "LVDS";
    case DRM_MODE_CONNECTOR_VGA:
        return "VGA";
    case DRM_MODE_CONNECTOR_DPI:
        return "DPI";
    case DRM_MODE_CONNECTOR_DisplayPort:
        return "DP";
    default:
        return "Unknown";
    }
}

static int connector_wanted(const char *want, drmModeConnector *c)
{
    char name[48];

    if (!strcmp(want, "auto"))
        return c->connector_type == DRM_MODE_CONNECTOR_DSI;
    snprintf(name, sizeof(name), "%s-%u", conn_type_name(c->connector_type),
             c->connector_type_id);
    if (!strcmp(want, name))
        return 1;
    if (atoi(want) == (int)c->connector_id)
        return 1;
    return 0;
}

static int parse_mode(const char *spec, int *w, int *h)
{
    char sep;
    int n;

    if (!spec || !strcmp(spec, "auto"))
        return -1;
    n = sscanf(spec, "%dx%d%c", w, h, &sep);
    if (n != 2)
        return -1;
    return 0;
}

static void flip_handler(int fd, unsigned seq, unsigned sec, unsigned usec, void *data)
{
    struct um_display *d = data;

    (void)fd;
    (void)seq;
    (void)sec;
    (void)usec;
    d->flip_done = 1;
}

static int find_connector(struct um_display *d, const struct um_config *cfg, drmModeRes *res,
                          drmModeConnector **out)
{
    drmModeConnector *pick = NULL;

    for (int pass = 0; pass < 2; pass++) {
        for (int i = 0; i < res->count_connectors; i++) {
            drmModeConnector *c = drmModeGetConnector(d->fd, res->connectors[i]);

            if (!c)
                continue;
            if (c->connection != DRM_MODE_CONNECTED || c->count_modes == 0) {
                drmModeFreeConnector(c);
                continue;
            }
            if (pass == 0) {
                if (connector_wanted(cfg->connector, c) && !pick)
                    pick = c;
                else
                    drmModeFreeConnector(c);
            } else if (!pick) {
                pick = c;
            } else {
                drmModeFreeConnector(c);
            }
            if (pick && pass == 0)
                break;
        }
        if (pick)
            break;
    }

    *out = pick;
    return pick ? 0 : -1;
}

static int pick_mode(struct um_display *d, drmModeConnector *c, const struct um_config *cfg)
{
    int want_w, want_h;

    if (parse_mode(cfg->mode, &want_w, &want_h) == 0) {
        for (int i = 0; i < c->count_modes; i++) {
            if (c->modes[i].hdisplay == want_w && c->modes[i].vdisplay == want_h) {
                d->mode = c->modes[i];
                return 0;
            }
        }
        um_warn("mode %s not found, using preferred %dx%d", cfg->mode, c->modes[0].hdisplay,
                c->modes[0].vdisplay);
    }
    d->mode = c->modes[0];
    return 0;
}

/*
 * Pick a CRTC for the connector.  A freshly re-opened panel often has
 * encoder_id == 0 (the previous client disabled the CRTC), so walking
 * res->crtcs[0] may land on VP0 while DSI is routed to VP1.  Prefer an
 * already bound encoder, then an active CRTC, then any CRTC that the
 * connector's encoders claim to support.
 */
static uint32_t find_crtc(struct um_display *d, drmModeRes *res, drmModeConnector *c)
{
    uint32_t crtc = 0;
    drmModeEncoder *enc;

    enc = drmModeGetEncoder(d->fd, c->encoder_id);
    if (enc) {
        crtc = enc->crtc_id;
        drmModeFreeEncoder(enc);
    }

    if (!crtc) {
        for (int e = 0; e < c->count_encoders && !crtc; e++) {
            enc = drmModeGetEncoder(d->fd, c->encoders[e]);
            if (!enc)
                continue;
            crtc = enc->crtc_id;
            drmModeFreeEncoder(enc);
        }
    }

    if (!crtc) {
        for (int i = 0; i < res->count_crtcs; i++) {
            drmModeCrtc *cc = drmModeGetCrtc(d->fd, res->crtcs[i]);

            if (cc) {
                if (cc->mode_valid)
                    crtc = res->crtcs[i];
                drmModeFreeCrtc(cc);
            }
            if (crtc)
                break;
        }
    }

    if (!crtc) {
        for (int e = 0; e < c->count_encoders && !crtc; e++) {
            enc = drmModeGetEncoder(d->fd, c->encoders[e]);
            if (!enc)
                continue;
            for (int i = 0; i < res->count_crtcs; i++) {
                if (enc->possible_crtcs & (1u << i)) {
                    crtc = res->crtcs[i];
                    break;
                }
            }
            drmModeFreeEncoder(enc);
        }
    }

    if (!crtc && res->count_crtcs > 0)
        crtc = res->crtcs[0];
    return crtc;
}

/* Try the preferred CRTC first, then every other one (VOP routing differs). */
static int set_crtc_any(struct um_display *d, drmModeRes *res, uint32_t preferred)
{
    uint32_t tried[16];
    int ntried = 0;
    int ret = -1;

    if (preferred)
        tried[ntried++] = preferred;
    for (int i = 0; i < res->count_crtcs && ntried < (int)(sizeof(tried) / sizeof(tried[0])); i++) {
        if (res->crtcs[i] != preferred)
            tried[ntried++] = res->crtcs[i];
    }

    for (int i = 0; i < ntried; i++) {
        if (drmModeSetCrtc(d->fd, tried[i], d->buf[0].fb, 0, 0, &d->conn, 1, &d->mode) == 0) {
            if (i > 0)
                um_warn("CRTC %u rejected, using %u instead", preferred, tried[i]);
            d->crtc = tried[i];
            return 0;
        }
    }
    return ret;
}

static void destroy_buffers(struct um_display *d)
{
    for (int i = 0; i < 3; i++) {
        if (d->buf[i].dma_fd >= 0)
            close(d->buf[i].dma_fd);
        if (d->buf[i].fb)
            drmModeRmFB(d->fd, d->buf[i].fb);
        if (d->buf[i].map && d->buf[i].map != MAP_FAILED)
            munmap(d->buf[i].map, d->buf[i].size);
        if (d->buf[i].handle) {
            struct drm_mode_destroy_dumb arg = {.handle = d->buf[i].handle};

            ioctl(d->fd, DRM_IOCTL_MODE_DESTROY_DUMB, &arg);
        }
        memset(&d->buf[i], 0, sizeof(d->buf[i]));
    }
}

static int create_buffers(struct um_display *d)
{
    for (int i = 0; i < 3; i++) {
        struct drm_mode_create_dumb create = {0};
        struct drm_mode_map_dumb map = {0};

        create.width = (uint32_t)d->w;
        create.height = (uint32_t)d->h;
        create.bpp = 32;

        if (ioctl(d->fd, DRM_IOCTL_MODE_CREATE_DUMB, &create) < 0) {
            um_err("CREATE_DUMB %dx%d failed: %s (CMA too small? try cma=128M)",
                   d->w, d->h, strerror(errno));
            return -1;
        }
        d->buf[i].handle = create.handle;
        d->buf[i].pitch = create.pitch;
        d->buf[i].size = create.size;

        if (drmModeAddFB(d->fd, (uint32_t)d->w, (uint32_t)d->h, 24, 32, create.pitch,
                         create.handle, &d->buf[i].fb) < 0) {
            um_err("AddFB failed: %s", strerror(errno));
            return -1;
        }

        map.handle = create.handle;
        if (ioctl(d->fd, DRM_IOCTL_MODE_MAP_DUMB, &map) < 0) {
            um_err("MAP_DUMB failed: %s", strerror(errno));
            return -1;
        }
        d->buf[i].map = mmap(NULL, create.size, PROT_READ | PROT_WRITE, MAP_SHARED, d->fd,
                             (off_t)map.offset);
        if (d->buf[i].map == MAP_FAILED) {
            um_err("mmap dumb buffer failed: %s", strerror(errno));
            return -1;
        }
        memset(d->buf[i].map, 0, create.size);
#ifdef UM_USE_RGA
        if (drmPrimeHandleToFD(d->fd, create.handle, DRM_CLOEXEC | DRM_RDWR, &d->buf[i].dma_fd) < 0)
            d->buf[i].dma_fd = -1;
#endif
    }
    return 0;
}

struct um_display *um_display_open(const struct um_config *cfg, int *panel_w, int *panel_h)
{
    struct um_display *d = calloc(1, sizeof(*d));
    drmModeRes *res = NULL;
    drmModeConnector *conn = NULL;
    const char *cards[2] = {cfg->card, "/dev/dri/card1"};

    if (!d)
        return NULL;
    d->fd = -1;
    d->buf[0].dma_fd = d->buf[1].dma_fd = d->buf[2].dma_fd = -1;
    d->render = 1;
    d->pending = -1;

    for (int i = 0; i < 2 && d->fd < 0; i++) {
        d->fd = open(cards[i], O_RDWR | O_CLOEXEC);
        if (d->fd >= 0)
            um_info("using DRM device %s", cards[i]);
    }
    if (d->fd < 0) {
        um_err("cannot open DRM device: %s", strerror(errno));
        free(d);
        return NULL;
    }

    if (drmSetMaster(d->fd) < 0)
        um_warn("drmSetMaster failed: %s (another DRM master running?)", strerror(errno));

    res = drmModeGetResources(d->fd);
    if (!res) {
        um_err("drmModeGetResources failed: %s", strerror(errno));
        goto fail;
    }

    if (find_connector(d, cfg, res, &conn) < 0) {
        um_err("no connected DSI connector found (is the panel driver bound?)");
        goto fail;
    }

    d->conn = conn->connector_id;
    pick_mode(d, conn, cfg);
    d->crtc = find_crtc(d, res, conn);
    if (!d->crtc) {
        um_err("no CRTC for connector %u", conn->connector_id);
        goto fail;
    }

    d->w = d->mode.hdisplay;
    d->h = d->mode.vdisplay;
    d->use_flip = 1;

    um_info("connector %s-%u mode %dx%d@%d crtc %u", conn_type_name(conn->connector_type),
            conn->connector_type_id, d->w, d->h, d->mode.vrefresh, d->crtc);

    if (create_buffers(d) < 0)
        goto fail;

    d->saved_crtc = drmModeGetCrtc(d->fd, d->crtc);
    d->saved_valid = d->saved_crtc != NULL;

    if (set_crtc_any(d, res, d->crtc) < 0) {
        um_err("SetCrtc failed on every CRTC: %s", strerror(errno));
        goto fail;
    }

    drmModeFreeConnector(conn);
    drmModeFreeResources(res);

#ifdef UM_USE_RGA
    if (d->buf[0].dma_fd >= 0 && d->buf[1].dma_fd >= 0 && d->buf[2].dma_fd >= 0 && c_RkRgaInit() == 0) {
        d->rga_ready = d->rga_initialized = 1;
        um_info("RGA hardware composition enabled");
    }
#endif
    *panel_w = d->w;
    *panel_h = d->h;
    return d;

fail:
    if (conn)
        drmModeFreeConnector(conn);
    if (res)
        drmModeFreeResources(res);
    um_display_close(d);
    return NULL;
}

uint8_t *um_display_buffer(struct um_display *d, int *pitch)
{
    if (!d)
        return NULL;
    *pitch = (int)d->buf[d->render].pitch;
    return d->buf[d->render].map;
}

int um_display_compose_dma(struct um_display *d, int fd, int stride, int vstride,
                          int format, const struct um_geom *g)
{
#ifdef UM_USE_RGA
    if (d->rga_ready && fd >= 0 && (format == 0 || format == 2)) {
        struct dbuf *b = &d->buf[d->render];
        rga_info_t source = {0}, dest = {0};
        source.fd = fd;
        source.mmuFlag = 1;
        source.rotation = g->rot == 90 ? HAL_TRANSFORM_ROT_90 :
                          g->rot == 180 ? HAL_TRANSFORM_ROT_180 :
                          g->rot == 270 ? HAL_TRANSFORM_ROT_270 : 0;
        rga_set_rect(&source.rect, 0, 0, g->sw, g->sh, stride, vstride,
                     format == 0 ? RK_FORMAT_YCbCr_420_SP : RK_FORMAT_YCbCr_422_SP);
        dest.fd = b->dma_fd;
        dest.mmuFlag = 1;
        dest.color_space_mode = yuv2rgb_mode1; /* JPEG full-range BT.601 */
        rga_set_rect(&dest.rect, g->dx, g->dy, g->dw, g->dh,
                     b->pitch / 4, d->h, RK_FORMAT_BGRA_8888);
        return c_RkRgaBlit(&source, &dest, NULL);
    }
#else
    (void)d; (void)fd; (void)stride; (void)vstride; (void)format; (void)g;
#endif
    return -1;
}

int um_display_compose(struct um_display *d, const uint8_t *src,
                       int sw, int sh, int sp, const struct um_geom *g)
{
    struct dbuf *b = &d->buf[d->render];
#ifdef UM_USE_RGA
    if (d->rga_ready) {
        rga_info_t source = {0}, dest = {0};
        source.fd = -1;
        source.virAddr = (void *)src;
        source.mmuFlag = 1;
        source.rotation = g->rot == 90 ? HAL_TRANSFORM_ROT_90 :
                          g->rot == 180 ? HAL_TRANSFORM_ROT_180 :
                          g->rot == 270 ? HAL_TRANSFORM_ROT_270 : 0;
        rga_set_rect(&source.rect, 0, 0, g->sw, g->sh, sp / 4, sh, RK_FORMAT_BGRA_8888);
        dest.fd = b->dma_fd;
        dest.mmuFlag = 1;
        rga_set_rect(&dest.rect, g->dx, g->dy, g->dw, g->dh,
                     b->pitch / 4, d->h, RK_FORMAT_BGRA_8888);
        if (c_RkRgaBlit(&source, &dest, NULL) == 0)
            return 0;
        um_warn("RGA composition failed; using CPU fallback");
        d->rga_ready = 0;
    }
#endif
    um_compose(b->map, b->pitch, d->w, d->h, src, sw, sh, sp, g);
    return 0;
}

int um_display_flip(struct um_display *d)
{
    if (!d) return -1;
    int target = d->render;
    if (d->use_flip) {
        drmEventContext ev = {
            .version = DRM_EVENT_CONTEXT_VERSION,
            .page_flip_handler = flip_handler,
        };
        struct pollfd pfd = {.fd = d->fd, .events = POLLIN};
        /* Three distinct buffers: scanning, pending, and being composed.
         * Decode/compose may overlap the previous vblank, but never overwrite
         * either a scanning or queued framebuffer. */
        if (d->pending >= 0) {
            for (int waited = 0; waited < 100 && !d->flip_done; waited += 5) {
                if (poll(&pfd, 1, 5) > 0)
                    drmHandleEvent(d->fd, &ev);
            }
            if (!d->flip_done) {
                um_warn("page flip completion timeout; retaining buffer ownership");
                return -1;
            }
            d->front = d->pending;
            d->pending = -1;
        }
        d->flip_done = 0;
        if (drmModePageFlip(d->fd, d->crtc, d->buf[target].fb,
                            DRM_MODE_PAGE_FLIP_EVENT, d) == 0) {
            d->pending = target;
            for (int i = 0; i < 3; i++)
                if (i != d->front && i != d->pending) { d->render = i; break; }
            return 0;
        }
        um_warn("page flip failed (%s), falling back to SetCrtc", strerror(errno));
        d->use_flip = 0;
    }
    if (drmModeSetCrtc(d->fd, d->crtc, d->buf[target].fb, 0, 0, &d->conn, 1, &d->mode) < 0)
        return -1;
    d->front = target;
    d->render = (target + 1) % 3;
    return 0;
}

void um_display_close(struct um_display *d)
{
    if (!d)
        return;
    if (d->fd >= 0) {
        /*
         * Hand the CRTC back before destroying our buffers, otherwise the VOP
         * keeps scanning a dead framebuffer and floods dmesg with
         * POST_BUF_EMPTY errors.
         */
        if (d->saved_valid && d->saved_crtc->buffer_id) {
            drmModeSetCrtc(d->fd, d->crtc, d->saved_crtc->buffer_id, d->saved_crtc->x,
                           d->saved_crtc->y, &d->conn, 1, &d->saved_crtc->mode);
        } else {
            drmModeSetCrtc(d->fd, d->crtc, 0, 0, 0, NULL, 0, NULL);
        }
        if (d->saved_crtc)
            drmModeFreeCrtc(d->saved_crtc);
        destroy_buffers(d);
        drmDropMaster(d->fd);
        close(d->fd);
    }
#ifdef UM_USE_RGA
    if (d->rga_initialized) c_RkRgaDeInit();
#endif
    free(d);
}

int um_display_set_brightness(int value)
{
    DIR *dir;
    struct dirent *de;
    int done = 0;

    if (value < 0)
        return 0;

    dir = opendir("/sys/class/backlight");
    if (!dir)
        return -1;

    while ((de = readdir(dir)) != NULL) {
        char path[256];
        char buf[32];
        int fd;
        long max = 255;
        long level;

        if (de->d_name[0] == '.')
            continue;
        snprintf(path, sizeof(path), "/sys/class/backlight/%s/max_brightness", de->d_name);
        fd = open(path, O_RDONLY | O_CLOEXEC);
        if (fd >= 0) {
            ssize_t n = read(fd, buf, sizeof(buf) - 1);

            close(fd);
            if (n > 0) {
                buf[n] = '\0';
                max = strtol(buf, NULL, 10);
                if (max <= 0)
                    max = 255;
            }
        }
        level = (long)value * max / 255;
        if (level < 0)
            level = 0;
        snprintf(path, sizeof(path), "/sys/class/backlight/%s/brightness", de->d_name);
        fd = open(path, O_WRONLY | O_CLOEXEC);
        if (fd >= 0) {
            char out[32];
            int len = snprintf(out, sizeof(out), "%ld", level);

            if (write(fd, out, (size_t)len) == len)
                done++;
            close(fd);
        }
    }
    closedir(dir);
    return done ? 0 : -1;
}
