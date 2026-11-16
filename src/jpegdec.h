/*
 * JPEG decoding through libjpeg-turbo (turbojpeg API).
 * SPDX-License-Identifier: MIT
 */
#ifndef UM_JPEGDEC_H
#define UM_JPEGDEC_H

#include <stddef.h>
#include <stdint.h>

struct um_decoder;

struct um_decoder *um_decoder_new(void);
void um_decoder_free(struct um_decoder *dec);

/* Decodes into an internal BGRX buffer. Returns 0 on success. */
int um_decoder_decode(struct um_decoder *dec, const uint8_t *jpg, size_t len);

const uint8_t *um_decoder_data(const struct um_decoder *dec);
int um_decoder_width(const struct um_decoder *dec);
int um_decoder_height(const struct um_decoder *dec);
int um_decoder_pitch(const struct um_decoder *dec);

#endif
