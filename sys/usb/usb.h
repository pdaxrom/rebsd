/*	$NetBSD: usb.h,v 1.71 2004/06/23 06:27:54 mycroft Exp $	*/

/*
 * Copyright (c) 1998 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Lennart Augustsson (lennart@augustsson.net) at
 * Carlstedt Research & Technology.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *        This product includes software developed by the NetBSD
 *        Foundation, Inc. and its contributors.
 * 4. Neither the name of The NetBSD Foundation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _USB_USB_H_
#define _USB_USB_H_

#define USB_MAX_ADDRESS        127
#define USB_CONTROL_ENDPOINT   0
#define USB_MAX_ENDPOINT_NUMBER 15
#define USB_FRAMES_PER_SECOND  1000

/*
 * USB multi-byte values are unaligned and little-endian.  Keep them as byte
 * arrays and always use these accessors, including on little-endian targets.
 */
typedef unsigned char uByte;
typedef unsigned char uWord[2];
typedef unsigned char uDWord[4];

#define UGETW(w) \
    ((unsigned int)(w)[0] | ((unsigned int)(w)[1] << 8))
#define USETW(w, v) do { \
    (w)[0] = (uByte)(v); \
    (w)[1] = (uByte)((unsigned int)(v) >> 8); \
} while (0)
#define USETW2(w, h, l) do { \
    (w)[0] = (uByte)(l); \
    (w)[1] = (uByte)(h); \
} while (0)
#define UGETDW(w) \
    ((unsigned int)(w)[0] | ((unsigned int)(w)[1] << 8) | \
    ((unsigned int)(w)[2] << 16) | ((unsigned int)(w)[3] << 24))
#define USETDW(w, v) do { \
    (w)[0] = (uByte)(v); \
    (w)[1] = (uByte)((unsigned int)(v) >> 8); \
    (w)[2] = (uByte)((unsigned int)(v) >> 16); \
    (w)[3] = (uByte)((unsigned int)(v) >> 24); \
} while (0)

#define UPACKED __attribute__((__packed__))

typedef struct {
    uByte bmRequestType;
    uByte bRequest;
    uWord wValue;
    uWord wIndex;
    uWord wLength;
} UPACKED usb_device_request_t;

#define USB_DEVICE_REQUEST_SIZE 8

#define UT_WRITE                0x00
#define UT_READ                 0x80
#define UT_STANDARD             0x00
#define UT_CLASS                0x20
#define UT_VENDOR               0x40
#define UT_DEVICE               0x00
#define UT_INTERFACE            0x01
#define UT_ENDPOINT             0x02
#define UT_OTHER                0x03

#define UT_READ_DEVICE          (UT_READ | UT_STANDARD | UT_DEVICE)
#define UT_READ_INTERFACE       (UT_READ | UT_STANDARD | UT_INTERFACE)
#define UT_READ_ENDPOINT        (UT_READ | UT_STANDARD | UT_ENDPOINT)
#define UT_WRITE_DEVICE         (UT_WRITE | UT_STANDARD | UT_DEVICE)
#define UT_WRITE_INTERFACE      (UT_WRITE | UT_STANDARD | UT_INTERFACE)
#define UT_WRITE_ENDPOINT       (UT_WRITE | UT_STANDARD | UT_ENDPOINT)
#define UT_READ_CLASS_DEVICE    (UT_READ | UT_CLASS | UT_DEVICE)
#define UT_READ_CLASS_INTERFACE (UT_READ | UT_CLASS | UT_INTERFACE)
#define UT_READ_CLASS_OTHER     (UT_READ | UT_CLASS | UT_OTHER)
#define UT_WRITE_CLASS_DEVICE   (UT_WRITE | UT_CLASS | UT_DEVICE)
#define UT_WRITE_CLASS_INTERFACE (UT_WRITE | UT_CLASS | UT_INTERFACE)
#define UT_WRITE_CLASS_OTHER    (UT_WRITE | UT_CLASS | UT_OTHER)

#define UR_GET_STATUS           0x00
#define UR_CLEAR_FEATURE        0x01
#define UR_SET_FEATURE          0x03
#define UR_SET_ADDRESS          0x05
#define UR_GET_DESCRIPTOR       0x06
#define UR_SET_DESCRIPTOR       0x07
#define UR_GET_CONFIG           0x08
#define UR_SET_CONFIG           0x09
#define UR_GET_INTERFACE        0x0a
#define UR_SET_INTERFACE        0x0b
#define UR_SYNCH_FRAME          0x0c

#define UDESC_DEVICE            0x01
#define UDESC_CONFIG            0x02
#define UDESC_STRING            0x03
#define UDESC_INTERFACE         0x04
#define UDESC_ENDPOINT          0x05
#define UDESC_DEVICE_QUALIFIER  0x06
#define UDESC_OTHER_SPEED_CONFIG 0x07
#define UDESC_INTERFACE_POWER   0x08
#define UDESC_OTG               0x09
#define UDESC_HID               0x21
#define UDESC_CS_DEVICE         0x21
#define UDESC_CS_CONFIG         0x22
#define UDESC_CS_STRING         0x23
#define UDESC_CS_INTERFACE      0x24
#define UDESC_CS_ENDPOINT       0x25
#define UDESC_HUB               0x29

#define UF_ENDPOINT_HALT        0
#define UF_DEVICE_REMOTE_WAKEUP 1
#define UF_TEST_MODE            2

#define USB_MAX_IPACKET         8
#define USB_2_MAX_CTRL_PACKET   64
#define USB_2_MAX_BULK_PACKET   512

typedef struct {
    uByte bLength;
    uByte bDescriptorType;
} UPACKED usb_descriptor_t;

#define USB_DESCRIPTOR_SIZE 2

typedef struct {
    uByte bLength;
    uByte bDescriptorType;
    uWord bcdUSB;
    uByte bDeviceClass;
    uByte bDeviceSubClass;
    uByte bDeviceProtocol;
    uByte bMaxPacketSize;
    uWord idVendor;
    uWord idProduct;
    uWord bcdDevice;
    uByte iManufacturer;
    uByte iProduct;
    uByte iSerialNumber;
    uByte bNumConfigurations;
} UPACKED usb_device_descriptor_t;

#define USB_DEVICE_DESCRIPTOR_SIZE 18
#define UD_USB_2_0              0x0200
#define UD_IS_USB2(d)           (UGETW((d)->bcdUSB) >= UD_USB_2_0)

typedef struct {
    uByte bLength;
    uByte bDescriptorType;
    uWord wTotalLength;
    uByte bNumInterface;
    uByte bConfigurationValue;
    uByte iConfiguration;
    uByte bmAttributes;
    uByte bMaxPower;
} UPACKED usb_config_descriptor_t;

#define USB_CONFIG_DESCRIPTOR_SIZE 9
#define UC_BUS_POWERED          0x80
#define UC_SELF_POWERED         0x40
#define UC_REMOTE_WAKEUP        0x20
#define UC_POWER_FACTOR         2

typedef struct {
    uByte bLength;
    uByte bDescriptorType;
    uByte bInterfaceNumber;
    uByte bAlternateSetting;
    uByte bNumEndpoints;
    uByte bInterfaceClass;
    uByte bInterfaceSubClass;
    uByte bInterfaceProtocol;
    uByte iInterface;
} UPACKED usb_interface_descriptor_t;

#define USB_INTERFACE_DESCRIPTOR_SIZE 9

typedef struct {
    uByte bLength;
    uByte bDescriptorType;
    uByte bEndpointAddress;
    uByte bmAttributes;
    uWord wMaxPacketSize;
    uByte bInterval;
} UPACKED usb_endpoint_descriptor_t;

#define USB_ENDPOINT_DESCRIPTOR_SIZE 7
#define UE_DIR_IN               0x80
#define UE_DIR_OUT              0x00
#define UE_ADDR                 0x0f
#define UE_GET_DIR(a)           ((a) & UE_DIR_IN)
#define UE_GET_ADDR(a)          ((a) & UE_ADDR)
#define UE_XFERTYPE             0x03
#define UE_CONTROL              0x00
#define UE_ISOCHRONOUS          0x01
#define UE_BULK                 0x02
#define UE_INTERRUPT            0x03
#define UE_GET_XFERTYPE(a)      ((a) & UE_XFERTYPE)

typedef struct {
    uByte bLength;
    uByte bDescriptorType;
    uWord bString[127];
} UPACKED usb_string_descriptor_t;

#define USB_MAX_STRING_LEN      128
#define USB_LANGUAGE_TABLE      0

/* Hub requests and features. */
#define UR_GET_BUS_STATE        0x02
#define UR_CLEAR_TT_BUFFER      0x08
#define UR_RESET_TT             0x09
#define UR_GET_TT_STATE         0x0a
#define UR_STOP_TT              0x0b

#define UHF_C_HUB_LOCAL_POWER   0
#define UHF_C_HUB_OVER_CURRENT  1
#define UHF_PORT_CONNECTION     0
#define UHF_PORT_ENABLE         1
#define UHF_PORT_SUSPEND        2
#define UHF_PORT_OVER_CURRENT   3
#define UHF_PORT_RESET          4
#define UHF_PORT_POWER          8
#define UHF_PORT_LOW_SPEED      9
#define UHF_C_PORT_CONNECTION   16
#define UHF_C_PORT_ENABLE       17
#define UHF_C_PORT_SUSPEND      18
#define UHF_C_PORT_OVER_CURRENT 19
#define UHF_C_PORT_RESET        20

typedef struct {
    uByte bDescLength;
    uByte bDescriptorType;
    uByte bNbrPorts;
    uWord wHubCharacteristics;
    uByte bPwrOn2PwrGood;
    uByte bHubContrCurrent;
    uByte DeviceRemovable[32];
    uByte PortPowerCtrlMask[1];
} UPACKED usb_hub_descriptor_t;

#define USB_HUB_DESCRIPTOR_SIZE 9
#define UHD_PWR                 0x0003
#define UHD_PWR_GANGED          0x0000
#define UHD_PWR_INDIVIDUAL      0x0001
#define UHD_PWR_NO_SWITCH       0x0002
#define UHD_OC                  0x0018
#define UHD_OC_GLOBAL           0x0000
#define UHD_OC_INDIVIDUAL       0x0008
#define UHD_OC_NONE             0x0010
#define UHD_PWRON_FACTOR        2

typedef struct {
    uWord wStatus;
} UPACKED usb_status_t;

#define UDS_SELF_POWERED        0x0001
#define UDS_REMOTE_WAKEUP       0x0002
#define UES_HALT                0x0001

typedef struct {
    uWord wHubStatus;
    uWord wHubChange;
} UPACKED usb_hub_status_t;

#define UHS_LOCAL_POWER         0x0001
#define UHS_OVER_CURRENT        0x0002

typedef struct {
    uWord wPortStatus;
    uWord wPortChange;
} UPACKED usb_port_status_t;

#define UPS_CURRENT_CONNECT_STATUS 0x0001
#define UPS_PORT_ENABLED        0x0002
#define UPS_SUSPEND             0x0004
#define UPS_OVERCURRENT_INDICATOR 0x0008
#define UPS_RESET               0x0010
#define UPS_PORT_POWER          0x0100
#define UPS_LOW_SPEED           0x0200
#define UPS_HIGH_SPEED          0x0400
#define UPS_C_CONNECT_STATUS    0x0001
#define UPS_C_PORT_ENABLED      0x0002
#define UPS_C_SUSPEND           0x0004
#define UPS_C_OVERCURRENT_INDICATOR 0x0008
#define UPS_C_PORT_RESET        0x0010

/* Device and interface classes needed by the initial port. */
#define UDCLASS_IN_INTERFACE    0x00
#define UDCLASS_HUB             0x09
#define UDCLASS_VENDOR          0xff

#define UICLASS_UNSPEC          0x00
#define UICLASS_HID             0x03
#define UISUBCLASS_BOOT         0x01
#define UIPROTO_BOOT_KEYBOARD   0x01
#define UIPROTO_BOOT_MOUSE      0x02
#define UICLASS_MASS            0x08
#define UISUBCLASS_SCSI         0x06
#define UIPROTO_MASS_BBB        0x50
#define UICLASS_HUB             0x09
#define UISUBCLASS_HUB          0x00
#define UIPROTO_FSHUB           0x00
#define UIPROTO_HSHUBMTT        0x01
#define UICLASS_VENDOR          0xff

#define USB_CLASS_HID           UICLASS_HID
#define USB_SUBCLASS_BOOT       UISUBCLASS_BOOT
#define USB_PROTOCOL_KEYBOARD   UIPROTO_BOOT_KEYBOARD
#define USB_PROTOCOL_MOUSE      UIPROTO_BOOT_MOUSE
#define USB_CLASS_MASS_STORAGE  UICLASS_MASS
#define USB_CLASS_HUB           UICLASS_HUB

#define USB_SPEED_UNKNOWN       0
#define USB_SPEED_LOW           1
#define USB_SPEED_FULL          2
#define USB_SPEED_HIGH          3

#define USB_HUB_MAX_DEPTH       5
#define USB_PORT_RESET_DELAY    50
#define USB_PORT_ROOT_RESET_DELAY 250
#define USB_PORT_RESET_RECOVERY 250
#define USB_PORT_POWERUP_DELAY  300
#define USB_SET_ADDRESS_SETTLE  10

#endif /* _USB_USB_H_ */
