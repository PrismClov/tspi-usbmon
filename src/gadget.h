/*
 * SPDX-License-Identifier: MIT
 */
#ifndef UM_GADGET_H
#define UM_GADGET_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "usbmon.h"

struct um_frame {
    uint8_t *data;
    size_t len;
    uint16_t width;  /* logical stream size from the frame header */
    uint16_t height;
    uint16_t id;
    int slot;
};

int um_gadget_start(const struct um_config *cfg);
void um_gadget_stop(void);
bool um_gadget_enabled(void);

/* Blocks up to timeout_ms for a frame. Older queued frames are dropped. */
struct um_frame *um_gadget_wait_frame(int timeout_ms);
void um_gadget_release_frame(struct um_frame *f);

/* Queue a HID report (52 bytes). Returns false when USB is not ready. */
bool um_gadget_touch(const uint8_t *report, size_t len);

#endif
