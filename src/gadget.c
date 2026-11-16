/*
 * USB gadget side: configfs + FunctionFS, video frame reception and HID
 * multi-touch forwarding.
 *
 * Descriptor layout mirrors the ESP32-P4 reference firmware so that both
 * host drivers bind to us unchanged:
 *   interface 0: vendor specific, bulk OUT 0x01 / bulk IN 0x81
 *   interface 1: HID digitizer, interrupt IN 0x82
 *
 * SPDX-License-Identifier: MIT
 */
#define _GNU_SOURCE
#include "gadget.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/usb/ch9.h>
#include <linux/usb/functionfs.h>
#include <poll.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "usbmon.h"

#define GADGET_DIR "/sys/kernel/config/usb_gadget/tspi_udisp"
#define FFS_MOUNT "/dev/ffs-udisp"
#define FFS_INSTANCE "udisp"
#define RX_SLOTS 3
#define RX_CHUNK (64 * 1024)

struct slot {
    uint8_t *data;
    size_t cap;
    size_t len;
    uint16_t w, h, id;
    unsigned seq;
    int ready;
};

static struct {
    int ep0_fd;
    int ep1_fd;
    int ep3_fd;
    pthread_t ep0_thread;
    pthread_t rx_thread;
    int ep0_started;
    int rx_started;
    atomic_bool running;
    atomic_bool enabled;
    pthread_mutex_t lock;
    pthread_cond_t cond;
    struct slot slots[RX_SLOTS];
    unsigned seq;
    uint32_t limit;
    char udc[64];
    uint8_t hid_desc[512];
    size_t hid_desc_len;
    const struct um_config *cfg;
} g;

/* ------------------------------------------------------------- utilities */

static int write_str(const char *path, const char *value)
{
    int fd = open(path, O_WRONLY | O_CLOEXEC);
    ssize_t n;

    if (fd < 0)
        return -1;
    n = write(fd, value, strlen(value));
    close(fd);
    return n == (ssize_t)strlen(value) ? 0 : -1;
}

static int mkdir_if_missing(const char *path)
{
    if (mkdir(path, 0775) == 0 || errno == EEXIST)
        return 0;
    return -1;
}

static void sleep_ms(int ms)
{
    struct timespec ts = {.tv_sec = ms / 1000, .tv_nsec = (long)(ms % 1000) * 1000000L};

    nanosleep(&ts, NULL);
}

static void close_ep(int *fd)
{
    if (*fd >= 0) {
        close(*fd);
        *fd = -1;
    }
}

/* --------------------------------------------------------- configfs setup */

static int read_udc_name(char *out, size_t len)
{
    FILE *fp = popen("ls /sys/class/udc 2>/dev/null | head -n 1", "r");

    if (!fp)
        return -1;
    if (!fgets(out, (int)len, fp)) {
        pclose(fp);
        return -1;
    }
    pclose(fp);
    out[strcspn(out, "\r\n")] = '\0';
    return out[0] ? 0 : -1;
}

/* Returns 1 and the gadget name if another configfs gadget holds this UDC. */
static int udc_owner(const char *udc, char *out, size_t outlen)
{
    DIR *dir = opendir("/sys/kernel/config/usb_gadget");
    struct dirent *de;
    int found = 0;

    if (!dir)
        return 0;

    while ((de = readdir(dir)) != NULL) {
        char path[320];
        char buf[128];
        ssize_t n;
        int fd;

        if (de->d_name[0] == '.')
            continue;
        snprintf(path, sizeof(path), "/sys/kernel/config/usb_gadget/%s/UDC", de->d_name);
        fd = open(path, O_RDONLY | O_CLOEXEC);
        if (fd < 0)
            continue;
        n = read(fd, buf, sizeof(buf) - 1);
        close(fd);
        if (n <= 0)
            continue;
        buf[n] = '\0';
        buf[strcspn(buf, "\r\n")] = '\0';
        if (!strcmp(buf, udc)) {
            snprintf(out, outlen, "%s", de->d_name);
            found = 1;
            break;
        }
    }
    closedir(dir);
    return found;
}

static int bind_udc(const struct um_config *cfg)
{
    char path[256];
    char owner[64];

    snprintf(path, sizeof(path), "%s/UDC", GADGET_DIR);
    if (write_str(path, g.udc) == 0)
        return 0;

    if (udc_owner(g.udc, owner, sizeof(owner))) {
        if (!cfg->takeover_udc) {
            um_err("UDC '%s' is already used by gadget '%s' (adb?); stop that service "
                   "or set takeover_udc = 1", g.udc, owner);
            return -1;
        }
        um_warn("taking UDC '%s' over from gadget '%s'", g.udc, owner);
        snprintf(path, sizeof(path), "/sys/kernel/config/usb_gadget/%s/UDC", owner);
        write_str(path, "");
        sleep_ms(200);
        snprintf(path, sizeof(path), "%s/UDC", GADGET_DIR);
        if (write_str(path, g.udc) == 0)
            return 0;
    }

    um_err("bind UDC '%s' failed: %s", g.udc, strerror(errno));
    return -1;
}

static void gadget_unbind(void)
{
    char path[256];

    snprintf(path, sizeof(path), "%s/UDC", GADGET_DIR);
    write_str(path, "");
}

static int gadget_configfs_init(const struct um_config *cfg)
{
    char path[256];
    char udc[64];
    char v[16];

    system("modprobe libcomposite 2>/dev/null");
    system("modprobe usb_f_fs 2>/dev/null");

    if (mkdir_if_missing("/sys/kernel/config/usb_gadget") < 0) {
        um_err("configfs unavailable (mount -t configfs none /sys/kernel/config?)");
        return -1;
    }
    if (mkdir_if_missing(GADGET_DIR) < 0) {
        um_err("cannot create %s: %s", GADGET_DIR, strerror(errno));
        return -1;
    }
    gadget_unbind();

    mkdir_if_missing(GADGET_DIR "/strings/0x409");
    mkdir_if_missing(GADGET_DIR "/configs/c.1/strings/0x409");
    mkdir_if_missing(GADGET_DIR "/functions/ffs.udisp");

    snprintf(v, sizeof(v), "0x%04x", cfg->vid);
    snprintf(path, sizeof(path), "%s/idVendor", GADGET_DIR);
    write_str(path, v);
    snprintf(v, sizeof(v), "0x%04x", cfg->pid);
    snprintf(path, sizeof(path), "%s/idProduct", GADGET_DIR);
    write_str(path, v);

    snprintf(path, sizeof(path), "%s/bcdUSB", GADGET_DIR);
    write_str(path, "0x0200");
    snprintf(path, sizeof(path), "%s/bcdDevice", GADGET_DIR);
    write_str(path, "0x0100");

    snprintf(path, sizeof(path), "%s/strings/0x409/manufacturer", GADGET_DIR);
    write_str(path, "LCKFB TaishanPi");
    snprintf(path, sizeof(path), "%s/strings/0x409/product", GADGET_DIR);
    write_str(path, cfg->product);
    snprintf(path, sizeof(path), "%s/strings/0x409/serialnumber", GADGET_DIR);
    write_str(path, "TSPI0001");

    snprintf(path, sizeof(path), "%s/configs/c.1/strings/0x409/configuration", GADGET_DIR);
    write_str(path, "USB display + touch");
    snprintf(path, sizeof(path), "%s/configs/c.1/MaxPower", GADGET_DIR);
    write_str(path, "500");
    snprintf(path, sizeof(path), "%s/configs/c.1/bmAttributes", GADGET_DIR);
    write_str(path, "0x80");

    snprintf(path, sizeof(path), "%s/configs/c.1/ffs.udisp", GADGET_DIR);
    if (symlink(GADGET_DIR "/functions/ffs.udisp", path) < 0 && errno != EEXIST) {
        um_err("cannot link ffs function: %s", strerror(errno));
        return -1;
    }

    mkdir_if_missing(FFS_MOUNT);
    if (mount(FFS_INSTANCE, FFS_MOUNT, "functionfs", 0, NULL) < 0 && errno != EBUSY) {
        um_err("cannot mount functionfs: %s", strerror(errno));
        return -1;
    }

    snprintf(udc, sizeof(udc), "%s", cfg->udc);
    if (!strcmp(udc, "auto") && read_udc_name(udc, sizeof(udc)) < 0) {
        um_err("no UDC in /sys/class/udc (is dwc3 in peripheral mode?)");
        return -1;
    }
    snprintf(g.udc, sizeof(g.udc), "%s", udc);
    um_info("using UDC '%s'", g.udc);
    return 0;
}

/* --------------------------------------------------------- FFS descriptors */

struct ffs_hid_descriptor {
    uint8_t length;
    uint8_t descriptor_type;
    uint16_t hid_version;
    uint8_t country_code;
    uint8_t descriptor_count;
    uint8_t report_descriptor_type;
    uint16_t report_descriptor_length;
} __attribute__((packed));

struct ffs_descriptors {
    struct usb_functionfs_descs_head_v2 header;
    uint32_t fs_count;
    uint32_t hs_count;
    struct usb_interface_descriptor fs_if;
    struct usb_endpoint_descriptor_no_audio fs_out;
    struct usb_endpoint_descriptor_no_audio fs_in;
    struct usb_interface_descriptor fs_hid_if;
    struct ffs_hid_descriptor fs_hid;
    struct usb_endpoint_descriptor_no_audio fs_hid_in;
    struct usb_interface_descriptor hs_if;
    struct usb_endpoint_descriptor_no_audio hs_out;
    struct usb_endpoint_descriptor_no_audio hs_in;
    struct usb_interface_descriptor hs_hid_if;
    struct ffs_hid_descriptor hs_hid;
    struct usb_endpoint_descriptor_no_audio hs_hid_in;
} __attribute__((packed));

static int ffs_write_descriptors(const struct um_config *cfg)
{
    struct ffs_descriptors d;
    uint8_t sbuf[256];
    struct usb_functionfs_strings_head sh;
    size_t iface_len = strlen(cfg->interface) + 1;
    size_t hid_len = sizeof("touch");
    size_t off;
    uint16_t lang = 0x0409;

    memset(&d, 0, sizeof(d));
    memset(&sh, 0, sizeof(sh));

    g.hid_desc_len = um_hid_descriptor(g.hid_desc, sizeof(g.hid_desc),
                                       (uint16_t)cfg->host_w, (uint16_t)cfg->host_h);

    d.header.magic = FUNCTIONFS_DESCRIPTORS_MAGIC_V2;
    d.header.length = (uint32_t)sizeof(d);
    d.header.flags = FUNCTIONFS_HAS_FS_DESC | FUNCTIONFS_HAS_HS_DESC;
    d.fs_count = 6;
    d.hs_count = 6;

    d.fs_if.bLength = USB_DT_INTERFACE_SIZE;
    d.fs_if.bDescriptorType = USB_DT_INTERFACE;
    d.fs_if.bInterfaceNumber = 0;
    d.fs_if.bNumEndpoints = 2;
    d.fs_if.bInterfaceClass = USB_CLASS_VENDOR_SPEC;
    d.fs_if.iInterface = 1;

    d.fs_out.bLength = USB_DT_ENDPOINT_SIZE;
    d.fs_out.bDescriptorType = USB_DT_ENDPOINT;
    d.fs_out.bEndpointAddress = 0x01;
    d.fs_out.bmAttributes = USB_ENDPOINT_XFER_BULK;
    d.fs_out.wMaxPacketSize = 64;

    d.fs_in.bLength = USB_DT_ENDPOINT_SIZE;
    d.fs_in.bDescriptorType = USB_DT_ENDPOINT;
    d.fs_in.bEndpointAddress = 0x81;
    d.fs_in.bmAttributes = USB_ENDPOINT_XFER_BULK;
    d.fs_in.wMaxPacketSize = 64;

    d.fs_hid_if.bLength = USB_DT_INTERFACE_SIZE;
    d.fs_hid_if.bDescriptorType = USB_DT_INTERFACE;
    d.fs_hid_if.bInterfaceNumber = 1;
    d.fs_hid_if.bNumEndpoints = 1;
    d.fs_hid_if.bInterfaceClass = USB_CLASS_HID;
    d.fs_hid_if.iInterface = 2;

    d.fs_hid.length = 9;
    d.fs_hid.descriptor_type = 0x21;
    d.fs_hid.hid_version = 0x0111;
    d.fs_hid.descriptor_count = 1;
    d.fs_hid.report_descriptor_type = 0x22;
    d.fs_hid.report_descriptor_length = (uint16_t)g.hid_desc_len;

    d.fs_hid_in.bLength = USB_DT_ENDPOINT_SIZE;
    d.fs_hid_in.bDescriptorType = USB_DT_ENDPOINT;
    d.fs_hid_in.bEndpointAddress = 0x82;
    d.fs_hid_in.bmAttributes = USB_ENDPOINT_XFER_INT;
    d.fs_hid_in.wMaxPacketSize = 64;
    d.fs_hid_in.bInterval = 10;

    d.hs_if = d.fs_if;
    d.hs_out = d.fs_out;
    d.hs_in = d.fs_in;
    d.hs_out.wMaxPacketSize = 512;
    d.hs_in.wMaxPacketSize = 512;
    d.hs_hid_if = d.fs_hid_if;
    d.hs_hid = d.fs_hid;
    d.hs_hid_in = d.fs_hid_in;
    d.hs_hid_in.wMaxPacketSize = 512;

    if (write(g.ep0_fd, &d, sizeof(d)) != (ssize_t)sizeof(d)) {
        um_err("write FFS descriptors: %s", strerror(errno));
        return -1;
    }

    /* Strings must be byte-contiguous: header, langid, str1\0, str2\0. */
    if (sizeof(sh) + 2 + iface_len + hid_len > sizeof(sbuf)) {
        um_err("interface string too long");
        return -1;
    }
    sh.magic = FUNCTIONFS_STRINGS_MAGIC;
    sh.length = (uint32_t)(sizeof(sh) + 2 + iface_len + hid_len);
    sh.str_count = 2;
    sh.lang_count = 1;

    memcpy(sbuf, &sh, sizeof(sh));
    off = sizeof(sh);
    memcpy(sbuf + off, &lang, 2);
    off += 2;
    memcpy(sbuf + off, cfg->interface, iface_len);
    off += iface_len;
    memcpy(sbuf + off, "touch", hid_len);
    off += hid_len;

    if (write(g.ep0_fd, sbuf, off) != (ssize_t)off) {
        um_err("write FFS strings: %s", strerror(errno));
        return -1;
    }
    return 0;
}

/* ------------------------------------------------------------ frame slots */

static void slots_init(void)
{
    for (int i = 0; i < RX_SLOTS; i++) {
        g.slots[i].data = malloc(g.limit);
        g.slots[i].cap = g.slots[i].data ? g.limit : 0;
        g.slots[i].ready = 0;
    }
}

static void slots_free(void)
{
    for (int i = 0; i < RX_SLOTS; i++) {
        free(g.slots[i].data);
        g.slots[i].data = NULL;
    }
}

static struct slot *slot_alloc(size_t need)
{
    struct slot *s = NULL;

    pthread_mutex_lock(&g.lock);
    for (int i = 0; i < RX_SLOTS; i++) {
        if (!g.slots[i].ready && g.slots[i].cap >= need) {
            s = &g.slots[i];
            break;
        }
    }
    pthread_mutex_unlock(&g.lock);

    if (s)
        s->len = 0;
    return s;
}

static void slot_push(struct slot *s)
{
    pthread_mutex_lock(&g.lock);
    s->seq = ++g.seq;
    s->ready = 1;
    pthread_cond_signal(&g.cond);
    pthread_mutex_unlock(&g.lock);
}

/* --------------------------------------------------------------- readers */

static void *rx_thread(void *arg)
{
    static uint8_t buf[RX_CHUNK];
    struct um_proto proto;
    struct um_frame_hdr hdr;
    struct slot *cur = NULL;
    size_t payload_left = 0;
    size_t discard_left = 0;

    (void)arg;
    um_proto_init(&proto, g.limit);

    while (atomic_load(&g.running)) {
        if (!atomic_load(&g.enabled)) {
            close_ep(&g.ep1_fd);
            um_proto_reset(&proto);
            cur = NULL;
            payload_left = 0;
            discard_left = 0;
            sleep_ms(50);
            continue;
        }

        if (g.ep1_fd < 0) {
            g.ep1_fd = open(FFS_MOUNT "/ep1", O_RDWR | O_CLOEXEC);
            if (g.ep1_fd < 0) {
                sleep_ms(50);
                continue;
            }
            um_info("video endpoint open");
        }

        struct pollfd pfd = {.fd = g.ep1_fd, .events = POLLIN};
        int pr = poll(&pfd, 1, 200);

        if (pr <= 0)
            continue;
        if (pfd.revents & (POLLERR | POLLHUP)) {
            close_ep(&g.ep1_fd);
            continue;
        }

        ssize_t n = read(g.ep1_fd, buf, sizeof(buf));

        if (n < 0) {
            if (errno == EINTR || errno == EAGAIN)
                continue;
            um_warn("video ep read: %s", strerror(errno));
            close_ep(&g.ep1_fd);
            continue;
        }
        if (n == 0)
            continue;

        size_t off = 0;

        while (off < (size_t)n) {
            if (cur == NULL && payload_left == 0 && discard_left == 0) {
                size_t used = 0;
                enum um_proto_result r =
                    um_proto_scan(&proto, buf + off, (size_t)n - off, &used, &hdr);

                off += used;
                if (r == UM_PROTO_HEADER) {
                    if (hdr.type == UM_TYPE_END)
                        continue;
                    payload_left = hdr.payload;
                    cur = slot_alloc((size_t)hdr.payload);
                    if (!cur) {
                        discard_left = payload_left;
                        payload_left = 0;
                        continue;
                    }
                    cur->w = hdr.width;
                    cur->h = hdr.height;
                    cur->id = hdr.id;
                    cur->len = 0;
                }
            } else if (cur) {
                size_t take = payload_left;
                size_t avail = (size_t)n - off;

                if (take > avail)
                    take = avail;
                memcpy(cur->data + cur->len, buf + off, take);
                cur->len += take;
                off += take;
                payload_left -= take;
                if (payload_left == 0) {
                    if (cur->len > 0)
                        slot_push(cur);
                    cur = NULL;
                }
            } else {
                size_t take = discard_left;
                size_t avail = (size_t)n - off;

                if (take > avail)
                    take = avail;
                off += take;
                discard_left -= take;
            }
        }
    }

    close_ep(&g.ep1_fd);
    return NULL;
}

/* ------------------------------------------------------------- ep0 / HID */

static void handle_setup(const struct usb_ctrlrequest *setup)
{
    uint16_t value = setup->wValue;
    uint16_t length = setup->wLength;
    uint8_t type = setup->bRequestType;
    uint8_t zeros[2] = {0, 0};

    if ((type & USB_DIR_IN) && setup->bRequest == USB_REQ_GET_DESCRIPTOR &&
        (value >> 8) == 0x22) {
        size_t len = g.hid_desc_len;

        if (len > length)
            len = length;
        if (write(g.ep0_fd, g.hid_desc, len) < 0)
            um_warn("ep0 HID report descriptor: %s", strerror(errno));
        return;
    }
    if ((type & USB_DIR_IN) && setup->bRequest == USB_REQ_GET_DESCRIPTOR &&
        (value >> 8) == 0x21) {
        struct ffs_hid_descriptor d = {9, 0x21, 0x0111, 0, 1, 0x22,
                                       (uint16_t)g.hid_desc_len};
        size_t len = sizeof(d);

        if (len > length)
            len = length;
        if (write(g.ep0_fd, &d, len) < 0)
            um_warn("ep0 HID descriptor: %s", strerror(errno));
        return;
    }
    if (type == (USB_DIR_IN | USB_TYPE_CLASS | USB_RECIP_INTERFACE) &&
        setup->bRequest == 0x01 && (value >> 8) == 3 && (value & 0xff) == 2) {
        /* Feature report 2: contact count maximum. Windows expects the report
         * id byte first, hence {2, 5}. */
        uint8_t rep[2] = {0x02, UM_HID_MAX_CONTACTS};
        size_t len = length >= 2 ? 2 : 0;

        if (write(g.ep0_fd, rep, len) < 0)
            um_warn("ep0 feature report 2: %s", strerror(errno));
        return;
    }
    if ((type & USB_DIR_IN) && setup->bRequest == USB_REQ_GET_STATUS) {
        write(g.ep0_fd, zeros, length >= 2 ? 2 : 0);
        return;
    }
    if (type & USB_DIR_IN) {
        write(g.ep0_fd, zeros, length > 0 ? 1 : 0);
        return;
    }

    {
        uint16_t total = length;

        while (length > 0) {
            uint8_t junk[64];
            size_t chunk = length > sizeof(junk) ? sizeof(junk) : length;
            ssize_t got = read(g.ep0_fd, junk, chunk);

            if (got <= 0)
                break;
            length -= (uint16_t)got;
        }
        /* Zero-length OUT (SET_IDLE, SET_REPORT...) still needs a status stage. */
        if (total == 0)
            (void)read(g.ep0_fd, NULL, 0);
    }
}

static void *ep0_thread(void *arg)
{
    struct usb_functionfs_event events[4];

    (void)arg;
    while (atomic_load(&g.running)) {
        struct pollfd pfd = {.fd = g.ep0_fd, .events = POLLIN};

        if (poll(&pfd, 1, 200) <= 0)
            continue;

        ssize_t n = read(g.ep0_fd, events, sizeof(events));

        if (n <= 0) {
            if (n < 0 && errno != EINTR && errno != EAGAIN)
                sleep_ms(50);
            continue;
        }

        for (size_t i = 0; i < (size_t)n / sizeof(events[0]); i++) {
            switch (events[i].type) {
            case FUNCTIONFS_ENABLE:
                um_info("USB enabled");
                atomic_store(&g.enabled, true);
                break;
            case FUNCTIONFS_DISABLE:
                um_info("USB disabled");
                atomic_store(&g.enabled, false);
                break;
            case FUNCTIONFS_UNBIND:
                atomic_store(&g.enabled, false);
                break;
            case FUNCTIONFS_SETUP:
                handle_setup(&events[i].u.setup);
                break;
            default:
                break;
            }
        }
    }
    return NULL;
}

/* --------------------------------------------------------------- public */

int um_gadget_start(const struct um_config *cfg)
{
    memset(&g, 0, sizeof(g));
    g.ep0_fd = g.ep1_fd = g.ep3_fd = -1;
    g.cfg = cfg;
    g.limit = cfg->max_frame;
    pthread_mutex_init(&g.lock, NULL);
    pthread_cond_init(&g.cond, NULL);
    atomic_store(&g.running, true);
    atomic_store(&g.enabled, false);

    if (gadget_configfs_init(cfg) < 0)
        return -1;

    g.ep0_fd = open(FFS_MOUNT "/ep0", O_RDWR | O_CLOEXEC);
    if (g.ep0_fd < 0) {
        um_err("open ep0: %s", strerror(errno));
        return -1;
    }
    if (ffs_write_descriptors(cfg) < 0)
        return -1;

    slots_init();
    for (int i = 0; i < RX_SLOTS; i++) {
        if (!g.slots[i].data) {
            um_err("out of memory for frame buffers (%u KB x %d)", cfg->max_frame / 1024,
                   RX_SLOTS);
            return -1;
        }
    }

    g.ep0_started = pthread_create(&g.ep0_thread, NULL, ep0_thread, NULL) == 0;
    g.rx_started = pthread_create(&g.rx_thread, NULL, rx_thread, NULL) == 0;

    if (bind_udc(cfg) < 0)
        return -1;
    um_info("gadget bound to %s", g.udc);
    return 0;
}

void um_gadget_stop(void)
{
    if (!atomic_load(&g.running))
        return;
    atomic_store(&g.running, false);
    gadget_unbind();
    if (g.rx_started)
        pthread_join(g.rx_thread, NULL);
    if (g.ep0_started)
        pthread_join(g.ep0_thread, NULL);
    close_ep(&g.ep1_fd);
    close_ep(&g.ep3_fd);
    close_ep(&g.ep0_fd);
    slots_free();
    pthread_mutex_destroy(&g.lock);
    pthread_cond_destroy(&g.cond);
}

bool um_gadget_enabled(void)
{
    return atomic_load(&g.enabled);
}

struct um_frame *um_gadget_wait_frame(int timeout_ms)
{
    struct um_frame *out = NULL;
    int best = -1;
    struct timespec deadline;

    clock_gettime(CLOCK_REALTIME, &deadline);
    deadline.tv_sec += timeout_ms / 1000;
    deadline.tv_nsec += (long)(timeout_ms % 1000) * 1000000L;
    if (deadline.tv_nsec >= 1000000000L) {
        deadline.tv_sec++;
        deadline.tv_nsec -= 1000000000L;
    }

    pthread_mutex_lock(&g.lock);
    for (;;) {
        best = -1;
        for (int i = 0; i < RX_SLOTS; i++) {
            if (g.slots[i].ready && (best < 0 || g.slots[i].seq > g.slots[best].seq))
                best = i;
        }
        if (best >= 0)
            break;
        if (timeout_ms <= 0)
            break;
        if (pthread_cond_timedwait(&g.cond, &g.lock, &deadline) == ETIMEDOUT)
            break;
    }

    if (best >= 0) {
        for (int i = 0; i < RX_SLOTS; i++) {
            if (i != best && g.slots[i].ready)
                g.slots[i].ready = 0;
        }
        out = calloc(1, sizeof(*out));
        if (out) {
            out->data = g.slots[best].data;
            out->len = g.slots[best].len;
            out->width = g.slots[best].w;
            out->height = g.slots[best].h;
            out->id = g.slots[best].id;
            out->slot = best;
        }
    }
    pthread_mutex_unlock(&g.lock);
    return out;
}

void um_gadget_release_frame(struct um_frame *f)
{
    if (!f)
        return;
    if (f->slot >= 0 && f->slot < RX_SLOTS) {
        pthread_mutex_lock(&g.lock);
        g.slots[f->slot].ready = 0;
        pthread_cond_signal(&g.cond);
        pthread_mutex_unlock(&g.lock);
    }
    free(f);
}

bool um_gadget_touch(const uint8_t *report, size_t len)
{
    if (!atomic_load(&g.enabled))
        return false;
    if (g.ep3_fd < 0) {
        g.ep3_fd = open(FFS_MOUNT "/ep3", O_WRONLY | O_NONBLOCK | O_CLOEXEC);
        if (g.ep3_fd < 0)
            return false;
    }
    if (write(g.ep3_fd, report, len) < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            return true;
        close_ep(&g.ep3_fd);
        return false;
    }
    return true;
}
