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
constexpr auto USB_FFS_HDC_EP0 = "/dev/usb-ffs/adb/ep0";
constexpr auto USB_FFS_HDC_OUT = "/dev/usb-ffs/adb/ep1";
constexpr auto USB_FFS_HDC_IN = "/dev/usb-ffs/adb/ep2";
constexpr auto HDC_USBTF_DEV = 0x01;
constexpr auto HDC_USBTF_CFG = 0x02;
constexpr auto HDC_USBTF_STR = 0x03;
constexpr auto HDC_USBTF_ITF = 0x04;
constexpr auto HDC_USBTF_EPS = 0x05;

#define CPU_TO_LE16(x) htole16(x)
#define CPU_TO_LE32(x) htole32(x)
#define HDC_INTERFACE_NAME "HDC Interface"

struct UsbFunctionDesc {
    struct usb_interface_descriptor intf;
    struct usb_endpoint_descriptor_no_audio source;
    struct usb_endpoint_descriptor_no_audio sink;
} __attribute__((packed));

static const struct {
    struct usb_functionfs_strings_head header;
    struct {
        __le16 code;
        const char str1[sizeof(HDC_INTERFACE_NAME)];
    } __attribute__((packed)) lang0;
} __attribute__((packed)) USB_FFS_VALUE = {
    .header =
        {
            .magic = CPU_TO_LE32(FUNCTIONFS_STRINGS_MAGIC),
            .length = CPU_TO_LE32(sizeof(USB_FFS_VALUE)),
            .str_count = CPU_TO_LE32(1),
            .lang_count = CPU_TO_LE32(1),
        },
    .lang0 =
        {
            CPU_TO_LE16(0x0409),
            HDC_INTERFACE_NAME,
        },
};

struct UsbFunctionfsDescsHeadOld {
    __le32 magic;
    __le32 length;
    __le32 fs_count;
    __le32 hs_count;
} __attribute__((packed));

struct ss_func_desc {
    struct usb_interface_descriptor intf;
    struct usb_endpoint_descriptor_no_audio source;
    struct usb_ss_ep_comp_descriptor source_comp;
    struct usb_endpoint_descriptor_no_audio sink;
    struct usb_ss_ep_comp_descriptor sink_comp;
} __attribute__((packed));

static struct ss_func_desc ss_descriptors = {
    .intf = {
        .bLength = sizeof(ss_descriptors.intf),
        .bDescriptorType = USB_DT_INTERFACE,
        .bInterfaceNumber = 0,
        .bNumEndpoints = 2,
        .bInterfaceClass = HDC_CLASS,
        .bInterfaceSubClass = HDC_SUBCLASS,
        .bInterfaceProtocol = VER_PROTOCOL,
        .iInterface = 1, /* first string from the provided table */
    },
    .source = {
        .bLength = sizeof(ss_descriptors.source),
        .bDescriptorType = USB_DT_ENDPOINT,
        .bEndpointAddress = 1 | USB_DIR_OUT,
        .bmAttributes = USB_ENDPOINT_XFER_BULK,
        .wMaxPacketSize = HDC_FSPKT_SIZE_MAX,
    },
    .source_comp = {
        .bLength = sizeof(ss_descriptors.source_comp),
        .bDescriptorType = USB_DT_SS_ENDPOINT_COMP,
        .bMaxBurst = 4,
    },
    .sink = {
        .bLength = sizeof(ss_descriptors.sink),
        .bDescriptorType = USB_DT_ENDPOINT,
        .bEndpointAddress = 2 | USB_DIR_IN,
        .bmAttributes = USB_ENDPOINT_XFER_BULK,
        .wMaxPacketSize = HDC_FSPKT_SIZE_MAX,
    },
    .sink_comp = {
        .bLength = sizeof(ss_descriptors.sink_comp),
        .bDescriptorType = USB_DT_SS_ENDPOINT_COMP,
        .bMaxBurst = 4,
    },
};

static struct UsbFunctionDesc fs_descriptors = {
    .intf = {
        .bLength = sizeof(fs_descriptors.intf),
        .bDescriptorType = USB_DT_INTERFACE,
        .bInterfaceNumber = 0,
        .bNumEndpoints = 2,
        .bInterfaceClass = HDC_CLASS,
        .bInterfaceSubClass = HDC_SUBCLASS,
        .bInterfaceProtocol = VER_PROTOCOL,
        .iInterface = 1, /* first string from the provided table */
    },
    .source = {
        .bLength = sizeof(fs_descriptors.source),
        .bDescriptorType = USB_DT_ENDPOINT,
        .bEndpointAddress = 1 | USB_DIR_OUT,
        .bmAttributes = USB_ENDPOINT_XFER_BULK,
        .wMaxPacketSize = HDC_FSPKT_SIZE_MAX,
    },
    .sink = {
        .bLength = sizeof(fs_descriptors.sink),
        .bDescriptorType = USB_DT_ENDPOINT,
        .bEndpointAddress = 2 | USB_DIR_IN,
        .bmAttributes = USB_ENDPOINT_XFER_BULK,
        .wMaxPacketSize = HDC_FSPKT_SIZE_MAX,
    },
};

static struct UsbFunctionDesc hs_descriptors = {
    .intf = {
        .bLength = sizeof(hs_descriptors.intf),
        .bDescriptorType = USB_DT_INTERFACE,
        .bInterfaceNumber = 0,
        .bNumEndpoints = 2,
        .bInterfaceClass = HDC_CLASS,
        .bInterfaceSubClass = HDC_SUBCLASS,
        .bInterfaceProtocol = VER_PROTOCOL,
        .iInterface = 1, /* first string from the provided table */
    },
    .source = {
        .bLength = sizeof(hs_descriptors.source),
        .bDescriptorType = USB_DT_ENDPOINT,
        .bEndpointAddress = 1 | USB_DIR_OUT,
        .bmAttributes = USB_ENDPOINT_XFER_BULK,
        .wMaxPacketSize = HDC_HSPKT_SIZE_MAX,
    },
    .sink = {
        .bLength = sizeof(hs_descriptors.sink),
        .bDescriptorType = USB_DT_ENDPOINT,
        .bEndpointAddress = 2 | USB_DIR_IN,
        .bmAttributes = USB_ENDPOINT_XFER_BULK,
        .wMaxPacketSize = HDC_HSPKT_SIZE_MAX,
    },
};

static const struct {
    struct usb_functionfs_descs_head_v2 header;
    // The rest of the structure depends on the flags in the header.
    __le32 fs_count;
    __le32 hs_count;
    __le32 ss_count;
    __le32 os_count;
    struct UsbFunctionDesc fs_descs, hs_descs;
    struct ss_func_desc ss_descs;
    struct usb_os_desc_header os_header;
    struct usb_ext_compat_desc os_desc;
} __attribute__((packed)) USB_FFS_DESC = {
    .header =
    {
        .magic = CPU_TO_LE32(FUNCTIONFS_DESCRIPTORS_MAGIC_V2),
        .length = CPU_TO_LE32(sizeof(USB_FFS_DESC)),
        .flags = FUNCTIONFS_HAS_FS_DESC | FUNCTIONFS_HAS_HS_DESC |
                                 FUNCTIONFS_HAS_SS_DESC | FUNCTIONFS_HAS_MS_OS_DESC
    },
    .fs_count = 3,
    .hs_count = 3,
    .ss_count = 5,
    .os_count = 1,
    .fs_descs = fs_descriptors,
    .hs_descs = hs_descriptors,
    .ss_descs = ss_descriptors,
    .os_header = {
            .interface = 0,
            .dwLength = CPU_TO_LE32(sizeof(USB_FFS_DESC.os_header) + sizeof(USB_FFS_DESC.os_desc)),
            .bcdVersion = CPU_TO_LE16(1),
            .wIndex = CPU_TO_LE16(4),
            .bCount = 1,
            .Reserved = 0,
        },
    .os_desc = {
            .bFirstInterfaceNumber = 0,
            .Reserved1 = 1,
            .CompatibleID = { 'W', 'I', 'N', 'U', 'S', 'B', '\0', '\0'},
            .SubCompatibleID = {0},
            .Reserved2 = {0},
        }
};
}  // namespace Hdc
#endif
