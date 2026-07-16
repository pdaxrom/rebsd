/*	$NetBSD: umassvar.h,v 1.23 2004/03/22 14:55:42 tls Exp $	*/
/*-
 * Copyright (c) 1999 MAEKAWA Masahide <bishop@rr.iij4u.or.jp>,
 *		      Nick Hibma <n_hibma@freebsd.org>
 * All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *     $FreeBSD: src/sys/dev/usb/umass.c,v 1.13 2000/03/26 01:39:12 n_hibma Exp $
 */

#ifndef _USB_UMASSVAR_H_
#define _USB_UMASSVAR_H_

#include <usb/usbdi.h>

#define UMASS_BBB_RESET             0xff
#define UMASS_BBB_GET_MAX_LUN       0xfe

#define UMASS_BBB_CBW_SIGNATURE     0x43425355u
#define UMASS_BBB_CSW_SIGNATURE     0x53425355u
#define UMASS_BBB_CBW_SIZE          31u
#define UMASS_BBB_CSW_SIZE          13u
#define UMASS_BBB_CDB_MAX           16u

#define UMASS_BBB_CBW_OUT           0x00u
#define UMASS_BBB_CBW_IN            0x80u

#define UMASS_BBB_CSW_GOOD          0u
#define UMASS_BBB_CSW_FAILED        1u
#define UMASS_BBB_CSW_PHASE         2u

#define UMASS_DIR_NONE              0u
#define UMASS_DIR_IN                1u
#define UMASS_DIR_OUT               2u

#define UMASS_SECTOR_SIZE           512u
#define UMASS_MAX_TRANSFER          (128u * 1024u)
#define UMASS_MAX_IO_SECTORS        (UMASS_MAX_TRANSFER / UMASS_SECTOR_SIZE)
#define UMASS_MAX_READ_SECTORS      UMASS_MAX_IO_SECTORS
#define UMASS_MAX_WRITE_SECTORS     UMASS_MAX_IO_SECTORS
#define UMASS_COMMAND_TIMEOUT_MS    5000u
#define UMASS_READ_TIMEOUT_MS       10000u
#define UMASS_WRITE_TIMEOUT_MS      10000u
#define UMASS_FLUSH_TIMEOUT_MS      100000u

struct umass_bbb_cbw {
    uDWord dCBWSignature;
    uDWord dCBWTag;
    uDWord dCBWDataTransferLength;
    uByte bCBWFlags;
    uByte bCBWLUN;
    uByte bCDBLength;
    uByte CBWCDB[UMASS_BBB_CDB_MAX];
} UPACKED;

struct umass_bbb_csw {
    uDWord dCSWSignature;
    uDWord dCSWTag;
    uDWord dCSWDataResidue;
    uByte bCSWStatus;
} UPACKED;

typedef usb_error_t (*umass_bulk_fn_t)(void *, unsigned, void *, size_t,
    unsigned, unsigned, size_t *);
typedef usb_error_t (*umass_control_fn_t)(void *,
    const usb_device_request_t *, void *, size_t, unsigned, size_t *);
typedef usb_error_t (*umass_clear_halt_fn_t)(void *, unsigned);

struct umass_bbb_ops {
    umass_bulk_fn_t ubo_bulk;
    umass_control_fn_t ubo_control;
    umass_clear_halt_fn_t ubo_clear_halt;
};

struct umass_bbb {
    const struct umass_bbb_ops *ub_ops;
    void *ub_arg;
    unsigned ub_interface_number;
    unsigned ub_next_tag;
    usb_error_t ub_last_error;
};

enum umass_bbb_result {
    UMASS_BBB_OK = 0,
    UMASS_BBB_COMMAND_FAILED,
    UMASS_BBB_WIRE_FAILED
};

void umass_bbb_init(struct umass_bbb *, const struct umass_bbb_ops *,
    void *, unsigned);
enum umass_bbb_result umass_bbb_transfer(struct umass_bbb *, unsigned,
    const void *, size_t, void *, size_t, unsigned, unsigned, size_t *,
    unsigned *);

#endif /* _USB_UMASSVAR_H_ */
