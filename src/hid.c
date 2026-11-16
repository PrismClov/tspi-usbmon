/*
 * HID digitizer descriptor and multi-touch report.
 *
 * The descriptor is byte-identical to the ESP32-P4 reference firmware
 * (esp-iot-solution usb_extend_screen), because the Windows HID stack is
 * picky about the "Contact Count Maximum" feature report (id 2) and about
 * the 5-finger, 10-byte-per-contact report layout.
 *
 * SPDX-License-Identifier: MIT
 */
#include "usbmon.h"

#include <string.h>

size_t um_hid_descriptor(uint8_t *buf, size_t cap, uint16_t width, uint16_t height)
{
    static const uint8_t prefix[] = {0x05, 0x0d, 0x09, 0x04, 0xa1, 0x01, 0x85, 0x01};
    static const uint8_t finger_prefix[] = {
        0x09, 0x42, 0xa1, 0x02, 0x09, 0x42, 0x15, 0x00, 0x25, 0x01, 0x75,
        0x01, 0x95, 0x01, 0x81, 0x02, 0x95, 0x07, 0x81, 0x01, 0x75, 0x08,
        0x09, 0x51, 0x95, 0x01, 0x81, 0x02, 0x05, 0x01, 0x26,
    };
    static const uint8_t x_fields[] = {0x75, 0x10, 0x55, 0x0e, 0x65, 0x13,
                                       0x09, 0x30, 0x35, 0x00, 0x46};
    static const uint8_t between_xy[] = {0x81, 0x02, 0x26};
    static const uint8_t y_fields[] = {0x46};
    static const uint8_t dimensions[] = {0x09, 0x31, 0x81, 0x02, 0x05, 0x0d,
                                         0x09, 0x48, 0x09, 0x49, 0x95, 0x02,
                                         0x81, 0x02, 0xc0};
    static const uint8_t suffix[] = {0x09, 0x54, 0x25, 0x7f, 0x95, 0x01, 0x75,
                                     0x08, 0x81, 0x02, 0x85, 0x02, 0x09, 0x55,
                                     0x95, 0x01, 0x25, 0x10, 0xb1, 0x02, 0xc0};
    uint8_t *out = buf;
    uint8_t *end = buf + cap;

    if (!buf)
        return 0;

#define APPEND(ptr, n)                          \
    do {                                        \
        size_t _n = (n);                        \
        if ((size_t)(end - out) < _n)           \
            return 0;                           \
        memcpy(out, (ptr), _n);                 \
        out += _n;                              \
    } while (0)
#define APPEND_ARR(arr) APPEND((arr), sizeof(arr))
#define APPEND_U16(v)                           \
    do {                                        \
        uint16_t _v = (uint16_t)(v);            \
        uint8_t _b[2] = {(uint8_t)(_v & 0xff), (uint8_t)(_v >> 8)}; \
        APPEND(_b, 2);                          \
    } while (0)

    APPEND_ARR(prefix);

    for (int finger = 0; finger < UM_HID_MAX_CONTACTS; finger++) {
        APPEND_ARR(finger_prefix);
        APPEND_U16(width); /* logical maximum X */

        APPEND_ARR(x_fields);
        APPEND_U16(width); /* physical maximum X */

        APPEND_ARR(between_xy);
        APPEND_U16(height); /* logical maximum Y */

        APPEND_ARR(y_fields);
        APPEND_U16(height); /* physical maximum Y */

        APPEND_ARR(dimensions);
    }

    APPEND_ARR(suffix);

#undef APPEND
#undef APPEND_ARR
#undef APPEND_U16

    return (size_t)(out - buf);
}

size_t um_hid_touch_report(uint8_t out[UM_HID_REPORT_SIZE],
                           const struct um_touch_point *pts, int count)
{
    int used = 0;

    memset(out, 0, UM_HID_REPORT_SIZE);
    out[0] = 0x01; /* report id */

    if (count > UM_HID_MAX_CONTACTS)
        count = UM_HID_MAX_CONTACTS;

    for (int i = 0; i < count; i++) {
        uint8_t *it = out + 1 + i * 10;
        it[0] = 0x01; /* tip switch */
        it[1] = pts[i].id;
        it[2] = (uint8_t)(pts[i].x & 0xff);
        it[3] = (uint8_t)(pts[i].x >> 8);
        it[4] = (uint8_t)(pts[i].y & 0xff);
        it[5] = (uint8_t)(pts[i].y >> 8);
        it[6] = (uint8_t)(pts[i].w & 0xff);
        it[7] = (uint8_t)(pts[i].w >> 8);
        it[8] = (uint8_t)(pts[i].h & 0xff);
        it[9] = (uint8_t)(pts[i].h >> 8);
        used++;
    }

    out[UM_HID_REPORT_SIZE - 1] = (uint8_t)used;
    return UM_HID_REPORT_SIZE;
}
