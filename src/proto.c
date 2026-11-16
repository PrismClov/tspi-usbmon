/*
 * udisp frame protocol parser (16-byte header + payload).
 *
 * Header layout (little endian), identical to the ESP32-P4 reference firmware:
 *   0..1   crc16      (unused by both host drivers, kept for compatibility)
 *   2      type       3 = JPEG, 0xff = end marker
 *   3      cmd
 *   4..5   x
 *   6..7   y
 *   8..9   width
 *   10..11 height
 *   12..15 (frame_id & 0x3ff) | (payload_length << 10)
 *
 * SPDX-License-Identifier: MIT
 */
#include "usbmon.h"

#include <string.h>

static uint16_t le16(const uint8_t *p)
{
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

bool um_hdr_parse(const uint8_t raw[UM_HDR_SIZE], uint32_t limit, struct um_frame_hdr *out)
{
    struct um_frame_hdr h;
    uint32_t packed;

    if (!raw)
        return false;

    h.type = raw[2];
    h.cmd = raw[3];
    h.x = le16(raw + 4);
    h.y = le16(raw + 6);
    h.width = le16(raw + 8);
    h.height = le16(raw + 10);
    packed = le32(raw + 12);
    h.id = (uint16_t)(packed & 0x3ffu);
    h.payload = packed >> 10;

    if (h.type == UM_TYPE_END) {
        if (h.payload != 0)
            return false;
        if (out)
            *out = h;
        return true;
    }

    if (h.type != UM_TYPE_JPEG)
        return false;
    if (h.width < 8 || h.height < 8 || h.width > UM_MAX_DIM || h.height > UM_MAX_DIM)
        return false;
    if (h.x >= h.width || h.y >= h.height)
        return false;
    if (h.payload < 128 || h.payload > limit)
        return false;

    if (out)
        *out = h;
    return true;
}

void um_proto_init(struct um_proto *p, uint32_t limit)
{
    memset(p, 0, sizeof(*p));
    p->limit = limit;
}

void um_proto_reset(struct um_proto *p)
{
    p->have = 0;
}

enum um_proto_result um_proto_scan(struct um_proto *p, const uint8_t *data, size_t len,
                                   size_t *used, struct um_frame_hdr *out)
{
    *used = 0;

    while (*used < len) {
        size_t need = UM_HDR_SIZE - p->have;
        size_t avail = len - *used;
        size_t n = need < avail ? need : avail;

        memcpy(p->hdr + p->have, data + *used, n);
        p->have += n;
        *used += n;

        if (p->have < UM_HDR_SIZE)
            return UM_PROTO_NEED_MORE;

        if (um_hdr_parse(p->hdr, p->limit, out)) {
            p->have = 0;
            p->frames++;
            return UM_PROTO_HEADER;
        }

        /*
         * Invalid header: drop one byte and try again. This recovers from a
         * host that was unplugged in the middle of a frame or from a stray
         * short packet without losing the next valid header.
         */
        memmove(p->hdr, p->hdr + 1, UM_HDR_SIZE - 1);
        p->have = UM_HDR_SIZE - 1;
        p->resyncs++;
        return UM_PROTO_RESYNC;
    }

    return UM_PROTO_NEED_MORE;
}
