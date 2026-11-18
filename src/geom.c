/*
 * Frame geometry: stream (host framebuffer) -> rotation -> panel letterbox,
 * plus the inverse mapping used for touch coordinates.
 * SPDX-License-Identifier: MIT
 */
#include "usbmon.h"

#include <stdlib.h>
#include <string.h>

static void inv_rotate(const struct um_geom *g, int rx, int ry, int *sx, int *sy)
{
    switch (g->rot) {
    case 90:
        *sx = ry;
        *sy = g->sh - 1 - rx;
        break;
    case 180:
        *sx = g->sw - 1 - rx;
        *sy = g->sh - 1 - ry;
        break;
    case 270:
        *sx = g->sw - 1 - ry;
        *sy = rx;
        break;
    default:
        *sx = rx;
        *sy = ry;
        break;
    }
}

int um_geom_init(struct um_geom *g, int sw, int sh, int rot, int pw, int ph)
{
    return um_geom_init_mode(g, sw, sh, rot, pw, ph, 0);
}

int um_geom_init_mode(struct um_geom *g, int sw, int sh, int rot, int pw, int ph, int stretch)
{
    memset(g, 0, sizeof(*g));

    if (sw <= 0 || sh <= 0 || pw <= 0 || ph <= 0)
        return -1;
    if (rot != 0 && rot != 90 && rot != 180 && rot != 270)
        return -1;

    g->sw = sw;
    g->sh = sh;
    g->rot = rot;
    g->pw = pw;
    g->ph = ph;

    if (rot == 90 || rot == 270) {
        g->rw = sh;
        g->rh = sw;
    } else {
        g->rw = sw;
        g->rh = sh;
    }

    /* Aspect-preserving fit. */
    if (stretch) {
        g->dw = pw;
        g->dh = ph;
    } else if ((int64_t)pw * g->rh <= (int64_t)ph * g->rw) {
        g->dw = pw;
        g->dh = (int)((int64_t)pw * g->rh / g->rw);
    } else {
        g->dh = ph;
        g->dw = (int)((int64_t)ph * g->rw / g->rh);
    }
    if (g->dw <= 0 || g->dh <= 0)
        return -1;
    if (g->dw > pw)
        g->dw = pw;
    if (g->dh > ph)
        g->dh = ph;
    g->dx = (pw - g->dw) / 2;
    g->dy = (ph - g->dh) / 2;

    g->xmap = malloc(sizeof(int) * (size_t)g->dw);
    g->ymap = malloc(sizeof(int) * (size_t)g->dh);
    if (!g->xmap || !g->ymap) {
        um_geom_fini(g);
        return -1;
    }
    for (int x = 0; x < g->dw; x++)
        g->xmap[x] = (int)((int64_t)x * g->rw / g->dw);
    for (int y = 0; y < g->dh; y++)
        g->ymap[y] = (int)((int64_t)y * g->rh / g->dh);

    return 0;
}

void um_geom_fini(struct um_geom *g)
{
    free(g->xmap);
    free(g->ymap);
    g->xmap = NULL;
    g->ymap = NULL;
}

void um_geom_panel_to_stream(const struct um_geom *g, int px, int py, int *sx, int *sy)
{
    int rx, ry;

    if (px < g->dx)
        px = g->dx;
    if (py < g->dy)
        py = g->dy;
    if (px >= g->dx + g->dw)
        px = g->dx + g->dw - 1;
    if (py >= g->dy + g->dh)
        py = g->dy + g->dh - 1;

    rx = (int)((int64_t)(px - g->dx) * g->rw / g->dw);
    ry = (int)((int64_t)(py - g->dy) * g->rh / g->dh);

    inv_rotate(g, rx, ry, sx, sy);

    if (*sx < 0)
        *sx = 0;
    if (*sy < 0)
        *sy = 0;
    if (*sx >= g->sw)
        *sx = g->sw - 1;
    if (*sy >= g->sh)
        *sy = g->sh - 1;
}

void um_compose(uint8_t *fb, int fb_pitch, int fb_w, int fb_h,
                const uint8_t *src, int src_w, int src_h, int src_pitch,
                const struct um_geom *g)
{
    const uint8_t *s = src;

    (void)fb_w;
    (void)fb_h;
    (void)src_h;

    if (!fb || !src || !g || !g->xmap || !g->ymap)
        return;
    if (src_pitch <= 0)
        src_pitch = src_w * 4;

    if (g->rot == 0) {
        for (int y = 0; y < g->dh; y++) {
            uint8_t *drow = fb + (size_t)(g->dy + y) * (size_t)fb_pitch +
                            (size_t)g->dx * 4;
            const uint8_t *srow = s + (size_t)g->ymap[y] * (size_t)src_pitch;

            if (g->dw == g->sw && src_w >= g->sw) {
                memcpy(drow, srow, (size_t)g->sw * 4);
            } else {
                uint32_t *d32 = (uint32_t *)drow;
                const uint32_t *s32 = (const uint32_t *)srow;

                for (int x = 0; x < g->dw; x++)
                    d32[x] = s32[g->xmap[x]];
            }
        }
        return;
    }

    /* Rotated (and possibly scaled) path: tile the destination for locality. */
    for (int ty = 0; ty < g->dh; ty += 32) {
        int yend = ty + 32 < g->dh ? ty + 32 : g->dh;

        for (int tx = 0; tx < g->dw; tx += 32) {
            int xend = tx + 32 < g->dw ? tx + 32 : g->dw;

            for (int y = ty; y < yend; y++) {
                uint32_t *drow = (uint32_t *)(fb + (size_t)(g->dy + y) *
                                                       (size_t)fb_pitch) + g->dx;
                int ry = g->ymap[y];

                for (int x = tx; x < xend; x++) {
                    int sx, sy;
                    uint32_t px = 0;

                    inv_rotate(g, g->xmap[x], ry, &sx, &sy);
                    if (sx >= 0 && sy >= 0 && sx < g->sw && sy < g->sh) {
                        const uint32_t *sp = (const uint32_t *)(s + (size_t)sy *
                                                                       (size_t)src_pitch);
                        px = sp[sx];
                    }
                    drow[x] = px;
                }
            }
        }
    }
}

void um_compose_fit(uint8_t *fb, int fb_pitch, int fb_w, int fb_h,
                    const uint8_t *src, int src_w, int src_h, int src_pitch)
{
    struct um_geom g;

    if (um_geom_init(&g, src_w, src_h, 0, fb_w, fb_h) < 0)
        return;
    um_compose(fb, fb_pitch, fb_w, fb_h, src, src_w, src_h, src_pitch, &g);
    um_geom_fini(&g);
}

void um_geom_host_dims(int sw, int sh, int host_rotate, int *hw, int *hh)
{
    if (host_rotate == 90 || host_rotate == 270) {
        *hw = sh;
        *hh = sw;
    } else {
        *hw = sw;
        *hh = sh;
    }
}

void um_geom_stream_to_host(int sw, int sh, int host_rotate, int sx, int sy,
                            int *hx, int *hy)
{
    switch (host_rotate) {
    case 90:
        *hx = sy;
        *hy = sw - 1 - sx;
        break;
    case 180:
        *hx = sw - 1 - sx;
        *hy = sh - 1 - sy;
        break;
    case 270:
        *hx = sh - 1 - sy;
        *hy = sx;
        break;
    default:
        *hx = sx;
        *hy = sy;
        break;
    }
}
