/*
 * Copyright (C) 2021 Huawei Device Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#ifndef HDC_USBFFS_H
#define HDC_USBFFS_H
// clang-format off
#include <linux/usb/functionfs.h>
#include "daemon_common.h"
// clang-format on

namespace Hdc {
constexpr auto HDC_USBDR_SND = 0x0;
constexpr auto HDC_USBDR_RCV = 0x80;
constexpr auto HDC_USBMD_BLK = 0X02;
constexpr auto HDC_USBMD_RCV = 0X03;
constexpr auto HDC_CLASS = 0xff;
constexpr auto HDC_SUBCLASS = 0x50;
constexpr auto HDC_FSPKT_SIZE_MAX = 64;
constexpr auto HDC_HSPKT_SIZE_MAX = 512;
constexpr uint16_t HDC_SSPKT_SIZE_MAX = 1024;
constexpr auto USB_FFS_BASE = "/dev/usb-ffs/";
constexpr auto HDC_USBTF_DEV = 0x01;
constexpr auto HDC_USBTF_CFG = 0x02;
constexpr auto HDC_USBTF_STR = 0x03;
constexpr auto HDC_USBTF_ITF = 0x04;
constexpr auto HDC_USBTF_EPS = 0x05;

#define SHORT_LE(x) htole16(x)
#define LONG_LE(x) htole32(x)
#define HDC_INTERFACE_NAME "HDC Interface"

struct UsbFunctionDesc {
    struct usb_interface_descriptor ifDesc;
    struct usb_endpoint_descriptor_no_audio from;
    struct usb_endpoint_descriptor_no_audio to;
} __attribute__((packed));

static const struct {
    struct usb_functionfs_strings_head head;
    struct {
        __le16 code;
        const char name[sizeof(HDC_INTERFACE_NAME)];
    } __attribute__((packed)) firstItem;
} __attribute__((packed)) USB_FFS_VALUE = {
    .head =
        {
            .magic = LONG_LE(FUNCTIONFS_STRINGS_MAGIC),
            .length = LONG_LE(sizeof(USB_FFS_VALUE)),
            .str_count = LONG_LE(1),
            .lang_count = LONG_LE(1),
        },
    .firstItem =
        {
            SHORT_LE(0x0409),
            HDC_INTERFACE_NAME,
        },
};

struct UsbFunctionfsDescsHeadOld {
    __le32 magic;
    __le32 length;
    __le32 config1Count;
    __le32 config2Count;
} __attribute__((packed));

struct UsbFuncConfig {
    struct usb_interface_descriptor ifDesc;
    struct usb_endpoint_descriptor_no_audio from;
    struct usb_ss_ep_comp_descriptor pairFrom;
    struct usb_endpoint_descriptor_no_audio to;
    struct usb_ss_ep_comp_descriptor pairTo;
} __attribute__((packed));

static struct UsbFuncConfig config3 = {
    .ifDesc = {
        .bLength = sizeof(config3.ifDesc),
        .bDescriptorType = USB_DT_INTERFACE,
        .bInterfaceNumber = 0,
        .bNumEndpoints = 2,
        .bInterfaceClass = HDC_CLASS,
        .bInterfaceSubClass = HDC_SUBCLASS,
        .bInterfaceProtocol = VER_PROTOCOL,
        .iInterface = 1
    },
    .from = {
        .bLength = sizeof(config3.from),
        .bDescriptorType = USB_DT_ENDPOINT,
        .bEndpointAddress = 1 | USB_DIR_OUT,
        .bmAttributes = USB_ENDPOINT_XFER_BULK,
        .wMaxPacketSize = HDC_SSPKT_SIZE_MAX,
    },
    .pairFrom = {
        .bLength = sizeof(config3.pairFrom),
        .bDescriptorType = USB_DT_SS_ENDPOINT_COMP,
        .bMaxBurst = 4,
    },
    .to = {
        .bLength = sizeof(config3.to),
        .bDescriptorType = USB_DT_ENDPOINT,
        .bEndpointAddress = 2 | USB_DIR_IN,
        .bmAttributes = USB_ENDPOINT_XFER_BULK,
        .wMaxPacketSize = HDC_SSPKT_SIZE_MAX,
    },
    .pairTo = {
        .bLength = sizeof(config3.pairTo),
        .bDescriptorType = USB_DT_SS_ENDPOINT_COMP,
        .bMaxBurst = 4,
    },
};

static struct UsbFunctionDesc config1 = {
    .ifDesc = {
        .bLength = sizeof(config1.ifDesc),
        .bDescriptorType = USB_DT_INTERFACE,
        .bInterfaceNumber = 0,
        .bNumEndpoints = 2,
        .bInterfaceClass = HDC_CLASS,
        .bInterfaceSubClass = HDC_SUBCLASS,
        .bInterfaceProtocol = VER_PROTOCOL,
        .iInterface = 1
    },
    .from = {
        .bLength = sizeof(config1.from),
        .bDescriptorType = USB_DT_ENDPOINT,
        .bEndpointAddress = 1 | USB_DIR_OUT,
        .bmAttributes = USB_ENDPOINT_XFER_BULK,
        .wMaxPacketSize = HDC_FSPKT_SIZE_MAX,
    },
    .to = {
        .bLength = sizeof(config1.to),
        .bDescriptorType = USB_DT_ENDPOINT,
        .bEndpointAddress = 2 | USB_DIR_IN,
        .bmAttributes = USB_ENDPOINT_XFER_BULK,
        .wMaxPacketSize = HDC_FSPKT_SIZE_MAX,
    },
};

static struct UsbFunctionDesc config2 = {
    .ifDesc = {
        .bLength = sizeof(config2.ifDesc),
        .bDescriptorType = USB_DT_INTERFACE,
        .bInterfaceNumber = 0,
        .bNumEndpoints = 2,
        .bInterfaceClass = HDC_CLASS,
        .bInterfaceSubClass = HDC_SUBCLASS,
        .bInterfaceProtocol = VER_PROTOCOL,
        .iInterface = 1
    },
    .from = {
        .bLength = sizeof(config2.from),
        .bDescriptorType = USB_DT_ENDPOINT,
        .bEndpointAddress = 1 | USB_DIR_OUT,
        .bmAttributes = USB_ENDPOINT_XFER_BULK,
        .wMaxPacketSize = HDC_HSPKT_SIZE_MAX,
    },
    .to = {
        .bLength = sizeof(config2.to),
        .bDescriptorType = USB_DT_ENDPOINT,
        .bEndpointAddress = 2 | USB_DIR_IN,
        .bmAttributes = USB_ENDPOINT_XFER_BULK,
        .wMaxPacketSize = HDC_HSPKT_SIZE_MAX,
    },
};

struct usb_functionfs_desc_v2 {
    struct usb_functionfs_descs_head_v2 head;
    __le32 config1Count;
    __le32 config2Count;
    __le32 config3Count;
    __le32 configWosCount;
    struct UsbFunctionDesc config1Desc, config2Desc;
    struct UsbFuncConfig config3Desc;
    struct usb_os_desc_header wosHead;
    struct usb_ext_compat_desc wosDesc;
} __attribute__((packed));

}  // namespace Hdc
#endif
