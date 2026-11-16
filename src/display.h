/*
 * DRM/KMS output on the MIPI-DSI panel.
 * SPDX-License-Identifier: MIT
 */
#ifndef UM_DISPLAY_H
#define UM_DISPLAY_H

#include <stdint.h>

#include "usbmon.h"

struct um_display;

struct um_display *um_display_open(const struct um_config *cfg, int *panel_w, int *panel_h);
/* Back buffer to compose into (XRGB8888). */
uint8_t *um_display_buffer(struct um_display *d, int *pitch);
/* Queue a page flip and wait for the previous one to complete. */
int um_display_flip(struct um_display *d);
void um_display_close(struct um_display *d);

/* 0..255, -1 leaves it untouched. Returns 0 on success. */
int um_display_set_brightness(int value);

#endif
