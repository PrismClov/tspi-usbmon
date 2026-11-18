/*
 * JPEG decoding through libjpeg-turbo (turbojpeg API).
 * SPDX-License-Identifier: MIT
 */
#include "jpegdec.h"

#include <stdlib.h>
#include <string.h>
#include <turbojpeg.h>

#include "usbmon.h"
#ifdef UM_USE_MPP
#define MODULE_TAG "usbmon-jpeg"
#include <rk_mpi.h>
#include <mpp_buffer.h>
#endif

struct um_decoder {
    tjhandle tj;
    uint8_t *buf;
    size_t cap;
    int w, h;
    int pitch;
    int hardware, dma_fd, vstride, format;
#ifdef UM_USE_MPP
    MppCtx ctx;
    MppApi *api;
    MppBufferGroup group;
    MppBuffer input, output;
    MppFrame frame;
    MppPacket packet;
    size_t input_cap;
    int hw_w, hw_h;
#endif
};

#ifdef UM_USE_MPP
static void hardware_close(struct um_decoder *d)
{
    if (d->ctx) {
        d->api->reset(d->ctx);
        mpp_destroy(d->ctx);
        d->ctx = NULL;
    }
    if (d->packet) mpp_packet_deinit(&d->packet);
    if (d->frame) mpp_frame_deinit(&d->frame);
    if (d->input) { mpp_buffer_put(d->input); d->input = NULL; }
    if (d->output) { mpp_buffer_put(d->output); d->output = NULL; }
    if (d->group) { mpp_buffer_group_put(d->group); d->group = NULL; }
    d->dma_fd = -1;
}

static int hardware_decode(struct um_decoder *d, const uint8_t *jpg, size_t len, int w, int h)
{
    MppTask task = NULL;
    MppFrame result = NULL;
    int stride = (w + 15) & ~15, vstride = (h + 15) & ~15;
    if (d->ctx && (d->hw_w != w || d->hw_h != h || len > d->input_cap))
        hardware_close(d);
    if (!d->ctx) {
        d->input_cap = len > 4 * 1024 * 1024 ? len : 4 * 1024 * 1024;
        if (mpp_create(&d->ctx, &d->api) ||
            mpp_init(d->ctx, MPP_CTX_DEC, MPP_VIDEO_CodingMJPEG) ||
            mpp_buffer_group_get_internal(&d->group, MPP_BUFFER_TYPE_DRM) ||
            mpp_buffer_get(d->group, &d->input, d->input_cap) ||
            mpp_buffer_get(d->group, &d->output, (size_t)stride * vstride * 4) ||
            mpp_frame_init(&d->frame) ||
            mpp_packet_init_with_buffer(&d->packet, d->input))
            goto fail;
        mpp_frame_set_buffer(d->frame, d->output);
        d->hw_w = w; d->hw_h = h;
        um_info("MPP JPEG hardware decoder %dx%d enabled", w, h);
    }
    if (d->api->poll(d->ctx, MPP_PORT_INPUT, (MppPollType)500) ||
        d->api->dequeue(d->ctx, MPP_PORT_INPUT, &task) || !task)
        goto fail;
    memcpy(mpp_buffer_get_ptr(d->input), jpg, len);
    mpp_packet_set_pos(d->packet, mpp_buffer_get_ptr(d->input));
    mpp_packet_set_length(d->packet, len);
    mpp_packet_clr_eos(d->packet);
    mpp_task_meta_set_packet(task, KEY_INPUT_PACKET, d->packet);
    mpp_task_meta_set_frame(task, KEY_OUTPUT_FRAME, d->frame);
    if (d->api->enqueue(d->ctx, MPP_PORT_INPUT, task) ||
        d->api->poll(d->ctx, MPP_PORT_OUTPUT, (MppPollType)500) ||
        d->api->dequeue(d->ctx, MPP_PORT_OUTPUT, &task) || !task)
        goto fail;
    mpp_task_meta_get_frame(task, KEY_OUTPUT_FRAME, &result);
    int bad = !result || mpp_frame_get_errinfo(result) || mpp_frame_get_discard(result);
    if (!bad) {
        d->w = mpp_frame_get_width(result);
        d->h = mpp_frame_get_height(result);
        d->pitch = mpp_frame_get_hor_stride(result);
        d->vstride = mpp_frame_get_ver_stride(result);
        d->format = mpp_frame_get_fmt(result);
        d->dma_fd = mpp_buffer_get_fd(mpp_frame_get_buffer(result));
        bad = d->w != w || d->h != h ||
              (d->format != MPP_FMT_YUV420SP && d->format != MPP_FMT_YUV422SP);
    }
    if (d->api->enqueue(d->ctx, MPP_PORT_OUTPUT, task) || bad)
        goto fail;
    return 0;
fail:
    hardware_close(d);
    return -1;
}
#endif

void um_decoder_enable_hardware(struct um_decoder *d, int enable)
{
    d->hardware = enable;
#ifdef UM_USE_MPP
    if (!enable) hardware_close(d);
#endif
}

int um_decoder_dma_fd(const struct um_decoder *d) { return d ? d->dma_fd : -1; }
int um_decoder_vertical_stride(const struct um_decoder *d) { return d ? d->vstride : 0; }
int um_decoder_format(const struct um_decoder *d) { return d ? d->format : 0; }

struct um_decoder *um_decoder_new(void)
{
    struct um_decoder *dec = calloc(1, sizeof(*dec));

    if (!dec)
        return NULL;
    dec->dma_fd = -1;
    dec->tj = tjInitDecompress();
    if (!dec->tj) {
        free(dec);
        return NULL;
    }
    return dec;
}

void um_decoder_free(struct um_decoder *dec)
{
    if (!dec)
        return;
#ifdef UM_USE_MPP
    hardware_close(dec);
#endif
    if (dec->tj)
        tjDestroy(dec->tj);
    free(dec->buf);
    free(dec);
}

int um_decoder_decode(struct um_decoder *dec, const uint8_t *jpg, size_t len)
{
    int w = 0, h = 0, subsamp = 0, colorspace = 0;
    size_t need;

    if (!dec || !jpg || len < 4)
        return -1;

    if (tjDecompressHeader3(dec->tj, jpg, (unsigned long)len, &w, &h, &subsamp,
                            &colorspace) < 0) {
        um_dbg("jpeg header: %s", tjGetErrorStr2(dec->tj));
        return -1;
    }
    if (w <= 0 || h <= 0 || w > UM_MAX_DIM || h > UM_MAX_DIM) {
        um_warn("jpeg dimensions %dx%d rejected", w, h);
        return -1;
    }

#ifdef UM_USE_MPP
    if (dec->hardware) {
        if (hardware_decode(dec, jpg, len, w, h) == 0)
            return 0;
        um_warn("MPP JPEG failed; falling back to software decoding");
        dec->hardware = 0;
    }
#endif

    need = (size_t)w * 4u * (size_t)h;

    if (need > dec->cap) {
        uint8_t *p = realloc(dec->buf, need);

        if (!p)
            return -1;
        dec->buf = p;
        dec->cap = need;
    }

    if (tjDecompress2(dec->tj, jpg, (unsigned long)len, dec->buf, w, w * 4, h,
                      TJPF_BGRX, 0) < 0) {
        um_dbg("jpeg decode: %s", tjGetErrorStr2(dec->tj));
        return -1;
    }

    dec->w = w;
    dec->h = h;
    dec->pitch = w * 4;
    return 0;
}

const uint8_t *um_decoder_data(const struct um_decoder *dec)
{
    return dec ? dec->buf : NULL;
}

int um_decoder_width(const struct um_decoder *dec)
{
    return dec ? dec->w : 0;
}

int um_decoder_height(const struct um_decoder *dec)
{
    return dec ? dec->h : 0;
}

int um_decoder_pitch(const struct um_decoder *dec)
{
    return dec ? dec->pitch : 0;
}
