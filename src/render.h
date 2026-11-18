/* SPDX-License-Identifier: MIT */
#ifndef UM_RENDER_H
#define UM_RENDER_H
#include <stddef.h>
#include <stdint.h>
struct um_display;
struct um_decoder;
struct um_geom;
struct um_renderer;
struct um_render_result {
    uint64_t jpeg_us, compose_us, flip_us;
    uint64_t start_us, end_us;
    size_t bytes;
    int ok;
};
/* One outstanding job. Decoder belongs to renderer until finish returns. */
struct um_renderer *um_renderer_new(struct um_display *, const struct um_geom *, int threaded);
int um_renderer_submit(struct um_renderer *, struct um_decoder *, size_t bytes, uint64_t jpeg_us);
int um_renderer_finish(struct um_renderer *, struct um_render_result *);
void um_renderer_free(struct um_renderer *);
#endif
