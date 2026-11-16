/* Compile-check stub, not shipped. */
#ifndef _STUB_LINUX_INPUT_H
#define _STUB_LINUX_INPUT_H
#include <linux/types.h>
#include <sys/ioctl.h>
struct input_event { struct { long tv_sec, tv_usec; } time; __u16 type, code; __s32 value; };
struct input_absinfo { __s32 value, minimum, maximum, fuzz, flat, resolution; };
#define EV_SYN 0x00
#define EV_KEY 0x01
#define EV_ABS 0x03
#define SYN_REPORT 0
#define ABS_X 0x00
#define ABS_Y 0x01
#define ABS_MT_SLOT 0x2f
#define ABS_MT_POSITION_X 0x35
#define ABS_MT_POSITION_Y 0x36
#define ABS_MT_TRACKING_ID 0x39
#define ABS_CNT 0x40
#define BTN_TOUCH 0x14a
#define BTN_MOUSE 0x110
#define EVIOCGNAME(len) _IOC(_IOC_READ, 'E', 0x06, len)
#define EVIOCGBIT(ev, len) _IOC(_IOC_READ, 'E', 0x20 + (ev), len)
#define EVIOCGABS(abs) _IOC(_IOC_READ, 'E', 0x40 + (abs), sizeof(struct input_absinfo))
#endif
