/*	$NetBSD: ohcireg.h,v 1.19 2002/07/11 21:14:27 augustss Exp $	*/

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

#ifndef _DEV_USB_OHCIREG_H_
#define _DEV_USB_OHCIREG_H_

#define OHCI_REVISION               0x00
#define OHCI_REV_LO(v)              ((v) & 0x0f)
#define OHCI_REV_HI(v)              (((v) >> 4) & 0x0f)
#define OHCI_REV_LEGACY(v)          ((v) & 0x100)

#define OHCI_CONTROL                0x04
#define OHCI_CBSR_MASK              0x00000003u
#define OHCI_RATIO_1_4              0x00000003u
#define OHCI_PLE                    0x00000004u
#define OHCI_IE                     0x00000008u
#define OHCI_CLE                    0x00000010u
#define OHCI_BLE                    0x00000020u
#define OHCI_HCFS_MASK              0x000000c0u
#define OHCI_HCFS_RESET             0x00000000u
#define OHCI_HCFS_RESUME            0x00000040u
#define OHCI_HCFS_OPERATIONAL       0x00000080u
#define OHCI_HCFS_SUSPEND           0x000000c0u
#define OHCI_IR                     0x00000100u

#define OHCI_COMMAND_STATUS         0x08
#define OHCI_HCR                    0x00000001u
#define OHCI_CLF                    0x00000002u
#define OHCI_BLF                    0x00000004u
#define OHCI_OCR                    0x00000008u

#define OHCI_INTERRUPT_STATUS       0x0c
#define OHCI_INTERRUPT_ENABLE       0x10
#define OHCI_INTERRUPT_DISABLE      0x14
#define OHCI_SO                     0x00000001u
#define OHCI_WDH                    0x00000002u
#define OHCI_SF                     0x00000004u
#define OHCI_RD                     0x00000008u
#define OHCI_UE                     0x00000010u
#define OHCI_FNO                    0x00000020u
#define OHCI_RHSC                   0x00000040u
#define OHCI_OC                     0x40000000u
#define OHCI_MIE                    0x80000000u
#define OHCI_ALL_INTRS              0x4000007fu

#define OHCI_HCCA                   0x18
#define OHCI_PERIOD_CURRENT_ED      0x1c
#define OHCI_CONTROL_HEAD_ED        0x20
#define OHCI_CONTROL_CURRENT_ED     0x24
#define OHCI_BULK_HEAD_ED           0x28
#define OHCI_BULK_CURRENT_ED        0x2c
#define OHCI_DONE_HEAD              0x30
#define OHCI_FM_INTERVAL            0x34
#define OHCI_FM_REMAINING           0x38
#define OHCI_FM_NUMBER              0x3c
#define OHCI_PERIODIC_START         0x40
#define OHCI_LS_THRESHOLD           0x44
#define OHCI_RH_DESCRIPTOR_A        0x48
#define OHCI_RH_DESCRIPTOR_B        0x4c
#define OHCI_RH_STATUS              0x50
#define OHCI_RH_PORT_STATUS(n)      (0x50 + (n) * 4)

#define OHCI_GET_IVAL(v)            ((v) & 0x3fffu)
#define OHCI_FIT                    0x80000000u
#define OHCI_FSMPS(i)               ((((i) - 210u) * 6u / 7u) << 16)
#define OHCI_PERIODIC(i)            ((i) * 9u / 10u)
#define OHCI_DEFAULT_FI             0x2edfu
#define OHCI_DEFAULT_LS_THRESHOLD   0x0628u

#define OHCI_GET_NDP(v)             ((v) & 0xffu)
#define OHCI_PSM                    0x00000100u
#define OHCI_NPS                    0x00000200u
#define OHCI_NOCP                   0x00001000u
#define OHCI_POTPGT(v)              ((v) >> 24)

#define OHCI_LPS                    0x00000001u
#define OHCI_OCI                    0x00000002u
#define OHCI_DRWE                   0x00008000u
#define OHCI_LPSC                   0x00010000u
#define OHCI_CCIC                   0x00020000u
#define OHCI_CRWE                   0x80000000u

#define OHCI_RHPS_CCS               0x00000001u
#define OHCI_RHPS_PES               0x00000002u
#define OHCI_RHPS_PSS               0x00000004u
#define OHCI_RHPS_POCI              0x00000008u
#define OHCI_RHPS_PRS               0x00000010u
#define OHCI_RHPS_PPS               0x00000100u
#define OHCI_RHPS_LSDA              0x00000200u
#define OHCI_RHPS_CSC               0x00010000u
#define OHCI_RHPS_PESC              0x00020000u
#define OHCI_RHPS_PSSC              0x00040000u
#define OHCI_RHPS_OCIC              0x00080000u
#define OHCI_RHPS_PRSC              0x00100000u
#define OHCI_RHPS_CPP               0x00000200u
#define OHCI_RHPS_SPP               0x00000100u
#define OHCI_RHPS_CPE               0x00000001u
#define OHCI_RHPS_SPE               0x00000002u
#define OHCI_RHPS_SPRS              0x00000010u

#define OHCI_HCCA_ALIGN             256u
#define OHCI_HCCA_SIZE              256u
#define OHCI_NO_INTRS               32u

struct ohci_hcca {
    unsigned int hcca_interrupt_table[OHCI_NO_INTRS];
    unsigned short hcca_frame_number;
    unsigned short hcca_pad1;
    unsigned int hcca_done_head;
    unsigned char hcca_reserved[120];
};

#define OHCI_ED_ALIGN               16u
struct ohci_ed {
    unsigned int ed_flags;
    unsigned int ed_tailp;
    unsigned int ed_headp;
    unsigned int ed_nexted;
};

#define OHCI_ED_SET_FA(v)           ((v) & 0x7fu)
#define OHCI_ED_SET_EN(v)           (((v) & 0x0fu) << 7)
#define OHCI_ED_DIR_TD              0x00000000u
#define OHCI_ED_DIR_OUT             0x00000800u
#define OHCI_ED_DIR_IN              0x00001000u
#define OHCI_ED_SPEED               0x00002000u
#define OHCI_ED_SKIP                0x00004000u
#define OHCI_ED_FORMAT_GEN          0x00000000u
#define OHCI_ED_SET_MAXP(v)         (((v) & 0x07ffu) << 16)
#define OHCI_ED_HEADMASK            0xfffffffcu
#define OHCI_ED_HALTED              0x00000001u
#define OHCI_ED_TOGGLE_CARRY        0x00000002u

#define OHCI_TD_ALIGN               16u
struct ohci_td {
    unsigned int td_flags;
    unsigned int td_cbp;
    unsigned int td_nexttd;
    unsigned int td_be;
};

#define OHCI_TD_R                   0x00040000u
#define OHCI_TD_SETUP               0x00000000u
#define OHCI_TD_OUT                 0x00080000u
#define OHCI_TD_IN                  0x00100000u
#define OHCI_TD_NOINTR              0x00e00000u
#define OHCI_TD_TOGGLE_CARRY        0x00000000u
#define OHCI_TD_TOGGLE_0            0x02000000u
#define OHCI_TD_TOGGLE_1            0x03000000u
#define OHCI_TD_NOCC                0xf0000000u
#define OHCI_TD_GET_CC(v)           ((v) >> 28)

#define OHCI_CC_NO_ERROR            0u
#define OHCI_CC_CRC                 1u
#define OHCI_CC_BIT_STUFFING        2u
#define OHCI_CC_DATA_TOGGLE         3u
#define OHCI_CC_STALL               4u
#define OHCI_CC_NOT_RESPONDING      5u
#define OHCI_CC_PID_CHECK           6u
#define OHCI_CC_UNEXPECTED_PID      7u
#define OHCI_CC_DATA_OVERRUN        8u
#define OHCI_CC_DATA_UNDERRUN       9u
#define OHCI_CC_BUFFER_OVERRUN      12u
#define OHCI_CC_BUFFER_UNDERRUN     13u
#define OHCI_CC_NOT_ACCESSED        15u

#endif /* _DEV_USB_OHCIREG_H_ */
