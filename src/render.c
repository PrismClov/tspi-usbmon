/* SPDX-License-Identifier: MIT */
#include "render.h"
#include "display.h"
#include "jpegdec.h"
#include "usbmon.h"
#include <pthread.h>
#include <stdlib.h>
#include <time.h>

struct um_renderer {
    struct um_display *display;
    const struct um_geom *geom;
    struct um_decoder *decoder;
    struct um_render_result result;
    pthread_t thread;
    pthread_mutex_t lock;
    pthread_cond_t cond;
    int threaded, state, stop; /* 0 idle, 1 submitted, 2 complete */
};

static uint64_t clock_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}

static void render(struct um_renderer *r)
{
    struct um_decoder *d = r->decoder;
    uint64_t start = clock_us();
    r->result.start_us = start;
    int fd = um_decoder_dma_fd(d), status;
    if (fd >= 0) {
        status = um_display_compose_dma(r->display, fd, um_decoder_pitch(d),
                    um_decoder_vertical_stride(d), um_decoder_format(d), r->geom);
        if (status < 0) {
            /* Compressed input has already been released. Retry future frames
             * in software; never scan an uncomposed or recycled DMA buffer. */
            um_warn("DMA composition failed; next frame uses software JPEG");
            um_decoder_enable_hardware(d, 0);
        }
    } else {
        status = um_display_compose(r->display, um_decoder_data(d),
                    um_decoder_width(d), um_decoder_height(d),
                    um_decoder_pitch(d), r->geom);
    }
    uint64_t composed = clock_us();
    r->result.compose_us = composed - start;
    r->result.ok = status == 0 && um_display_flip(r->display) == 0;
    r->result.flip_us = clock_us() - composed;
    r->result.end_us = clock_us();
}

static void *render_thread(void *arg)
{
    struct um_renderer *r = arg;
    pthread_mutex_lock(&r->lock);
    for (;;) {
        while (r->state != 1 && !r->stop)
            pthread_cond_wait(&r->cond, &r->lock);
        if (r->stop) break;
        pthread_mutex_unlock(&r->lock);
        render(r);
        pthread_mutex_lock(&r->lock);
        r->state = 2;
        pthread_cond_broadcast(&r->cond);
    }
    pthread_mutex_unlock(&r->lock);
    return NULL;
}

struct um_renderer *um_renderer_new(struct um_display *d, const struct um_geom *g, int threaded)
{
    struct um_renderer *r = calloc(1, sizeof(*r));
    if (!r) return NULL;
    r->display = d;
    r->geom = g;
    if (pthread_mutex_init(&r->lock, NULL)) { free(r); return NULL; }
    if (pthread_cond_init(&r->cond, NULL)) {
        pthread_mutex_destroy(&r->lock); free(r); return NULL;
    }
    if (threaded && pthread_create(&r->thread, NULL, render_thread, r) == 0)
        r->threaded = 1;
    else if (threaded)
        um_warn("render thread unavailable; using serial rendering");
    um_info("render pipeline: %s", r->threaded ? "parallel (two decoder buffers)" : "serial");
    return r;
}

int um_renderer_submit(struct um_renderer *r, struct um_decoder *d, size_t bytes, uint64_t jpeg_us)
{
    pthread_mutex_lock(&r->lock);
    if (r->state || r->stop) { pthread_mutex_unlock(&r->lock); return -1; }
    r->decoder = d;
    r->result = (struct um_render_result){.bytes = bytes, .jpeg_us = jpeg_us};
    r->state = 1;
    if (r->threaded) pthread_cond_broadcast(&r->cond);
    else { render(r); r->state = 2; }
    pthread_mutex_unlock(&r->lock);
    return 0;
}

int um_renderer_finish(struct um_renderer *r, struct um_render_result *result)
{
    pthread_mutex_lock(&r->lock);
    while (r->state == 1) pthread_cond_wait(&r->cond, &r->lock);
    int completed = r->state == 2;
    if (completed) {
        if (result) *result = r->result;
        r->state = 0;
    }
    pthread_mutex_unlock(&r->lock);
    return completed;
}

void um_renderer_free(struct um_renderer *r)
{
    if (!r) return;
    um_renderer_finish(r, NULL);
    pthread_mutex_lock(&r->lock);
    r->stop = 1;
    pthread_cond_broadcast(&r->cond);
    pthread_mutex_unlock(&r->lock);
    if (r->threaded) pthread_join(r->thread, NULL);
    pthread_cond_destroy(&r->cond);
    pthread_mutex_destroy(&r->lock);
    free(r);
}
