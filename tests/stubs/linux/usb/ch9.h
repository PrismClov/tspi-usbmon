/* Compile-check stub, not shipped. */
#ifndef _STUB_CH9_H
#define _STUB_CH9_H
#include <linux/types.h>
#define USB_DIR_OUT 0
#define USB_DIR_IN 0x80
#define USB_TYPE_MASK (0x03 << 5)
#define USB_TYPE_STANDARD (0x00 << 5)
#define USB_TYPE_CLASS (0x01 << 5)
#define USB_TYPE_VENDOR (0x02 << 5)
#define USB_RECIP_DEVICE 0x00
#define USB_RECIP_INTERFACE 0x01
#define USB_RECIP_ENDPOINT 0x02
#define USB_REQ_GET_STATUS 0x00
#define USB_REQ_GET_DESCRIPTOR 0x06
#define USB_DT_DEVICE 1
#define USB_DT_CONFIG 2
#define USB_DT_STRING 3
#define USB_DT_INTERFACE 4
#define USB_DT_ENDPOINT 5
#define USB_DT_INTERFACE_SIZE 9
#define USB_DT_ENDPOINT_SIZE 7
#define USB_CLASS_VENDOR_SPEC 0xff
#define USB_CLASS_HID 3
#define USB_ENDPOINT_XFER_CONTROL 0
#define USB_ENDPOINT_XFER_ISOC 1
#define USB_ENDPOINT_XFER_BULK 2
#define USB_ENDPOINT_XFER_INT 3
struct usb_ctrlrequest {
    __u8 bRequestType; __u8 bRequest; __le16 wValue; __le16 wIndex; __le16 wLength;
} __attribute__((packed));
struct usb_interface_descriptor {
    __u8 bLength, bDescriptorType, bInterfaceNumber, bAlternateSetting;
    __u8 bNumEndpoints, bInterfaceClass, bInterfaceSubClass, bInterfaceProtocol, iInterface;
} __attribute__((packed));
struct usb_endpoint_descriptor_no_audio {
    __u8 bLength, bDescriptorType, bEndpointAddress, bmAttributes;
    __le16 wMaxPacketSize; __u8 bInterval;
} __attribute__((packed));
#endif
