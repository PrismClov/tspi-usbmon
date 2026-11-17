/*
 * tspi-usbmon - USB secondary display for LCKFB Taishan Pi (RK3566).
 * SPDX-License-Identifier: MIT
 */
#define _GNU_SOURCE
#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "display.h"
#include "gadget.h"
#include "jpegdec.h"
#include "touch.h"
#include "usbmon.h"

static volatile sig_atomic_t g_stop;

static void on_signal(int sig)
{
    (void)sig;
    g_stop = 1;
}

static uint64_t now_ms(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
}

static void usage(const char *argv0)
{
    printf("tspi-usbmon %s - USB secondary display for Taishan Pi RK3566\n\n",
           UM_VERSION);
    printf("usage: %s [options]\n\n", argv0);
    printf("  -c, --config <path>   config file (default /etc/tspi-usbmon.conf)\n");
    printf("  -s, --set <k=v>       override one config key (repeatable)\n");
    printf("  -v, --verbose         debug logging (repeat for more)\n");
    printf("  -h, --help            this help\n");
    printf("  -V, --version         version\n\n");
    printf("config keys: stream=WxH rotate=0|90|180|270 fps=.. jpeg_quality=1..10\n");
    printf("             max_frame_kb=.. card=.. connector=.. mode=.. brightness=..\n");
    printf("             touch=.. udc=.. product=.. interface=.. vid=.. pid=..\n");
    printf("             splash=.. verbose=0|1|2\n");
}

static struct um_decoder *load_image(const char *path)
{
    FILE *fp;
    long size;
    uint8_t *buf;
    struct um_decoder *dec;

    fp = fopen(path, "rb");
    if (!fp)
        return NULL;
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return NULL;
    }
    size = ftell(fp);
    if (size <= 0 || size > 16 * 1024 * 1024) {
        fclose(fp);
        return NULL;
    }
    rewind(fp);
    buf = malloc((size_t)size);
    if (!buf) {
        fclose(fp);
        return NULL;
    }
    if (fread(buf, 1, (size_t)size, fp) != (size_t)size) {
        free(buf);
        fclose(fp);
        return NULL;
    }
    fclose(fp);

    dec = um_decoder_new();
    if (!dec || um_decoder_decode(dec, buf, (size_t)size) < 0) {
        um_warn("cannot decode splash image %s", path);
        um_decoder_free(dec);
        free(buf);
        return NULL;
    }
    free(buf);
    um_info("splash %s (%dx%d)", path, um_decoder_width(dec), um_decoder_height(dec));
    return dec;
}

int main(int argc, char **argv)
{
    struct um_config cfg;
    struct um_geom geom;
    struct um_display *disp = NULL;
    struct um_decoder *dec = NULL;
    struct um_decoder *splash = NULL;
    const char *config_path = "/etc/tspi-usbmon.conf";
    const char *sets[32];
    int nsets = 0;
    int verbose = 0;
    int panel_w = 0, panel_h = 0;
    int ret = 1;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-c") || !strcmp(argv[i], "--config")) {
            if (++i >= argc) {
                um_err("missing argument for %s", argv[i - 1]);
                return 2;
            }
            config_path = argv[i];
        } else if (!strcmp(argv[i], "-s") || !strcmp(argv[i], "--set")) {
            if (++i >= argc || nsets >= (int)(sizeof(sets) / sizeof(sets[0]))) {
                um_err("missing argument for %s", argv[i - 1]);
                return 2;
            }
            sets[nsets++] = argv[i];
        } else if (!strcmp(argv[i], "-v") || !strcmp(argv[i], "--verbose")) {
            verbose++;
        } else if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
            usage(argv[0]);
            return 0;
        } else if (!strcmp(argv[i], "-V") || !strcmp(argv[i], "--version")) {
            printf("tspi-usbmon %s\n", UM_VERSION);
            return 0;
        } else {
            um_err("unknown option '%s'", argv[i]);
            usage(argv[0]);
            return 2;
        }
    }

    um_config_defaults(&cfg);
    um_config_load(&cfg, config_path);
    for (int i = 0; i < nsets; i++) {
        if (um_config_set(&cfg, sets[i]) < 0)
            um_warn("ignoring invalid override '%s'", sets[i]);
    }
    if (verbose > cfg.verbose)
        cfg.verbose = verbose;
    um_log_set_level(cfg.verbose >= 2 ? UM_LOG_DEBUG : UM_LOG_INFO);

    um_config_build_strings(&cfg);
    um_info("tspi-usbmon %s starting", UM_VERSION);
    um_info("stream %dx%d rotate %d, product '%s'", cfg.stream_w, cfg.stream_h,
            cfg.rotate, cfg.product);

    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    signal(SIGPIPE, SIG_IGN);

    dec = um_decoder_new();
    if (!dec) {
        um_err("cannot initialise turbojpeg decoder");
        return 1;
    }

    disp = um_display_open(&cfg, &panel_w, &panel_h);
    if (!disp)
        goto out;

    if (cfg.brightness >= 0)
        um_display_set_brightness(cfg.brightness);

    if (um_geom_init(&geom, cfg.stream_w, cfg.stream_h, cfg.rotate, panel_w, panel_h) < 0) {
        um_err("invalid geometry");
        goto out;
    }
    um_info("panel %dx%d, stream %dx%d rot %d, host %dx%d rot %d, dest %dx%d+%d+%d",
            panel_w, panel_h, cfg.stream_w, cfg.stream_h, cfg.rotate, cfg.host_w,
            cfg.host_h, cfg.host_rotate, geom.dw, geom.dh, geom.dx, geom.dy);

    splash = load_image(cfg.splash);

    if (cfg.no_gadget) {
        um_warn("no_gadget=1: USB gadget disabled, display-only mode");
    } else if (um_gadget_start(&cfg) < 0) {
        um_err("cannot start USB gadget");
        um_gadget_stop();
        goto out_geom;
    }
    if (um_touch_start(&cfg, &geom) < 0) {
        um_err("cannot start touch forwarding");
        goto out_gadget;
    }

    uint64_t frames = 0, failed = 0, bytes = 0;
    uint64_t decode_us = 0;
    uint64_t last_stats = now_ms();
    uint64_t stat_frames = 0;
    int splash_shown = 0;

    while (!g_stop) {
        struct um_frame *f = um_gadget_wait_frame(200);

        if (f) {
            uint64_t t0 = now_ms();

            if (um_decoder_decode(dec, f->data, f->len) == 0) {
                int pitch = 0;
                uint8_t *fb = um_display_buffer(disp, &pitch);

                if (fb) {
                    um_compose(fb, pitch, panel_w, panel_h, um_decoder_data(dec),
                               um_decoder_width(dec), um_decoder_height(dec),
                               um_decoder_pitch(dec), &geom);
                    um_display_flip(disp);
                }
                frames++;
                stat_frames++;
                bytes += f->len;
                decode_us += (now_ms() - t0) * 1000u;
                splash_shown = 0;
            } else {
                failed++;
            }
            um_gadget_release_frame(f);
        } else if (!um_gadget_enabled() && splash && !splash_shown) {
            int pitch = 0;
            uint8_t *fb = um_display_buffer(disp, &pitch);

            if (fb)
                um_compose_fit(fb, pitch, panel_w, panel_h, um_decoder_data(splash),
                               um_decoder_width(splash), um_decoder_height(splash),
                               um_decoder_pitch(splash));
            um_display_flip(disp);
            splash_shown = 1;
        }

        uint64_t now = now_ms();

        if (now - last_stats >= 5000) {
            uint64_t dt = now - last_stats;

            um_info("usb=%d frames=%llu (%.1f fps) failed=%llu avg=%.1f ms %.2f MB/s",
                    um_gadget_enabled() ? 1 : 0, (unsigned long long)frames,
                    dt ? (double)stat_frames * 1000.0 / (double)dt : 0.0,
                    (unsigned long long)failed,
                    stat_frames ? (double)decode_us / 1000.0 / (double)stat_frames : 0.0,
                    dt ? (double)bytes / 1048576.0 * 1000.0 / (double)dt : 0.0);
            last_stats = now;
            stat_frames = 0;
            bytes = 0;
            decode_us = 0;
        }
    }

    um_info("shutting down");
    ret = 0;

    um_touch_stop();
out_gadget:
    um_gadget_stop();
out_geom:
    um_geom_fini(&geom);
out:
    if (splash)
        um_decoder_free(splash);
    if (dec)
        um_decoder_free(dec);
    if (disp)
        um_display_close(disp);
    return ret;
}
