/*
 * JPEG decoding through libjpeg-turbo (turbojpeg API).
 * SPDX-License-Identifier: MIT
 */
#include "jpegdec.h"

#include <stdlib.h>
#include <string.h>
#include <turbojpeg.h>

#include "usbmon.h"

struct um_decoder {
    tjhandle tj;
    uint8_t *buf;
    size_t cap;
    int w, h;
    int pitch;
};

struct um_decoder *um_decoder_new(void)
{
    struct um_decoder *dec = calloc(1, sizeof(*dec));

    if (!dec)
        return NULL;
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

    need = (size_t)w * 4u * (size_t)h;

    if (need > dec->cap) {
        uint8_t *p = realloc(dec->buf, need);

        if (!p)
            return -1;
        dec->buf = p;
        dec->cap = need;
    }

    if (tjDecompress2(dec->tj, jpg, (unsigned long)len, dec->buf, w, w * 4, h,
                      TJPF_BGRX, TJFLAG_FASTDCT) < 0) {
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
