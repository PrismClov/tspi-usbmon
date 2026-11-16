/*
 * Touch input: reads the panel digitizer from evdev and forwards it to the
 * host as a 5-finger HID report, mapping panel coordinates back into the
 * host framebuffer space through the display geometry.
 *
 * SPDX-License-Identifier: MIT
 */
#define _GNU_SOURCE
#include "touch.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <poll.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#include "gadget.h"
#include "usbmon.h"

#define TOUCH_SLOTS 10

struct contact {
    int active;
    int raw_x, raw_y;
};

static struct {
    int fd;
    int has_mt;
    int min_x, max_x, min_y, max_y;
    int panel_w, panel_h;
    const struct um_geom *geom;
    const struct um_config *cfg;
    struct contact contacts[TOUCH_SLOTS];
    int cur_slot;
    int btn_down;
    int last_count;
    atomic_bool running;
    pthread_t thread;
} t;

#define TEST_BIT(bits, bit) \
    ((bits)[(bit) / (8 * sizeof(unsigned long))] & (1UL << ((bit) % (8 * sizeof(unsigned long)))))

static int read_abs_range(int fd, int code, int *min, int *max)
{
    struct input_absinfo ai;

    if (ioctl(fd, EVIOCGABS(code), &ai) < 0)
        return -1;
    *min = ai.minimum;
    *max = ai.maximum;
    return 0;
}

static int try_open(const char *path)
{
    unsigned long absbits[1 + ABS_CNT / (8 * sizeof(unsigned long))];
    int fd;
    int has_mt, has_st;

    fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0)
        return -1;

    memset(absbits, 0, sizeof(absbits));
    if (ioctl(fd, EVIOCGBIT(EV_ABS, sizeof(absbits)), absbits) < 0) {
        close(fd);
        return -1;
    }

    has_mt = TEST_BIT(absbits, ABS_MT_POSITION_X) && TEST_BIT(absbits, ABS_MT_POSITION_Y);
    has_st = TEST_BIT(absbits, ABS_X) && TEST_BIT(absbits, ABS_Y);

    if (!has_mt && !has_st) {
        close(fd);
        return -1;
    }

    t.has_mt = has_mt;
    if (read_abs_range(fd, has_mt ? ABS_MT_POSITION_X : ABS_X, &t.min_x, &t.max_x) < 0 ||
        read_abs_range(fd, has_mt ? ABS_MT_POSITION_Y : ABS_Y, &t.min_y, &t.max_y) < 0) {
        close(fd);
        return -1;
    }
    if (t.max_x <= t.min_x) {
        t.min_x = 0;
        t.max_x = t.panel_w - 1;
    }
    if (t.max_y <= t.min_y) {
        t.min_y = 0;
        t.max_y = t.panel_h - 1;
    }

    t.fd = fd;
    {
        char name[128] = "unknown";

        if (ioctl(fd, EVIOCGNAME(sizeof(name)), name) < 0)
            snprintf(name, sizeof(name), "unknown");
        um_info("touch device %s ('%s', mt=%d, x=%d..%d, y=%d..%d)", path, name, has_mt,
                t.min_x, t.max_x, t.min_y, t.max_y);
    }
    return 0;
}

static int open_touch_device(void)
{
    if (t.cfg->touch[0] && strcmp(t.cfg->touch, "auto"))
        return try_open(t.cfg->touch);

    for (int i = 0; i < 32; i++) {
        char path[64];

        snprintf(path, sizeof(path), "/dev/input/event%d", i);
        if (try_open(path) == 0)
            return 0;
    }
    return -1;
}

static void reset_contacts(void)
{
    memset(t.contacts, 0, sizeof(t.contacts));
    t.cur_slot = 0;
}

static void emit_report(void)
{
    struct um_touch_point pts[UM_HID_MAX_CONTACTS];
    uint8_t report[UM_HID_REPORT_SIZE];
    int n = 0;

    for (int i = 0; i < TOUCH_SLOTS && n < UM_HID_MAX_CONTACTS; i++) {
        int px, py, sx, sy, hx, hy;

        if (!t.contacts[i].active)
            continue;

        px = (t.contacts[i].raw_x - t.min_x) * t.panel_w / (t.max_x - t.min_x + 1);
        py = (t.contacts[i].raw_y - t.min_y) * t.panel_h / (t.max_y - t.min_y + 1);
        if (px < 0)
            px = 0;
        if (py < 0)
            py = 0;
        if (px >= t.panel_w)
            px = t.panel_w - 1;
        if (py >= t.panel_h)
            py = t.panel_h - 1;

        um_geom_panel_to_stream(t.geom, px, py, &sx, &sy);
        um_geom_stream_to_host(t.geom->sw, t.geom->sh, t.cfg->host_rotate, sx, sy, &hx,
                               &hy);

        pts[n].id = (uint8_t)i;
        pts[n].x = (uint16_t)hx;
        pts[n].y = (uint16_t)hy;
        pts[n].w = 5;
        pts[n].h = 5;
        n++;
    }

    if (n == 0 && t.last_count == 0)
        return;

    um_hid_touch_report(report, pts, n);
    um_gadget_touch(report, sizeof(report));
    t.last_count = n;
}

static void handle_abs(const struct input_event *ev)
{
    struct contact *c;

    if (ev->code == ABS_MT_SLOT) {
        if (ev->value >= 0 && ev->value < TOUCH_SLOTS)
            t.cur_slot = ev->value;
        return;
    }

    c = &t.contacts[t.cur_slot];

    if (ev->code == ABS_MT_TRACKING_ID) {
        c->active = ev->value >= 0;
        if (!c->active) {
            c->raw_x = 0;
            c->raw_y = 0;
        }
    } else if (ev->code == ABS_MT_POSITION_X || ev->code == ABS_X) {
        c->raw_x = ev->value;
        if (!t.has_mt)
            c->active = 1;
    } else if (ev->code == ABS_MT_POSITION_Y || ev->code == ABS_Y) {
        c->raw_y = ev->value;
        if (!t.has_mt)
            c->active = 1;
    }
}

static void handle_event(const struct input_event *ev)
{
    if (ev->type == EV_ABS) {
        handle_abs(ev);
    } else if (ev->type == EV_KEY && (ev->code == BTN_TOUCH || ev->code == BTN_MOUSE)) {
        t.btn_down = ev->value;
        if (!ev->value)
            reset_contacts();
    } else if (ev->type == EV_SYN && ev->code == SYN_REPORT) {
        emit_report();
    }
}

static void *touch_thread(void *arg)
{
    (void)arg;

    while (atomic_load(&t.running)) {
        struct input_event evs[64];
        struct pollfd pfd;
        ssize_t n;

        if (t.fd < 0) {
            if (open_touch_device() < 0) {
                struct timespec ts = {.tv_sec = 0, .tv_nsec = 500000000L};

                nanosleep(&ts, NULL);
                continue;
            }
        }

        pfd.fd = t.fd;
        pfd.events = POLLIN;
        if (poll(&pfd, 1, 200) <= 0)
            continue;

        n = read(t.fd, evs, sizeof(evs));
        if (n < 0) {
            if (errno == EINTR || errno == EAGAIN)
                continue;
            um_warn("touch read: %s", strerror(errno));
            close(t.fd);
            t.fd = -1;
            continue;
        }

        for (size_t i = 0; i < (size_t)n / sizeof(evs[0]); i++)
            handle_event(&evs[i]);
    }

    if (t.fd >= 0) {
        close(t.fd);
        t.fd = -1;
    }
    return NULL;
}

int um_touch_start(const struct um_config *cfg, const struct um_geom *geom)
{
    memset(&t, 0, sizeof(t));
    t.fd = -1;
    t.cfg = cfg;
    t.geom = geom;
    t.panel_w = geom->pw;
    t.panel_h = geom->ph;
    atomic_store(&t.running, true);

    if (open_touch_device() < 0)
        um_warn("no touch device found, touch forwarding disabled");

    if (pthread_create(&t.thread, NULL, touch_thread, NULL) != 0) {
        um_err("cannot start touch thread");
        atomic_store(&t.running, false);
        return -1;
    }
    return 0;
}

void um_touch_stop(void)
{
    if (!atomic_load(&t.running))
        return;
    atomic_store(&t.running, false);
    pthread_join(t.thread, NULL);
}
