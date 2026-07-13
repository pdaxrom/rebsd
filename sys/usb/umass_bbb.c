/*	$NetBSD: umass.c,v 1.117 2004/12/28 23:35:21 nathanw Exp $	*/

/*
 * Copyright (c) 2003 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Charles M. Hannum.
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

/*
 * Compact synchronous Bulk-Only Transport derived from the BBB state and
 * reset-recovery rules in the classic NetBSD driver.  ReBSD deliberately
 * omits CBI/UFI, multiple LUNs, quirks, SCSIPI, and asynchronous xfer pools.
 */

#include <usb/umassvar.h>

typedef char umass_cbw_size_check[
    sizeof(struct umass_bbb_cbw) == UMASS_BBB_CBW_SIZE ? 1 : -1];
typedef char umass_csw_size_check[
    sizeof(struct umass_bbb_csw) == UMASS_BBB_CSW_SIZE ? 1 : -1];

static void
umass_zero(void *vptr, size_t length)
{
    uByte *ptr;

    ptr = (uByte *)vptr;
    while (length-- != 0)
        *ptr++ = 0;
}

static void
umass_copy(void *vdst, const void *vsrc, size_t length)
{
    uByte *dst;
    const uByte *src;

    dst = (uByte *)vdst;
    src = (const uByte *)vsrc;
    while (length-- != 0)
        *dst++ = *src++;
}

void
umass_bbb_init(struct umass_bbb *bbb, const struct umass_bbb_ops *ops,
    void *arg, unsigned interface_number)
{
    if (bbb == 0)
        return;
    umass_zero(bbb, sizeof(*bbb));
    bbb->ub_ops = ops;
    bbb->ub_arg = arg;
    bbb->ub_interface_number = interface_number;
    bbb->ub_next_tag = 42u;
    bbb->ub_last_error = USB_STATUS_NORMAL_COMPLETION;
}

static void
umass_bbb_reset_recovery(struct umass_bbb *bbb)
{
    usb_device_request_t request;
    size_t actlen;

    if (bbb->ub_ops == 0)
        return;
    if (bbb->ub_ops->ubo_control != 0) {
        umass_zero(&request, sizeof(request));
        request.bmRequestType = UT_WRITE_CLASS_INTERFACE;
        request.bRequest = UMASS_BBB_RESET;
        USETW(request.wIndex, bbb->ub_interface_number);
        actlen = 0;
        (void)bbb->ub_ops->ubo_control(bbb->ub_arg, &request, 0, 0,
            UMASS_COMMAND_TIMEOUT_MS, &actlen);
    }
    if (bbb->ub_ops->ubo_clear_halt != 0) {
        (void)bbb->ub_ops->ubo_clear_halt(bbb->ub_arg, UMASS_DIR_IN);
        (void)bbb->ub_ops->ubo_clear_halt(bbb->ub_arg, UMASS_DIR_OUT);
    }
}

static enum umass_bbb_result
umass_bbb_wire_failed(struct umass_bbb *bbb, usb_error_t error)
{
    if (error == USB_STATUS_NORMAL_COMPLETION)
        error = USB_STATUS_IO_ERROR;
    bbb->ub_last_error = error;
    if (error != USB_STATUS_DISCONNECTED &&
        error != USB_STATUS_CANCELLED)
        umass_bbb_reset_recovery(bbb);
    return UMASS_BBB_WIRE_FAILED;
}

static usb_error_t
umass_bbb_clear_halt(struct umass_bbb *bbb, unsigned direction)
{
    if (bbb->ub_ops->ubo_clear_halt == 0)
        return USB_STATUS_UNSUPPORTED;
    return bbb->ub_ops->ubo_clear_halt(bbb->ub_arg, direction);
}

enum umass_bbb_result
umass_bbb_transfer(struct umass_bbb *bbb, unsigned lun, const void *cdb,
    size_t cdb_length, void *data, size_t data_length, unsigned direction,
    unsigned timeout_ms, size_t *actlenp, unsigned *residuep)
{
    struct umass_bbb_cbw cbw;
    struct umass_bbb_csw csw;
    usb_error_t error;
    size_t actlen;
    size_t data_actlen;
    unsigned residue;
    unsigned tag;

    if (actlenp != 0)
        *actlenp = 0;
    if (residuep != 0)
        *residuep = 0;
    if (bbb == 0 || bbb->ub_ops == 0 ||
        bbb->ub_ops->ubo_bulk == 0 || cdb == 0 || lun != 0 ||
        cdb_length == 0 || cdb_length > UMASS_BBB_CDB_MAX ||
        data_length > UMASS_MAX_TRANSFER ||
        (data_length != 0 && data == 0) ||
        (data_length == 0 && direction != UMASS_DIR_NONE) ||
        (data_length != 0 && direction != UMASS_DIR_IN &&
        direction != UMASS_DIR_OUT)) {
        if (bbb != 0)
            bbb->ub_last_error = USB_STATUS_INVALID;
        return UMASS_BBB_WIRE_FAILED;
    }
    if (timeout_ms == 0)
        timeout_ms = UMASS_COMMAND_TIMEOUT_MS;

    tag = bbb->ub_next_tag++;
    if (bbb->ub_next_tag == 0)
        bbb->ub_next_tag = 1;
    umass_zero(&cbw, sizeof(cbw));
    USETDW(cbw.dCBWSignature, UMASS_BBB_CBW_SIGNATURE);
    USETDW(cbw.dCBWTag, tag);
    USETDW(cbw.dCBWDataTransferLength, data_length);
    cbw.bCBWFlags = direction == UMASS_DIR_IN ?
        UMASS_BBB_CBW_IN : UMASS_BBB_CBW_OUT;
    cbw.bCBWLUN = 0;
    cbw.bCDBLength = (uByte)cdb_length;
    umass_copy(cbw.CBWCDB, cdb, cdb_length);

    actlen = 0;
    error = bbb->ub_ops->ubo_bulk(bbb->ub_arg, UMASS_DIR_OUT, &cbw,
        sizeof(cbw), 0, timeout_ms, &actlen);
    if (error != USB_STATUS_NORMAL_COMPLETION || actlen != sizeof(cbw))
        return umass_bbb_wire_failed(bbb,
            error == USB_STATUS_NORMAL_COMPLETION ?
            USB_STATUS_SHORT_XFER : error);

    data_actlen = 0;
    if (data_length != 0) {
        error = bbb->ub_ops->ubo_bulk(bbb->ub_arg, direction, data,
            data_length, direction == UMASS_DIR_IN ?
            USB_XFER_SHORT_OK : 0, timeout_ms, &data_actlen);
        if (error == USB_STATUS_STALLED) {
            error = umass_bbb_clear_halt(bbb, direction);
            if (error != USB_STATUS_NORMAL_COMPLETION)
                return umass_bbb_wire_failed(bbb, error);
        } else if (error != USB_STATUS_NORMAL_COMPLETION) {
            return umass_bbb_wire_failed(bbb, error);
        }
    }

    umass_zero(&csw, sizeof(csw));
    actlen = 0;
    error = bbb->ub_ops->ubo_bulk(bbb->ub_arg, UMASS_DIR_IN, &csw,
        sizeof(csw), 0, timeout_ms, &actlen);
    if (error == USB_STATUS_STALLED) {
        error = umass_bbb_clear_halt(bbb, UMASS_DIR_IN);
        if (error != USB_STATUS_NORMAL_COMPLETION)
            return umass_bbb_wire_failed(bbb, error);
        umass_zero(&csw, sizeof(csw));
        actlen = 0;
        error = bbb->ub_ops->ubo_bulk(bbb->ub_arg, UMASS_DIR_IN, &csw,
            sizeof(csw), 0, timeout_ms, &actlen);
    }
    if (error != USB_STATUS_NORMAL_COMPLETION || actlen != sizeof(csw))
        return umass_bbb_wire_failed(bbb,
            error == USB_STATUS_NORMAL_COMPLETION ?
            USB_STATUS_SHORT_XFER : error);

    residue = UGETDW(csw.dCSWDataResidue);
    if (UGETDW(csw.dCSWSignature) != UMASS_BBB_CSW_SIGNATURE ||
        UGETDW(csw.dCSWTag) != tag ||
        csw.bCSWStatus > UMASS_BBB_CSW_PHASE ||
        residue > data_length ||
        csw.bCSWStatus == UMASS_BBB_CSW_PHASE)
        return umass_bbb_wire_failed(bbb, USB_STATUS_IO_ERROR);

    bbb->ub_last_error = USB_STATUS_NORMAL_COMPLETION;
    if (actlenp != 0)
        *actlenp = data_actlen;
    if (residuep != 0)
        *residuep = residue;
    if (csw.bCSWStatus == UMASS_BBB_CSW_FAILED)
        return UMASS_BBB_COMMAND_FAILED;
    return UMASS_BBB_OK;
}
