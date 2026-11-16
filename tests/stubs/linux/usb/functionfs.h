/* Compile-check stub, not shipped. */
#ifndef _STUB_FUNCTIONFS_H
#define _STUB_FUNCTIONFS_H
#include <linux/types.h>
#include <linux/usb/ch9.h>
#define FUNCTIONFS_DESCRIPTORS_MAGIC 1
#define FUNCTIONFS_STRINGS_MAGIC 2
#define FUNCTIONFS_DESCRIPTORS_MAGIC_V2 3
#define FUNCTIONFS_HAS_FS_DESC 1
#define FUNCTIONFS_HAS_HS_DESC 2
#define FUNCTIONFS_HAS_SS_DESC 4
struct usb_functionfs_descs_head_v2 { __le32 magic, length, flags; } __attribute__((packed));
struct usb_functionfs_strings_head { __le32 magic, length, str_count, lang_count; } __attribute__((packed));
struct usb_functionfs_event {
    union { struct usb_ctrlrequest setup; __u8 data[8]; } u;
    __u8 type, pad[3];
} __attribute__((packed));
enum { FUNCTIONFS_BIND = 0, FUNCTIONFS_UNBIND, FUNCTIONFS_ENABLE, FUNCTIONFS_DISABLE,
       FUNCTIONFS_SETUP, FUNCTIONFS_SUSPEND, FUNCTIONFS_RESUME };
#endif
