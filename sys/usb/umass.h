/*
 * Copyright (c) 2026 ReBSD contributors
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 */

#ifndef _USB_UMASS_H_
#define _USB_UMASS_H_

#include <sys/types.h>
#include <usb/umassvar.h>

#define UMASS_INQUIRY_LENGTH        36u
#define UMASS_SENSE_LENGTH          18u

struct usb_core;

struct umass_media {
    struct umass_bbb *um_bbb;
    unsigned um_sector_count;
    unsigned um_sector_size;
    unsigned um_cache_mode_valid;
    unsigned um_write_cache_enabled;
    unsigned um_no_sync_cache;
    uByte um_inquiry[UMASS_INQUIRY_LENGTH];
    uByte um_sense[UMASS_SENSE_LENGTH];
};

enum umass_bbb_result umass_scsi_inquiry(struct umass_bbb *, void *,
    size_t, size_t *);
enum umass_bbb_result umass_scsi_test_unit_ready(struct umass_bbb *);
enum umass_bbb_result umass_scsi_request_sense(struct umass_bbb *, void *,
    size_t, size_t *);
enum umass_bbb_result umass_scsi_read_capacity(struct umass_bbb *,
    unsigned *, unsigned *);
enum umass_bbb_result umass_scsi_read_10(struct umass_bbb *, unsigned,
    unsigned, void *);
enum umass_bbb_result umass_scsi_write_10(struct umass_bbb *, unsigned,
    unsigned, const void *);
enum umass_bbb_result umass_scsi_synchronize_cache_10(struct umass_bbb *);

void umass_media_init(struct umass_media *, struct umass_bbb *);
enum umass_bbb_result umass_media_probe(struct umass_media *);
enum umass_bbb_result umass_media_read(struct umass_media *, unsigned,
    unsigned, void *);
enum umass_bbb_result umass_media_write(struct umass_media *, unsigned,
    unsigned, const void *);
enum umass_bbb_result umass_media_flush(struct umass_media *);

#ifdef KERNEL
usb_error_t umass_register(struct usb_core *);
void umassattach(int);
#endif

#endif /* _USB_UMASS_H_ */
