/*	$NetBSD: uhcireg.h,v 1.22 2016/04/23 10:15:32 skrll Exp $	*/

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
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _USB_UHCIREG_H_
#define _USB_UHCIREG_H_

#define UHCI_CMD                    0x00u
#define UHCI_CMD_RS                 0x0001u
#define UHCI_CMD_HCRESET            0x0002u
#define UHCI_CMD_GRESET             0x0004u
#define UHCI_CMD_EGSM               0x0008u
#define UHCI_CMD_FGR                0x0010u
#define UHCI_CMD_SWDBG              0x0020u
#define UHCI_CMD_CF                 0x0040u
#define UHCI_CMD_MAXP               0x0080u

#define UHCI_STS                    0x02u
#define UHCI_STS_USBINT             0x0001u
#define UHCI_STS_USBEI              0x0002u
#define UHCI_STS_RD                 0x0004u
#define UHCI_STS_HSE                0x0008u
#define UHCI_STS_HCPE               0x0010u
#define UHCI_STS_HCH                0x0020u
#define UHCI_STS_ALL                0x003fu
#define UHCI_STS_ACK                0x001fu

#define UHCI_INTR                   0x04u
#define UHCI_INTR_TOCRCIE           0x0001u
#define UHCI_INTR_RIE               0x0002u
#define UHCI_INTR_IOCE              0x0004u
#define UHCI_INTR_SPIE              0x0008u
#define UHCI_INTR_ALL               0x000fu

#define UHCI_FRNUM                  0x06u
#define UHCI_FRNUM_MASK             0x03ffu
#define UHCI_FLBASEADDR             0x08u
#define UHCI_SOF                    0x0cu

#define UHCI_PORTSC1                0x10u
#define UHCI_PORTSC2                0x12u
#define UHCI_PORTSC(port)           \
    ((port) == 1u ? UHCI_PORTSC1 : UHCI_PORTSC2)
#define UHCI_PORTSC_CCS             0x0001u
#define UHCI_PORTSC_CSC             0x0002u
#define UHCI_PORTSC_PE              0x0004u
#define UHCI_PORTSC_POEDC           0x0008u
#define UHCI_PORTSC_LS_MASK         0x0030u
#define UHCI_PORTSC_RD              0x0040u
#define UHCI_PORTSC_LSDA            0x0100u
#define UHCI_PORTSC_PR              0x0200u
#define UHCI_PORTSC_OCI             0x0400u
#define UHCI_PORTSC_OCIC            0x0800u
#define UHCI_PORTSC_SUSP            0x1000u
#define UHCI_PORTSC_W1C             \
    (UHCI_PORTSC_CSC | UHCI_PORTSC_POEDC | UHCI_PORTSC_OCIC)
#define UHCI_PORTSC_RW              \
    (UHCI_PORTSC_PE | UHCI_PORTSC_RD | UHCI_PORTSC_PR | UHCI_PORTSC_SUSP)

#define UHCI_FRAME_LIST_COUNT       1024u
#define UHCI_FRAME_LIST_ALIGN       4096u

#define UHCI_PTR_T                  0x00000001u
#define UHCI_PTR_TD                 0x00000000u
#define UHCI_PTR_QH                 0x00000002u
#define UHCI_PTR_VF                 0x00000004u
#define UHCI_PTR_MASK               0xfffffff0u

#define UHCI_TD_ALIGN               16u
struct uhci_td {
    volatile unsigned int td_link;
    volatile unsigned int td_status;
    volatile unsigned int td_token;
    volatile unsigned int td_buffer;
};

#define UHCI_TD_ACTLEN_MASK         0x000007ffu
#define UHCI_TD_GET_ACTLEN(v)       (((v) + 1u) & 0x07ffu)
#define UHCI_TD_ZERO_ACTLEN         0x000007ffu
#define UHCI_TD_BITSTUFF            0x00020000u
#define UHCI_TD_CRCTO               0x00040000u
#define UHCI_TD_NAK                 0x00080000u
#define UHCI_TD_BABBLE              0x00100000u
#define UHCI_TD_DBUFFER             0x00200000u
#define UHCI_TD_STALLED             0x00400000u
#define UHCI_TD_ACTIVE              0x00800000u
#define UHCI_TD_IOC                 0x01000000u
#define UHCI_TD_IOS                 0x02000000u
#define UHCI_TD_LS                  0x04000000u
#define UHCI_TD_SET_ERRCNT(v)       (((v) & 3u) << 27)
#define UHCI_TD_SPD                 0x20000000u
#define UHCI_TD_ERROR               \
    (UHCI_TD_BITSTUFF | UHCI_TD_CRCTO | UHCI_TD_BABBLE | \
    UHCI_TD_DBUFFER | UHCI_TD_STALLED)

#define UHCI_TD_PID_IN              0x69u
#define UHCI_TD_PID_OUT             0xe1u
#define UHCI_TD_PID_SETUP           0x2du
#define UHCI_TD_GET_PID(v)          ((v) & 0xffu)
#define UHCI_TD_SET_ADDR(v)         (((v) & 0x7fu) << 8)
#define UHCI_TD_SET_ENDPT(v)        (((v) & 0x0fu) << 15)
#define UHCI_TD_SET_TOGGLE(v)       (((v) & 1u) << 19)
#define UHCI_TD_GET_TOGGLE(v)       (((v) >> 19) & 1u)
#define UHCI_TD_SET_MAXLEN(v)       ((((v) - 1u) & 0x07ffu) << 21)
#define UHCI_TD_GET_MAXLEN(v)       ((((v) >> 21) + 1u) & 0x07ffu)

#define UHCI_QH_ALIGN               16u
struct uhci_qh {
    volatile unsigned int qh_hlink;
    volatile unsigned int qh_elink;
    unsigned int qh_pad[2];
};

#endif /* _USB_UHCIREG_H_ */
