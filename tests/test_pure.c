/*
 * Host-side unit tests for the dependency-free modules:
 * protocol parser, HID descriptor/report, geometry and compose.
 *
 * Build:  make test
 * SPDX-License-Identifier: MIT
 */
#include "usbmon.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_fail;

#define CHECK(cond)                                                           \
    do {                                                                      \
        if (!(cond)) {                                                        \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);            \
            g_fail++;                                                         \
        }                                                                     \
    } while (0)

static void put_hdr(uint8_t *b, uint8_t type, uint16_t w, uint16_t h, uint16_t id,
                    uint32_t len)
{
    uint32_t packed = (uint32_t)(id & 0x3ffu) | (len << 10);

    memset(b, 0, UM_HDR_SIZE);
    b[2] = type;
    b[8] = (uint8_t)(w & 0xff);
    b[9] = (uint8_t)(w >> 8);
    b[10] = (uint8_t)(h & 0xff);
    b[11] = (uint8_t)(h >> 8);
    b[12] = (uint8_t)(packed & 0xff);
    b[13] = (uint8_t)((packed >> 8) & 0xff);
    b[14] = (uint8_t)((packed >> 16) & 0xff);
    b[15] = (uint8_t)((packed >> 24) & 0xff);
}

static void test_proto_basic(void)
{
    uint8_t buf[16 + 1000];
    struct um_proto p;
    struct um_frame_hdr h;
    size_t off = 0;
    int headers = 0;

    put_hdr(buf, UM_TYPE_JPEG, 1080, 2160, 7, 1000);
    memset(buf + 16, 0xa5, 1000);

    um_proto_init(&p, 4u << 20);

    while (off < sizeof(buf) && headers == 0) {
        size_t used = 0;
        enum um_proto_result r = um_proto_scan(&p, buf + off, sizeof(buf) - off, &used, &h);

        CHECK(used > 0);
        off += used;
        if (r == UM_PROTO_HEADER) {
            headers++;
            CHECK(h.type == UM_TYPE_JPEG);
            CHECK(h.width == 1080);
            CHECK(h.height == 2160);
            CHECK(h.id == 7);
            CHECK(h.payload == 1000);
            CHECK(h.x == 0 && h.y == 0);
        } else if (r == UM_PROTO_RESYNC) {
            CHECK(0);
        }
    }
    CHECK(headers == 1);
    CHECK(off == 16);
    CHECK(p.frames == 1);
    CHECK(p.resyncs == 0);

    /* Body is read separately: exactly payload bytes. */
    CHECK(sizeof(buf) - off == 1000);
}

static void test_proto_resync(void)
{
    uint8_t buf[5 + 16 + 200];
    struct um_proto p;
    struct um_frame_hdr h;
    size_t off = 0;
    int resyncs = 0, headers = 0;

    memset(buf, 0x11, 5); /* stray bytes, e.g. tail of a broken frame */
    put_hdr(buf + 5, UM_TYPE_JPEG, 720, 1440, 3, 200);
    memset(buf + 5 + 16, 0x5a, 200);

    um_proto_init(&p, 1u << 20);
    while (off < sizeof(buf) && headers == 0) {
        size_t used = 0;
        enum um_proto_result r = um_proto_scan(&p, buf + off, sizeof(buf) - off, &used, &h);

        off += used;
        if (r == UM_PROTO_RESYNC)
            resyncs++;
        if (r == UM_PROTO_HEADER) {
            headers++;
            CHECK(h.payload == 200);
            CHECK(h.width == 720 && h.height == 1440);
        }
    }
    CHECK(resyncs == 5);
    CHECK(headers == 1);
    CHECK(off == 5 + 16);
}

static void test_proto_invalid(void)
{
    uint8_t raw[UM_HDR_SIZE];
    struct um_frame_hdr h;

    put_hdr(raw, UM_TYPE_JPEG, 1080, 2160, 1, 40); /* payload too small */
    CHECK(!um_hdr_parse(raw, 4u << 20, &h));

    put_hdr(raw, UM_TYPE_JPEG, 1080, 2160, 1, 4u << 20);
    CHECK(!um_hdr_parse(raw, 1u << 20, &h)); /* over limit */

    put_hdr(raw, 9, 1080, 2160, 1, 2000); /* unknown type */
    CHECK(!um_hdr_parse(raw, 4u << 20, &h));

    put_hdr(raw, UM_TYPE_JPEG, 0, 2160, 1, 2000);
    CHECK(!um_hdr_parse(raw, 4u << 20, &h));

    put_hdr(raw, UM_TYPE_END, 0, 0, 0, 0);
    CHECK(um_hdr_parse(raw, 4u << 20, &h) && h.type == UM_TYPE_END);
}

static void test_proto_byte_by_byte(void)
{
    uint8_t raw[UM_HDR_SIZE];
    struct um_proto p;
    struct um_frame_hdr h;
    int r;

    put_hdr(raw, UM_TYPE_JPEG, 1080, 2160, 1023, 300000);
    um_proto_init(&p, 4u << 20);

    for (size_t i = 0; i < UM_HDR_SIZE; i++) {
        size_t used = 0;

        r = um_proto_scan(&p, raw + i, 1, &used, &h);
        CHECK(used == 1);
        CHECK(r == (i + 1 == UM_HDR_SIZE ? UM_PROTO_HEADER : UM_PROTO_NEED_MORE));
    }
    CHECK(h.id == 1023);
    CHECK(h.payload == 300000);
}

static void test_hid_descriptor(void)
{
    uint8_t desc[512];
    size_t n = um_hid_descriptor(desc, sizeof(desc), 2160, 1080);

    /* 8 (prefix) + 5 * 69 + 21 (suffix) */
    CHECK(n == 374);
    CHECK(desc[0] == 0x05 && desc[1] == 0x0d);
    CHECK(desc[2] == 0x09 && desc[3] == 0x04);
    CHECK(desc[4] == 0xa1 && desc[5] == 0x01);
    CHECK(desc[6] == 0x85 && desc[7] == 0x01);
    CHECK(desc[n - 1] == 0xc0);

    /* "Contact Count Maximum" feature report 2 is what Windows requires. */
    CHECK(desc[n - 11] == 0x85 && desc[n - 10] == 0x02);
    CHECK(desc[n - 5] == 0x25 && desc[n - 4] == 0x10);
    CHECK(desc[n - 2] == 0x02);

    /* Logical maximum X of the first finger: 0x26 <width lo> <width hi>. */
    CHECK(desc[8 + 30] == 0x26);
    CHECK(desc[8 + 31] == (2160 & 0xff));
    CHECK(desc[8 + 32] == (2160 >> 8));

    /* Input(Y) then logical maximum Y: 0x81 0x02 0x26 <height lo> <height hi>. */
    CHECK(desc[8 + 46] == 0x81);
    CHECK(desc[8 + 47] == 0x02);
    CHECK(desc[8 + 48] == 0x26);
    CHECK(desc[8 + 49] == (1080 & 0xff));
    CHECK(desc[8 + 50] == (1080 >> 8));
}

static void test_hid_report(void)
{
    uint8_t rep[UM_HID_REPORT_SIZE];
    struct um_touch_point pts[2] = {
        {.id = 0, .x = 100, .y = 200, .w = 5, .h = 5},
        {.id = 1, .x = 3000, .y = 900, .w = 5, .h = 5},
    };
    size_t n = um_hid_touch_report(rep, pts, 2);

    CHECK(n == UM_HID_REPORT_SIZE);
    CHECK(rep[0] == 0x01);
    CHECK(rep[1] == 0x01); /* tip switch */
    CHECK(rep[2] == 0x00); /* contact id 0 */
    CHECK(rep[3] == (100 & 0xff) && rep[4] == 0);
    CHECK(rep[5] == (200 & 0xff) && rep[6] == 0);
    CHECK(rep[7] == 5 && rep[9] == 5);
    CHECK(rep[11] == 0x01 && rep[12] == 0x01);
    CHECK(rep[13] == (3000 & 0xff) && rep[14] == (3000 >> 8));
    CHECK(rep[51] == 2);

    n = um_hid_touch_report(rep, NULL, 0);
    CHECK(n == UM_HID_REPORT_SIZE && rep[0] == 1 && rep[51] == 0);
}

static void test_geom_native(void)
{
    struct um_geom g;

    CHECK(um_geom_init(&g, 1080, 2160, 0, 1080, 2160) == 0);
    CHECK(g.dw == 1080 && g.dh == 2160);
    CHECK(g.dx == 0 && g.dy == 0);

    int sx, sy;

    um_geom_panel_to_stream(&g, 0, 0, &sx, &sy);
    CHECK(sx == 0 && sy == 0);
    um_geom_panel_to_stream(&g, 1079, 2159, &sx, &sy);
    CHECK(sx == 1079 && sy == 2159);
    um_geom_fini(&g);
}

static void test_geom_rotate90(void)
{
    struct um_geom g;
    int sx, sy;

    /* Landscape stream 2160x1080 rotated into the portrait panel. */
    CHECK(um_geom_init(&g, 2160, 1080, 90, 1080, 2160) == 0);
    CHECK(g.rw == 1080 && g.rh == 2160);
    CHECK(g.dw == 1080 && g.dh == 2160);

    um_geom_panel_to_stream(&g, 0, 0, &sx, &sy);
    CHECK(sx == 0 && sy == 1079);
    um_geom_panel_to_stream(&g, 1079, 0, &sx, &sy);
    CHECK(sx == 0 && sy == 0);
    um_geom_panel_to_stream(&g, 0, 2159, &sx, &sy);
    CHECK(sx == 2159 && sy == 1079);
    um_geom_panel_to_stream(&g, 1079, 2159, &sx, &sy);
    CHECK(sx == 2159 && sy == 0);
    um_geom_fini(&g);
}

static void test_geom_letterbox(void)
{
    struct um_geom g;

    /* 16:9 stream on a 1:2 panel -> vertical letterbox. */
    CHECK(um_geom_init(&g, 1920, 1080, 0, 1080, 2160) == 0);
    CHECK(g.dw == 1080);
    CHECK(g.dh == 607); /* 1080 * 1080 / 1920 */
    CHECK(g.dx == 0);
    CHECK(g.dy == (2160 - 607) / 2);
    um_geom_fini(&g);

    /* Touch in the black bar must clamp into the image. */
    struct um_geom h;
    int sx, sy;

    CHECK(um_geom_init(&h, 1080, 2160, 0, 1080, 2160) == 0);
    um_geom_panel_to_stream(&h, -50, 99999, &sx, &sy);
    CHECK(sx == 0 && sy == 2159);
    um_geom_fini(&h);
}

static void test_compose_rotate90(void)
{
    /* 4x2 source, rotate 90 CW -> 2x4 panel. */
    uint32_t src[8] = {10, 11, 12, 13, 20, 21, 22, 23};
    uint32_t fb[8] = {0};
    struct um_geom g;

    CHECK(um_geom_init(&g, 4, 2, 90, 2, 4) == 0);
    CHECK(g.dw == 2 && g.dh == 4 && g.rw == 2 && g.rh == 4);
    um_compose((uint8_t *)fb, 8, 2, 4, (const uint8_t *)src, 4, 2, 16, &g);

    /* Rotating a 4x2 image 90 deg CW gives rows [E A] [F B] [G C] [H D]. */
    CHECK(fb[0] == 20 && fb[1] == 10);
    CHECK(fb[2] == 21 && fb[3] == 11);
    CHECK(fb[4] == 22 && fb[5] == 12);
    CHECK(fb[6] == 23 && fb[7] == 13);
    um_geom_fini(&g);
}

static void test_compose_native_copy(void)
{
    /* Padded JPEG (1088 wide) cropped into a 1080 wide panel. */
    enum { JW = 1088, JH = 4, PW = 1080, PH = 4 };
    static uint32_t src[JW * JH];
    static uint32_t fb[PW * PH];
    struct um_geom g;

    for (int i = 0; i < JW * JH; i++)
        src[i] = (uint32_t)i;

    CHECK(um_geom_init(&g, PW, PH, 0, PW, PH) == 0);
    um_compose((uint8_t *)fb, PW * 4, PW, PH, (const uint8_t *)src, JW, JH, JW * 4, &g);
    for (int y = 0; y < PH; y++) {
        for (int x = 0; x < PW; x++)
            CHECK(fb[y * PW + x] == src[y * JW + x]);
    }
    um_geom_fini(&g);
}

static void test_compose_scaled(void)
{
    /* 2x2 source scaled to 4x4: nearest neighbour must replicate pixels. */
    uint32_t src[4] = {1, 2, 3, 4};
    uint32_t fb[16] = {0};
    struct um_geom g;

    CHECK(um_geom_init(&g, 2, 2, 0, 4, 4) == 0);
    um_compose((uint8_t *)fb, 16, 4, 4, (const uint8_t *)src, 2, 2, 8, &g);
    CHECK(fb[0] == 1 && fb[1] == 1 && fb[2] == 2 && fb[3] == 2);
    CHECK(fb[4] == 1 && fb[5] == 1 && fb[6] == 2 && fb[7] == 2);
    CHECK(fb[8] == 3 && fb[9] == 3 && fb[10] == 4 && fb[11] == 4);
    um_geom_fini(&g);
}

static void test_host_mapping(void)
{
    int hw, hh, hx, hy;

    um_geom_host_dims(1080, 2160, 90, &hw, &hh);
    CHECK(hw == 2160 && hh == 1080);
    um_geom_host_dims(2160, 1080, 0, &hw, &hh);
    CHECK(hw == 2160 && hh == 1080);
    um_geom_host_dims(1080, 2160, 180, &hw, &hh);
    CHECK(hw == 1080 && hh == 2160);

    /* Linux default: host 2160x1080 landscape, stream portrait. */
    um_geom_stream_to_host(1080, 2160, 90, 0, 0, &hx, &hy);
    CHECK(hx == 0 && hy == 1079);
    um_geom_stream_to_host(1080, 2160, 90, 1079, 2159, &hx, &hy);
    CHECK(hx == 2159 && hy == 0);

    /* Windows: host space == stream space. */
    um_geom_stream_to_host(2160, 1080, 0, 5, 6, &hx, &hy);
    CHECK(hx == 5 && hy == 6);

    /* 180 rotation is its own inverse. */
    um_geom_stream_to_host(1080, 2160, 180, 0, 0, &hx, &hy);
    CHECK(hx == 1079 && hy == 2159);
}

static void test_config(void)
{
    struct um_config cfg;

    um_config_defaults(&cfg);
    CHECK(cfg.stream_w == 1080 && cfg.stream_h == 2160);
    CHECK(cfg.host_rotate == 90);
    CHECK(um_config_set(&cfg, "stream=2160x1080") == 0);
    CHECK(cfg.stream_w == 2160 && cfg.stream_h == 1080);
    CHECK(um_config_set(&cfg, "rotate=90") == 0);
    CHECK(cfg.rotate == 90);
    CHECK(um_config_set(&cfg, "host_rotate=0") == 0);
    CHECK(cfg.host_rotate == 0);
    CHECK(um_config_set(&cfg, "rotate=45") != 0);
    CHECK(um_config_set(&cfg, "jpeg_quality=11") != 0);
    CHECK(um_config_set(&cfg, "max_frame_kb=2048") == 0);
    CHECK(cfg.max_frame == 2048u * 1024u);
    CHECK(um_config_set(&cfg, "nonsense=1") != 0);

    um_config_build_strings(&cfg);
    CHECK(cfg.host_w == 2160 && cfg.host_h == 1080);
    CHECK(strstr(cfg.product, "R2160x1080") != NULL);
    CHECK(strstr(cfg.interface, "Ejpg6") != NULL);
    CHECK(strstr(cfg.interface, "Fps30") != NULL);
    CHECK(strstr(cfg.product, "Bl2097152") != NULL);
}

int main(void)
{
    um_log_set_level(UM_LOG_ERROR);

    test_proto_basic();
    test_proto_resync();
    test_proto_invalid();
    test_proto_byte_by_byte();
    test_hid_descriptor();
    test_hid_report();
    test_geom_native();
    test_geom_rotate90();
    test_geom_letterbox();
    test_host_mapping();
    test_compose_rotate90();
    test_compose_native_copy();
    test_compose_scaled();
    test_config();

    if (g_fail) {
        printf("%d check(s) failed\n", g_fail);
        return 1;
    }
    printf("all tests passed\n");
    return 0;
}
