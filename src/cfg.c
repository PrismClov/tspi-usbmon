/*
 * Configuration file / command line handling.
 * SPDX-License-Identifier: MIT
 */
#define _GNU_SOURCE
#include "usbmon.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void um_config_defaults(struct um_config *cfg)
{
    memset(cfg, 0, sizeof(*cfg));
    cfg->stream_w = 1080;
    cfg->stream_h = 2160;
    cfg->rotate = 0;
    cfg->host_rotate = 90;
    cfg->fps = 30;
    cfg->jpeg_quality = 6;
    cfg->max_frame = 4u * 1024u * 1024u;
    snprintf(cfg->card, sizeof(cfg->card), "/dev/dri/card0");
    snprintf(cfg->connector, sizeof(cfg->connector), "auto");
    snprintf(cfg->mode, sizeof(cfg->mode), "auto");
    cfg->brightness = -1;
    snprintf(cfg->touch, sizeof(cfg->touch), "auto");
    snprintf(cfg->udc, sizeof(cfg->udc), "auto");
    cfg->takeover_udc = 0;
    cfg->no_gadget = 0;
    cfg->tcp_port = 0;
    cfg->attach = 0;
    cfg->vid = 0x303a;
    cfg->pid = 0x2986;
    snprintf(cfg->splash, sizeof(cfg->splash), "/usr/share/tspi-usbmon/splash.jpg");
    cfg->verbose = 0;
}

static char *trim(char *s)
{
    char *end;

    while (*s && isspace((unsigned char)*s))
        s++;
    end = s + strlen(s);
    while (end > s && isspace((unsigned char)end[-1]))
        *--end = '\0';
    return s;
}

static int parse_size(const char *s, int *w, int *h)
{
    char sep;

    if (!s)
        return -1;
    if (sscanf(s, "%dx%d%c", w, h, &sep) != 2)
        return -1;
    if (*w <= 0 || *h <= 0 || *w > UM_MAX_DIM || *h > UM_MAX_DIM)
        return -1;
    return 0;
}

int um_config_set(struct um_config *cfg, const char *kv)
{
    char buf[256];
    char *key, *val;
    char *eq;

    if (!kv)
        return -1;
    snprintf(buf, sizeof(buf), "%s", kv);
    key = trim(buf);
    eq = strchr(key, '=');
    if (!eq)
        return -1;
    *eq = '\0';
    val = trim(eq + 1);
    key = trim(key);

    if (!strcmp(key, "stream")) {
        return parse_size(val, &cfg->stream_w, &cfg->stream_h);
    } else if (!strcmp(key, "rotate")) {
        int r = atoi(val);

        if (r != 0 && r != 90 && r != 180 && r != 270)
            return -1;
        cfg->rotate = r;
    } else if (!strcmp(key, "host_rotate")) {
        int r = atoi(val);

        if (r != 0 && r != 90 && r != 180 && r != 270)
            return -1;
        cfg->host_rotate = r;
    } else if (!strcmp(key, "fps")) {
        int v = atoi(val);

        if (v < 1 || v > 120)
            return -1;
        cfg->fps = v;
    } else if (!strcmp(key, "jpeg_quality") || !strcmp(key, "jpeg")) {
        int v = atoi(val);

        if (v < 1 || v > 10)
            return -1;
        cfg->jpeg_quality = v;
    } else if (!strcmp(key, "max_frame_kb") || !strcmp(key, "buffer_kb")) {
        long v = atol(val);

        if (v < 64 || v > 16384)
            return -1;
        cfg->max_frame = (uint32_t)v * 1024u;
    } else if (!strcmp(key, "card")) {
        snprintf(cfg->card, sizeof(cfg->card), "%s", val);
    } else if (!strcmp(key, "connector")) {
        snprintf(cfg->connector, sizeof(cfg->connector), "%s", val);
    } else if (!strcmp(key, "mode")) {
        snprintf(cfg->mode, sizeof(cfg->mode), "%s", val);
    } else if (!strcmp(key, "stretch")) {
        cfg->stretch = atoi(val) != 0;
    } else if (!strcmp(key, "brightness")) {
        int v = atoi(val);

        if (v < 0 || v > 255)
            return -1;
        cfg->brightness = v;
    } else if (!strcmp(key, "touch")) {
        snprintf(cfg->touch, sizeof(cfg->touch), "%s", val);
    } else if (!strcmp(key, "udc")) {
        snprintf(cfg->udc, sizeof(cfg->udc), "%s", val);
    } else if (!strcmp(key, "takeover_udc")) {
        cfg->takeover_udc = atoi(val) ? 1 : 0;
    } else if (!strcmp(key, "no_gadget")) {
        cfg->no_gadget = atoi(val) ? 1 : 0;
    } else if (!strcmp(key, "attach")) {
        cfg->attach = atoi(val) ? 1 : 0;
    } else if (!strcmp(key, "tcp_port")) {
        int v = atoi(val);

        if (v < 0 || v > 65535)
            return -1;
        cfg->tcp_port = v;
    } else if (!strcmp(key, "product")) {
        snprintf(cfg->product, sizeof(cfg->product), "%s", val);
    } else if (!strcmp(key, "interface")) {
        snprintf(cfg->interface, sizeof(cfg->interface), "%s", val);
    } else if (!strcmp(key, "vid")) {
        cfg->vid = (uint16_t)strtoul(val, NULL, 0);
    } else if (!strcmp(key, "pid")) {
        cfg->pid = (uint16_t)strtoul(val, NULL, 0);
    } else if (!strcmp(key, "splash")) {
        snprintf(cfg->splash, sizeof(cfg->splash), "%s", val);
    } else if (!strcmp(key, "verbose")) {
        cfg->verbose = atoi(val);
    } else {
        um_warn("config: unknown key '%s'", key);
        return -1;
    }

    return 0;
}

int um_config_load(struct um_config *cfg, const char *path)
{
    FILE *fp;
    char line[512];
    int lineno = 0;
    int errors = 0;

    fp = fopen(path, "r");
    if (!fp) {
        um_warn("config: cannot open %s, using defaults", path);
        return -1;
    }

    while (fgets(line, sizeof(line), fp)) {
        char *s;

        lineno++;
        s = trim(line);
        if (!*s || *s == '#' || *s == ';' || *s == '[')
            continue;
        if (um_config_set(cfg, s) < 0) {
            um_warn("config: %s:%d: invalid entry '%s'", path, lineno, s);
            errors++;
        }
    }
    fclose(fp);
    return errors ? -1 : 0;
}

void um_config_build_strings(struct um_config *cfg)
{
    um_geom_host_dims(cfg->stream_w, cfg->stream_h, cfg->host_rotate, &cfg->host_w,
                      &cfg->host_h);

    char auto_product[128];
    char auto_iface[128];

    snprintf(auto_product, sizeof(auto_product), "tspi_R%dx%d_Ejpg%d_Fps%d_Bl%u",
             cfg->stream_w, cfg->stream_h, cfg->jpeg_quality, cfg->fps,
             (unsigned)cfg->max_frame);
    snprintf(auto_iface, sizeof(auto_iface), "tspiudisp0_R%dx%d_Ejpg%d_Fps%d_Bl%u",
             cfg->stream_w, cfg->stream_h, cfg->jpeg_quality, cfg->fps,
             (unsigned)cfg->max_frame);

    if (!cfg->product[0] || !strcmp(cfg->product, "auto"))
        snprintf(cfg->product, sizeof(cfg->product), "%s", auto_product);
    if (!cfg->interface[0] || !strcmp(cfg->interface, "auto"))
        snprintf(cfg->interface, sizeof(cfg->interface), "%s", auto_iface);
}
