/*	$NetBSD: ehcireg.h,v 1.20.2.2 2005/12/07 19:15:05 riz Exp $	*/

/*
 * Copyright (c) 2001, 2004 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Lennart Augustsson (lennart@augustsson.net).
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

#ifndef _USB_EHCIREG_H_
#define _USB_EHCIREG_H_

/* EHCI capability registers. */
#define EHCI_CAPLENGTH              0x00
#define EHCI_CAP_GET_LENGTH(v)      ((v) & 0xffu)
#define EHCI_CAP_GET_VERSION(v)     (((v) >> 16) & 0xffffu)
#define EHCI_HCSPARAMS              0x04
#define EHCI_HCS_N_CC(v)            (((v) >> 12) & 0x0fu)
#define EHCI_HCS_N_PCC(v)           (((v) >> 8) & 0x0fu)
#define EHCI_HCS_PPC(v)             ((v) & 0x10u)
#define EHCI_HCS_N_PORTS(v)         ((v) & 0x0fu)
#define EHCI_HCCPARAMS              0x08
#define EHCI_HCC_64BIT(v)           ((v) & 0x01u)

/* Operational registers, relative to CAPLENGTH. */
#define EHCI_USBCMD                 0x00
#define EHCI_CMD_ITC_2              0x00020000u
#define EHCI_CMD_ASE                0x00000020u
#define EHCI_CMD_PSE                0x00000010u
#define EHCI_CMD_FLS_M              0x0000000cu
#define EHCI_CMD_HCRESET            0x00000002u
#define EHCI_CMD_RS                 0x00000001u

#define EHCI_USBSTS                 0x04
#define EHCI_STS_ASS                0x00008000u
#define EHCI_STS_PSS                0x00004000u
#define EHCI_STS_HCH                0x00001000u
#define EHCI_STS_IAA                0x00000020u
#define EHCI_STS_HSE                0x00000010u
#define EHCI_STS_FLR                0x00000008u
#define EHCI_STS_PCD                0x00000004u
#define EHCI_STS_ERRINT             0x00000002u
#define EHCI_STS_INT                0x00000001u
#define EHCI_STS_INTRS              0x0000003fu

#define EHCI_USBINTR                0x08
#define EHCI_INTR_HSEE              0x00000010u
#define EHCI_INTR_PCIE              0x00000004u
#define EHCI_INTR_UEIE              0x00000002u
#define EHCI_INTR_UIE               0x00000001u
#define EHCI_TRANSFER_INTRS         (EHCI_INTR_HSEE | EHCI_INTR_UEIE | \
    EHCI_INTR_UIE)

#define EHCI_FRINDEX                0x0c
#define EHCI_CTRLDSSEGMENT          0x10
#define EHCI_PERIODICLISTBASE       0x14
#define EHCI_ASYNCLISTADDR          0x18
#define EHCI_CONFIGFLAG             0x40
#define EHCI_CONF_CF                0x00000001u
#define EHCI_PORTSC(n)              (0x40u + 4u * (n))
#define EHCI_PS_PO                  0x00002000u
#define EHCI_PS_PP                  0x00001000u
#define EHCI_PS_LS                  0x00000c00u
#define EHCI_PS_IS_LOWSPEED(v)      (((v) & EHCI_PS_LS) == 0x00000400u)
#define EHCI_PS_PR                  0x00000100u
#define EHCI_PS_SUSP                0x00000080u
#define EHCI_PS_OCC                 0x00000020u
#define EHCI_PS_OCA                 0x00000010u
#define EHCI_PS_PEC                 0x00000008u
#define EHCI_PS_PE                  0x00000004u
#define EHCI_PS_CSC                 0x00000002u
#define EHCI_PS_CS                  0x00000001u
#define EHCI_PS_CLEAR               (EHCI_PS_OCC | EHCI_PS_PEC | \
    EHCI_PS_CSC)
#define EHCI_PORT_RESET_COMPLETE_MS 2u
#define EHCI_MAX_PORTS              16u

#define EHCI_PAGE_SIZE              0x1000u
#define EHCI_PAGE(v)                ((v) & ~0xfffu)

typedef unsigned int ehci_link_t;
#define EHCI_LINK_TERMINATE         0x00000001u
#define EHCI_LINK_QH                0x00000002u
#define EHCI_LINK_ADDR(v)           ((v) & ~0x1fu)

#define EHCI_QTD_NBUFFERS           5u
struct ehci_qtd {
    ehci_link_t qtd_next;
    ehci_link_t qtd_altnext;
    unsigned int qtd_status;
    unsigned int qtd_buffer[EHCI_QTD_NBUFFERS];
    unsigned int qtd_buffer_hi[EHCI_QTD_NBUFFERS];
    /* Keep adjacent fixed-pool qTDs on the required 32-byte boundary. */
    unsigned int qtd_pad[3];
};

#define EHCI_QTD_ALIGN              32u
#define EHCI_QTD_ACTIVE             0x00000080u
#define EHCI_QTD_HALTED             0x00000040u
#define EHCI_QTD_BUFERR             0x00000020u
#define EHCI_QTD_BABBLE             0x00000010u
#define EHCI_QTD_XACTERR            0x00000008u
#define EHCI_QTD_MISSEDMICRO        0x00000004u
#define EHCI_QTD_STATERRS           0x0000003cu
#define EHCI_QTD_SET_PID(v)         ((v) << 8)
#define EHCI_QTD_PID_OUT            0u
#define EHCI_QTD_PID_IN             1u
#define EHCI_QTD_PID_SETUP          2u
#define EHCI_QTD_SET_CERR(v)        ((v) << 10)
#define EHCI_QTD_IOC                0x00008000u
#define EHCI_QTD_GET_BYTES(v)       (((v) >> 16) & 0x7fffu)
#define EHCI_QTD_SET_BYTES(v)       ((v) << 16)
#define EHCI_QTD_GET_TOGGLE(v)      (((v) >> 31) & 1u)
#define EHCI_QTD_SET_TOGGLE(v)      ((v) << 31)

struct ehci_qh {
    ehci_link_t qh_link;
    unsigned int qh_endp;
    unsigned int qh_endphub;
    unsigned int qh_curqtd;
    struct ehci_qtd qh_qtd;
    /* Keep adjacent fixed-pool QHs on the required 32-byte boundary. */
    unsigned int qh_pad[4];
};

#define EHCI_QH_ALIGN               32u
#define EHCI_QH_SET_ADDR(v)         ((v) & 0x7fu)
#define EHCI_QH_SET_ENDPT(v)        (((v) & 0x0fu) << 8)
#define EHCI_QH_SET_EPS(v)          (((v) & 0x03u) << 12)
#define EHCI_QH_SPEED_FULL          0u
#define EHCI_QH_SPEED_LOW           1u
#define EHCI_QH_SPEED_HIGH          2u
#define EHCI_QH_DTC                 0x00004000u
#define EHCI_QH_HRECL               0x00008000u
#define EHCI_QH_SET_MPL(v)          (((v) & 0x07ffu) << 16)
#define EHCI_QH_CTL                 0x08000000u
#define EHCI_QH_SET_NRL(v)          (((v) & 0x0fu) << 28)
#define EHCI_QH_SET_SMASK(v)        ((v) & 0xffu)
#define EHCI_QH_SET_CMASK(v)        (((v) & 0xffu) << 8)
#define EHCI_QH_SET_HUBA(v)         (((v) & 0x7fu) << 16)
#define EHCI_QH_SET_PORT(v)         (((v) & 0x7fu) << 23)
#define EHCI_QH_SET_MULT(v)         (((v) & 0x03u) << 30)

#endif /* _USB_EHCIREG_H_ */
