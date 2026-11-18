/* SPDX-License-Identifier: MIT
 * Mock render backend: verifies job ownership, completion, failures, teardown.
 */
#include "render.h"
#include "display.h"
#include "jpegdec.h"
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <time.h>

struct um_decoder { int fd, hardware; };
struct um_display { int flips, fail; };
static pthread_mutex_t gate = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t changed = PTHREAD_COND_INITIALIZER;
static int entered, allow;

int um_decoder_dma_fd(const struct um_decoder *d) { return d->fd; }
int um_decoder_pitch(const struct um_decoder *d) { (void)d; return 1920; }
int um_decoder_vertical_stride(const struct um_decoder *d) { (void)d; return 1080; }
int um_decoder_format(const struct um_decoder *d) { (void)d; return 0; }
int um_decoder_width(const struct um_decoder *d) { (void)d; return 1920; }
int um_decoder_height(const struct um_decoder *d) { (void)d; return 1080; }
const uint8_t *um_decoder_data(const struct um_decoder *d) { return (const uint8_t *)d; }
void um_decoder_enable_hardware(struct um_decoder *d, int on) { d->hardware = on; }
int um_display_compose_dma(struct um_display *d, int fd, int stride, int vs, int fmt,
                           const struct um_geom *g)
{
    (void)stride; (void)vs; (void)fmt; (void)g;
    assert(fd == 42);
    pthread_mutex_lock(&gate);
    entered = 1;
    pthread_cond_broadcast(&changed);
    while (!allow) pthread_cond_wait(&changed, &gate);
    pthread_mutex_unlock(&gate);
    return d->fail ? -1 : 0;
}
int um_display_compose(struct um_display *d, const uint8_t *s, int w, int h, int p,
                       const struct um_geom *g)
{ (void)d; (void)s; (void)w; (void)h; (void)p; (void)g; return 0; }
int um_display_flip(struct um_display *d) { d->flips++; return 0; }

int main(void)
{
    struct um_display display = {0};
    struct um_decoder decoder = {42, 1};
    struct um_geom geom = {0};
    struct um_render_result result;
    struct um_renderer *r = um_renderer_new(&display, &geom, 1);
    assert(r && !um_renderer_finish(r, &result));
    assert(!um_renderer_submit(r, &decoder, 123, 456));
    /* Submit returned while render is blocked: caller can decode next frame. */
    pthread_mutex_lock(&gate);
    while (!entered) {
        struct timespec deadline;
        clock_gettime(CLOCK_REALTIME, &deadline); deadline.tv_sec += 2;
        assert(!pthread_cond_timedwait(&changed, &gate, &deadline));
    }
    assert(um_renderer_submit(r, &decoder, 0, 0) == -1);
    allow = 1; pthread_cond_broadcast(&changed); pthread_mutex_unlock(&gate);
    assert(um_renderer_finish(r, &result) && result.ok);
    assert(result.bytes == 123 && result.jpeg_us == 456 && display.flips == 1);
    display.fail = 1;
    assert(!um_renderer_submit(r, &decoder, 1, 2));
    assert(um_renderer_finish(r, &result) && !result.ok);
    assert(!decoder.hardware && display.flips == 1);
    decoder.fd = -1;
    for (int i = 0; i < 1000; i++) {
        assert(!um_renderer_submit(r, &decoder, (size_t)i, (uint64_t)i));
        assert(um_renderer_finish(r, &result) && result.ok);
        assert(result.bytes == (size_t)i && result.jpeg_us == (uint64_t)i);
    }
    assert(!um_renderer_submit(r, &decoder, 1, 2));
    um_renderer_free(r); /* outstanding software job must finish before free */
    assert(display.flips == 1002);
    r = um_renderer_new(&display, &geom, 0);
    assert(r && !um_renderer_submit(r, &decoder, 1, 2));
    assert(um_renderer_finish(r, &result) && result.ok);
    um_renderer_free(r);
    puts("render ownership, overlap, fallback, shutdown and serial tests passed");
    return 0;
}
