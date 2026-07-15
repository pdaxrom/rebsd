/* Host-side tests for compact USB Mass Storage BOT and SCSI block I/O. */

#include <stdio.h>
#include <string.h>
#include <usb/umass.h>

#define CHECK(expr) do {                                                \
    if (!(expr)) {                                                      \
        fprintf(stderr, "%s:%d: check failed: %s\n",                 \
            __FILE__, __LINE__, #expr);                                 \
        return 1;                                                       \
    }                                                                   \
} while (0)

#define FAKE_SECTORS                64u
#define SCSI_TEST_UNIT_READY        0x00u
#define SCSI_REQUEST_SENSE          0x03u
#define SCSI_INQUIRY                0x12u
#define SCSI_READ_CAPACITY_10       0x25u
#define SCSI_READ_10                0x28u
#define SCSI_WRITE_10               0x2au
#define SCSI_SYNCHRONIZE_CACHE_10   0x35u

struct fake_disk {
    unsigned char storage[FAKE_SECTORS * UMASS_SECTOR_SIZE];
    struct umass_bbb_cbw cbw;
    unsigned sector_size;
    unsigned have_cbw;
    unsigned data_attempted;
    unsigned data_done;
    unsigned tur_failures;
    unsigned bad_tag_once;
    unsigned csw_stall_once;
    unsigned data_stall_once;
    usb_error_t fail_next_bulk;
    unsigned reset_count;
    unsigned clear_in_count;
    unsigned clear_out_count;
    unsigned cbw_count;
    unsigned inquiry_count;
    unsigned tur_count;
    unsigned sense_count;
    unsigned capacity_count;
    unsigned read_count;
    unsigned write_count;
    unsigned sync_count;
    unsigned max_read_sectors;
    unsigned max_write_sectors;
    unsigned write_protected;
    unsigned write_rejected;
    unsigned sync_unsupported;
    unsigned sense_key;
};

static unsigned
get_be16(const unsigned char *data)
{
    return ((unsigned)data[0] << 8) | data[1];
}

static unsigned
get_be32(const unsigned char *data)
{
    return ((unsigned)data[0] << 24) | ((unsigned)data[1] << 16) |
        ((unsigned)data[2] << 8) | data[3];
}

static void
set_be32(unsigned char *data, unsigned value)
{
    data[0] = (unsigned char)(value >> 24);
    data[1] = (unsigned char)(value >> 16);
    data[2] = (unsigned char)(value >> 8);
    data[3] = (unsigned char)value;
}

static void
fake_init(struct fake_disk *fake)
{
    unsigned sector;
    unsigned byte;

    memset(fake, 0, sizeof(*fake));
    fake->sector_size = UMASS_SECTOR_SIZE;
    for (sector = 0; sector < FAKE_SECTORS; ++sector)
        for (byte = 0; byte < UMASS_SECTOR_SIZE; ++byte)
            fake->storage[sector * UMASS_SECTOR_SIZE + byte] =
                (unsigned char)(sector + byte);
}

static usb_error_t
fake_control(void *arg, const usb_device_request_t *request, void *buffer,
    size_t length, unsigned timeout_ms, size_t *actlenp)
{
    struct fake_disk *fake;

    (void)buffer;
    (void)timeout_ms;
    fake = (struct fake_disk *)arg;
    if (actlenp != 0)
        *actlenp = 0;
    if (request->bmRequestType != UT_WRITE_CLASS_INTERFACE ||
        request->bRequest != UMASS_BBB_RESET || length != 0)
        return USB_STATUS_STALLED;
    ++fake->reset_count;
    fake->have_cbw = 0;
    fake->data_attempted = 0;
    fake->data_done = 0;
    fake->write_rejected = 0;
    return USB_STATUS_NORMAL_COMPLETION;
}

static usb_error_t
fake_clear_halt(void *arg, unsigned direction)
{
    struct fake_disk *fake;

    fake = (struct fake_disk *)arg;
    if (direction == UMASS_DIR_IN)
        ++fake->clear_in_count;
    else if (direction == UMASS_DIR_OUT)
        ++fake->clear_out_count;
    else
        return USB_STATUS_INVALID;
    return USB_STATUS_NORMAL_COMPLETION;
}

static void
fake_inquiry(struct fake_disk *fake, void *vdata, size_t length)
{
    unsigned char *data;

    data = (unsigned char *)vdata;
    memset(data, 0, length);
    if (length > 0)
        data[0] = 0;
    if (length >= UMASS_INQUIRY_LENGTH) {
        memcpy(data + 8, "REBSD   ", 8);
        memcpy(data + 16, "FAKE USB DISK   ", 16);
        memcpy(data + 32, "1.0 ", 4);
    }
    ++fake->inquiry_count;
}

static usb_error_t
fake_data_in(struct fake_disk *fake, void *data, size_t length,
    size_t *actlenp)
{
    unsigned char *bytes;
    unsigned opcode;
    unsigned lba;
    unsigned sectors;

    opcode = fake->cbw.CBWCDB[0];
    bytes = (unsigned char *)data;
    fake->data_attempted = 1;
    if (fake->data_stall_once != 0) {
        fake->data_stall_once = 0;
        return USB_STATUS_STALLED;
    }
    switch (opcode) {
    case SCSI_INQUIRY:
        fake_inquiry(fake, data, length);
        break;
    case SCSI_REQUEST_SENSE:
        memset(data, 0, length);
        if (length >= UMASS_SENSE_LENGTH) {
            bytes[0] = 0x70;
            bytes[2] = (unsigned char)(fake->sense_key != 0 ?
                fake->sense_key : 0x06u);
            bytes[7] = 10;
        }
        fake->sense_key = 0;
        ++fake->sense_count;
        break;
    case SCSI_READ_CAPACITY_10:
        if (length != 8)
            return USB_STATUS_IO_ERROR;
        set_be32(bytes, FAKE_SECTORS - 1);
        set_be32(bytes + 4, fake->sector_size);
        ++fake->capacity_count;
        break;
    case SCSI_READ_10:
        lba = get_be32(fake->cbw.CBWCDB + 2);
        sectors = get_be16(fake->cbw.CBWCDB + 7);
        if (sectors == 0 || sectors > FAKE_SECTORS ||
            lba > FAKE_SECTORS - sectors ||
            length != sectors * UMASS_SECTOR_SIZE)
            return USB_STATUS_IO_ERROR;
        memcpy(data, fake->storage + lba * UMASS_SECTOR_SIZE, length);
        ++fake->read_count;
        if (sectors > fake->max_read_sectors)
            fake->max_read_sectors = sectors;
        break;
    default:
        return USB_STATUS_IO_ERROR;
    }
    fake->data_done = 1;
    *actlenp = length;
    return USB_STATUS_NORMAL_COMPLETION;
}

static usb_error_t
fake_data_out(struct fake_disk *fake, const void *data, size_t length,
    size_t *actlenp)
{
    unsigned lba;
    unsigned opcode;
    unsigned sectors;

    opcode = fake->cbw.CBWCDB[0];
    fake->data_attempted = 1;
    if (fake->data_stall_once != 0) {
        fake->data_stall_once = 0;
        return USB_STATUS_STALLED;
    }
    if (opcode != SCSI_WRITE_10)
        return USB_STATUS_IO_ERROR;
    lba = get_be32(fake->cbw.CBWCDB + 2);
    sectors = get_be16(fake->cbw.CBWCDB + 7);
    if (sectors == 0 || sectors > FAKE_SECTORS ||
        lba > FAKE_SECTORS - sectors ||
        length != sectors * UMASS_SECTOR_SIZE)
        return USB_STATUS_IO_ERROR;
    if (fake->write_protected)
        fake->write_rejected = 1;
    else
        memcpy(fake->storage + lba * UMASS_SECTOR_SIZE, data, length);
    ++fake->write_count;
    if (sectors > fake->max_write_sectors)
        fake->max_write_sectors = sectors;
    fake->data_done = 1;
    *actlenp = length;
    return USB_STATUS_NORMAL_COMPLETION;
}

static usb_error_t
fake_csw(struct fake_disk *fake, void *buffer, size_t *actlenp)
{
    struct umass_bbb_csw *csw;
    unsigned opcode;

    if (fake->csw_stall_once != 0) {
        fake->csw_stall_once = 0;
        return USB_STATUS_STALLED;
    }
    csw = (struct umass_bbb_csw *)buffer;
    memset(csw, 0, sizeof(*csw));
    USETDW(csw->dCSWSignature, UMASS_BBB_CSW_SIGNATURE);
    USETDW(csw->dCSWTag, UGETDW(fake->cbw.dCBWTag) +
        (fake->bad_tag_once != 0 ? 1u : 0u));
    fake->bad_tag_once = 0;
    opcode = fake->cbw.CBWCDB[0];
    if (opcode == SCSI_TEST_UNIT_READY) {
        ++fake->tur_count;
        if (fake->tur_failures != 0) {
            --fake->tur_failures;
            csw->bCSWStatus = UMASS_BBB_CSW_FAILED;
        }
    } else if (opcode == SCSI_WRITE_10 && fake->write_rejected) {
        csw->bCSWStatus = UMASS_BBB_CSW_FAILED;
        fake->write_rejected = 0;
    } else if (opcode == SCSI_SYNCHRONIZE_CACHE_10) {
        ++fake->sync_count;
        if (fake->sync_unsupported) {
            csw->bCSWStatus = UMASS_BBB_CSW_FAILED;
            fake->sense_key = 0x05u;
        }
    }
    *actlenp = sizeof(*csw);
    fake->have_cbw = 0;
    fake->data_attempted = 0;
    fake->data_done = 0;
    return USB_STATUS_NORMAL_COMPLETION;
}

static usb_error_t
fake_bulk(void *arg, unsigned direction, void *buffer, size_t length,
    unsigned flags, unsigned timeout_ms, size_t *actlenp)
{
    struct fake_disk *fake;

    (void)flags;
    (void)timeout_ms;
    fake = (struct fake_disk *)arg;
    *actlenp = 0;
    if (fake->fail_next_bulk != USB_STATUS_NORMAL_COMPLETION) {
        usb_error_t error;

        error = fake->fail_next_bulk;
        fake->fail_next_bulk = USB_STATUS_NORMAL_COMPLETION;
        return error;
    }
    if (direction == UMASS_DIR_OUT && length == UMASS_BBB_CBW_SIZE) {
        memcpy(&fake->cbw, buffer, sizeof(fake->cbw));
        if (UGETDW(fake->cbw.dCBWSignature) !=
            UMASS_BBB_CBW_SIGNATURE || fake->cbw.bCBWLUN != 0 ||
            fake->cbw.bCDBLength == 0 ||
            fake->cbw.bCDBLength > UMASS_BBB_CDB_MAX)
            return USB_STATUS_IO_ERROR;
        fake->have_cbw = 1;
        fake->data_attempted = 0;
        fake->data_done = 0;
        ++fake->cbw_count;
        *actlenp = length;
        return USB_STATUS_NORMAL_COMPLETION;
    }
    if (!fake->have_cbw)
        return USB_STATUS_IO_ERROR;
    if (direction == UMASS_DIR_IN && length == UMASS_BBB_CSW_SIZE &&
        (UGETDW(fake->cbw.dCBWDataTransferLength) == 0 ||
        fake->data_attempted))
        return fake_csw(fake, buffer, actlenp);
    if (direction == UMASS_DIR_IN &&
        length == UGETDW(fake->cbw.dCBWDataTransferLength))
        return fake_data_in(fake, buffer, length, actlenp);
    if (direction == UMASS_DIR_OUT &&
        length == UGETDW(fake->cbw.dCBWDataTransferLength))
        return fake_data_out(fake, buffer, length, actlenp);
    return USB_STATUS_IO_ERROR;
}

static const struct umass_bbb_ops fake_ops = {
    fake_bulk,
    fake_control,
    fake_clear_halt
};

static int
test_probe_and_chunked_io(void)
{
    struct fake_disk fake;
    struct umass_bbb bbb;
    struct umass_media media;
    unsigned char data[20 * UMASS_SECTOR_SIZE];
    unsigned char write_data[20 * UMASS_SECTOR_SIZE];
    unsigned cbw_count;
    unsigned i;

    fake_init(&fake);
    fake.tur_failures = 1;
    umass_bbb_init(&bbb, &fake_ops, &fake, 0);
    umass_media_init(&media, &bbb);
    CHECK(umass_media_probe(&media) == UMASS_BBB_OK);
    CHECK(media.um_sector_count == FAKE_SECTORS);
    CHECK(media.um_sector_size == UMASS_SECTOR_SIZE);
    CHECK(fake.inquiry_count == 1);
    CHECK(fake.tur_count == 2);
    CHECK(fake.sense_count == 1);
    CHECK(fake.capacity_count == 1);
    CHECK(fake.read_count == 0);

    CHECK(umass_media_read(&media, 4, 20, data) == UMASS_BBB_OK);
    CHECK(memcmp(data, fake.storage + 4 * UMASS_SECTOR_SIZE,
        sizeof(data)) == 0);
    CHECK(fake.read_count == 2);
    CHECK(fake.max_read_sectors == UMASS_MAX_READ_SECTORS);

    for (i = 0; i < sizeof(write_data); ++i)
        write_data[i] = (unsigned char)(0xa5u ^ i);
    CHECK(umass_media_write(&media, 8, 20, write_data) == UMASS_BBB_OK);
    CHECK(memcmp(fake.storage + 8 * UMASS_SECTOR_SIZE, write_data,
        sizeof(write_data)) == 0);
    CHECK(fake.write_count == 2);
    CHECK(fake.max_write_sectors == UMASS_MAX_WRITE_SECTORS);
    CHECK(umass_media_flush(&media) == UMASS_BBB_OK);
    CHECK(fake.sync_count == 1);

    cbw_count = fake.cbw_count;
    CHECK(umass_media_write(&media, FAKE_SECTORS - 1, 2, write_data) ==
        UMASS_BBB_WIRE_FAILED);
    CHECK(bbb.ub_last_error == USB_STATUS_INVALID);
    CHECK(fake.cbw_count == cbw_count);
    return 0;
}

static int
test_write_failures_and_optional_flush(void)
{
    struct fake_disk fake;
    struct umass_bbb bbb;
    struct umass_media media;
    unsigned char before[UMASS_SECTOR_SIZE];
    unsigned char data[UMASS_SECTOR_SIZE];
    unsigned resets;
    unsigned sync_count;

    fake_init(&fake);
    memset(data, 0x5a, sizeof(data));
    umass_bbb_init(&bbb, &fake_ops, &fake, 0);
    umass_media_init(&media, &bbb);
    CHECK(umass_media_probe(&media) == UMASS_BBB_OK);

    memcpy(before, fake.storage + 3 * UMASS_SECTOR_SIZE, sizeof(before));
    fake.write_protected = 1;
    CHECK(umass_media_write(&media, 3, 1, data) ==
        UMASS_BBB_COMMAND_FAILED);
    CHECK(memcmp(fake.storage + 3 * UMASS_SECTOR_SIZE, before,
        sizeof(before)) == 0);

    fake.write_protected = 0;
    resets = fake.reset_count;
    fake.fail_next_bulk = USB_STATUS_DISCONNECTED;
    CHECK(umass_media_write(&media, 3, 1, data) ==
        UMASS_BBB_WIRE_FAILED);
    CHECK(bbb.ub_last_error == USB_STATUS_DISCONNECTED);
    CHECK(fake.reset_count == resets);
    CHECK(memcmp(fake.storage + 3 * UMASS_SECTOR_SIZE, before,
        sizeof(before)) == 0);

    fake.sync_unsupported = 1;
    CHECK(umass_media_flush(&media) == UMASS_BBB_OK);
    CHECK(media.um_no_sync_cache == 1);
    CHECK(fake.sync_count == 1);
    CHECK(fake.sense_count == 1);
    sync_count = fake.sync_count;
    CHECK(umass_media_flush(&media) == UMASS_BBB_OK);
    CHECK(fake.sync_count == sync_count);
    return 0;
}

static int
test_bot_error_paths(void)
{
    struct fake_disk fake;
    struct umass_bbb bbb;
    unsigned char inquiry[UMASS_INQUIRY_LENGTH];
    size_t actlen;
    unsigned resets;

    fake_init(&fake);
    umass_bbb_init(&bbb, &fake_ops, &fake, 3);

    fake.bad_tag_once = 1;
    CHECK(umass_scsi_inquiry(&bbb, inquiry, sizeof(inquiry), &actlen) ==
        UMASS_BBB_WIRE_FAILED);
    CHECK(bbb.ub_last_error == USB_STATUS_IO_ERROR);
    CHECK(fake.reset_count == 1);
    CHECK(fake.clear_in_count == 1 && fake.clear_out_count == 1);

    resets = fake.reset_count;
    fake.fail_next_bulk = USB_STATUS_DISCONNECTED;
    CHECK(umass_scsi_test_unit_ready(&bbb) == UMASS_BBB_WIRE_FAILED);
    CHECK(bbb.ub_last_error == USB_STATUS_DISCONNECTED);
    CHECK(fake.reset_count == resets);

    fake.data_stall_once = 1;
    CHECK(umass_scsi_inquiry(&bbb, inquiry, sizeof(inquiry), &actlen) ==
        UMASS_BBB_OK);
    CHECK(actlen == 0);
    CHECK(fake.clear_in_count == 2);

    fake.csw_stall_once = 1;
    CHECK(umass_scsi_test_unit_ready(&bbb) == UMASS_BBB_OK);
    CHECK(fake.clear_in_count == 3);

    fake.tur_failures = 1;
    CHECK(umass_scsi_test_unit_ready(&bbb) ==
        UMASS_BBB_COMMAND_FAILED);
    CHECK(fake.reset_count == resets);
    return 0;
}

static int
test_unsupported_sector_size(void)
{
    struct fake_disk fake;
    struct umass_bbb bbb;
    struct umass_media media;

    fake_init(&fake);
    fake.sector_size = 4096;
    umass_bbb_init(&bbb, &fake_ops, &fake, 0);
    umass_media_init(&media, &bbb);
    CHECK(umass_media_probe(&media) == UMASS_BBB_WIRE_FAILED);
    CHECK(bbb.ub_last_error == USB_STATUS_UNSUPPORTED);
    CHECK(fake.read_count == 0);
    return 0;
}

int
main(void)
{
    CHECK(test_probe_and_chunked_io() == 0);
    CHECK(test_write_failures_and_optional_flush() == 0);
    CHECK(test_bot_error_paths() == 0);
    CHECK(test_unsupported_sector_size() == 0);
    puts("umass_test: all tests passed");
    return 0;
}
