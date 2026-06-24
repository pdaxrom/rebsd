/*
 * Host-side ROMFS journal power-cut tests.
 *
 * This uses the public romfs_flash_sector_* hooks to emulate NOR flash and
 * fail erase/write transactions at deterministic points.  It does not touch
 * /dev/cartflash0 or real cartridge flash.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "romfs.h"

#define TEST_ROM_SIZE       (8u * 1024u * 1024u)
#define TEST_ROMFS_OFFSET   0x10000u
#define TEST_PAYLOAD        "journal-payload"
#define TEST_PAYLOAD_LEN    ((uint32_t)(sizeof(TEST_PAYLOAD) - 1))
#define TEST_APPEND         "-deferred"
#define TEST_APPEND_LEN     ((uint32_t)(sizeof(TEST_APPEND) - 1))

/*
 * With TEST_ROM_SIZE, ROMFS list and map are one sector each.  A metadata-only
 * rename flush writes:
 *   WRITING header erase/write, list erase/write, map erase/write,
 *   VALID header erase/write, primary list erase/write, primary map erase/write.
 */
#define TEST_VALID_JOURNAL_OPS 8
#define TEST_FLUSH_OPS        12

static uint8_t *flash_image;
static uint8_t *base_image;
static uint16_t *flash_map;
static uint8_t *flash_list;
static uint32_t flash_map_size;
static uint32_t flash_list_size;
static uint8_t io_buffer[ROMFS_FLASH_SECTOR];

static bool fail_enabled;
static bool fail_tripped;
static int fail_after_ops;
static int write_erase_ops;

static void
die(const char *message)
{
    fprintf(stderr, "romfs-journal-hosttest: %s\n", message);
    exit(1);
}

static void
die_romfs(const char *what, uint32_t err)
{
    fprintf(stderr, "romfs-journal-hosttest: %s: %s\n", what,
        romfs_strerror(err));
    exit(1);
}

static bool
valid_range(uint32_t offset, uint32_t size)
{
    return offset <= TEST_ROM_SIZE && size <= TEST_ROM_SIZE - offset;
}

static bool
power_cut_now(void)
{
    if (!fail_enabled)
        return false;
    if (fail_tripped)
        return true;
    if (write_erase_ops++ >= fail_after_ops) {
        fail_tripped = true;
        return true;
    }
    return false;
}

bool
romfs_flash_sector_read(uint32_t offset, uint8_t *buffer, uint32_t need)
{
    if (!valid_range(offset, need))
        return false;
    memcpy(buffer, &flash_image[offset], need);
    return true;
}

bool
romfs_flash_sector_write(uint32_t offset, uint8_t *buffer)
{
    if (!valid_range(offset, ROMFS_FLASH_SECTOR))
        return false;
    if (power_cut_now())
        return false;
    for (uint32_t i = 0; i < ROMFS_FLASH_SECTOR; i++)
        flash_image[offset + i] &= buffer[i];
    return true;
}

bool
romfs_flash_sector_erase(uint32_t offset)
{
    if (!valid_range(offset, ROMFS_FLASH_SECTOR))
        return false;
    if (power_cut_now())
        return false;
    memset(&flash_image[offset], 0xff, ROMFS_FLASH_SECTOR);
    return true;
}

static void
alloc_buffers(void)
{
    flash_image = malloc(TEST_ROM_SIZE);
    base_image = malloc(TEST_ROM_SIZE);
    if (!flash_image || !base_image)
        die("no memory for flash image");

    romfs_get_buffers_sizes(TEST_ROM_SIZE, &flash_map_size, &flash_list_size);
    flash_map = malloc(flash_map_size);
    flash_list = malloc(flash_list_size);
    if (!flash_map || !flash_list)
        die("no memory for romfs tables");
}

static void
start_romfs_or_die(void)
{
    if (!romfs_start(TEST_ROMFS_OFFSET, TEST_ROM_SIZE, flash_map, flash_list))
        die("romfs_start failed");
    if (!romfs_validate())
        die("romfs_validate failed");
}

static void
start_blank_romfs_or_die(void)
{
    if (!romfs_start(TEST_ROMFS_OFFSET, TEST_ROM_SIZE, flash_map, flash_list))
        die("romfs_start on blank image failed");
}

static void
format_base_image(void)
{
    romfs_file file;
    uint32_t err;

    memset(flash_image, 0xff, TEST_ROM_SIZE);
    start_blank_romfs_or_die();
    if (!romfs_format())
        die("romfs_format failed");

    start_romfs_or_die();
    memset(&file, 0, sizeof(file));
    err = romfs_create_path("/before.txt", &file, ROMFS_MODE_READWRITE,
        ROMFS_TYPE_MISC, io_buffer, false);
    if (err != ROMFS_NOERR)
        die_romfs("create /before.txt", err);
    if (romfs_write_file(TEST_PAYLOAD, TEST_PAYLOAD_LEN, &file) !=
        TEST_PAYLOAD_LEN)
        die_romfs("write /before.txt", file.err);
    err = romfs_close_file(&file);
    if (err != ROMFS_NOERR)
        die_romfs("close /before.txt", err);

    memcpy(base_image, flash_image, TEST_ROM_SIZE);
}

static bool
read_file_exact(const char *path)
{
    romfs_file file;
    char buffer[sizeof(TEST_PAYLOAD)];
    uint32_t got;
    uint32_t err;

    memset(&file, 0, sizeof(file));
    err = romfs_open_path(path, &file, io_buffer);
    if (err == ROMFS_ERR_NO_ENTRY)
        return false;
    if (err != ROMFS_NOERR)
        die_romfs(path, err);
    memset(buffer, 0, sizeof(buffer));
    got = romfs_read_file(buffer, TEST_PAYLOAD_LEN, &file);
    if (got != TEST_PAYLOAD_LEN || memcmp(buffer, TEST_PAYLOAD,
        TEST_PAYLOAD_LEN) != 0)
        die("file payload mismatch after recovery");
    return true;
}

static bool
read_file_payload(const char *path, const char *payload, uint32_t payload_len)
{
    romfs_file file;
    char buffer[64];
    uint32_t got;
    uint32_t err;

    if (payload_len > sizeof(buffer))
        die("read_file_payload buffer too small");

    memset(&file, 0, sizeof(file));
    err = romfs_open_path(path, &file, io_buffer);
    if (err == ROMFS_ERR_NO_ENTRY)
        return false;
    if (err != ROMFS_NOERR)
        die_romfs(path, err);
    if (file.entry.size != payload_len)
        die("file payload size mismatch");
    memset(buffer, 0, sizeof(buffer));
    got = romfs_read_file(buffer, payload_len, &file);
    if (got != payload_len || memcmp(buffer, payload, payload_len) != 0)
        die("file payload mismatch");
    return true;
}

static void
check_state_after_restart(int fail_after)
{
    bool have_before;
    bool have_after;
    bool expect_after;

    start_romfs_or_die();

    have_before = read_file_exact("/before.txt");
    have_after = read_file_exact("/after.txt");
    if (have_before == have_after)
        die("recovered image has neither or both rename states");

    expect_after = fail_after >= TEST_VALID_JOURNAL_OPS;
    if (expect_after && !have_after)
        die("valid journal was not recovered after simulated power cut");
    if (!expect_after && !have_before)
        die("uncommitted journal update changed primary state");
}

static void
run_rename_power_cut_case(int fail_after)
{
    uint32_t err;

    memcpy(flash_image, base_image, TEST_ROM_SIZE);
    start_romfs_or_die();

    fail_enabled = true;
    fail_tripped = false;
    fail_after_ops = fail_after;
    write_erase_ops = 0;
    err = romfs_rename_path("/before.txt", "/after.txt", false);
    if (err != ROMFS_NOERR)
        die_romfs("rename power-cut case", err);
    if (!fail_tripped && fail_after < TEST_FLUSH_OPS)
        die("power-cut trigger did not trip");
    fail_enabled = false;

    check_state_after_restart(fail_after);
}

static void
corrupt_sector(uint32_t offset)
{
    if (!valid_range(offset, ROMFS_FLASH_SECTOR))
        die("corrupt_sector outside flash image");
    memset(&flash_image[offset], 0xff, ROMFS_FLASH_SECTOR);
}

static void
run_primary_corruption_recovery(void)
{
    uint32_t flash_start = (TEST_ROMFS_OFFSET + 0x7fffu) & ~0x7fffu;

    memcpy(flash_image, base_image, TEST_ROM_SIZE);
    start_romfs_or_die();
    if (romfs_rename_path("/before.txt", "/after.txt", false) != ROMFS_NOERR)
        die("clean rename failed");

    corrupt_sector(flash_start);
    check_state_after_restart(TEST_VALID_JOURNAL_OPS);

    memcpy(flash_image, base_image, TEST_ROM_SIZE);
    start_romfs_or_die();
    if (romfs_rename_path("/before.txt", "/after.txt", false) != ROMFS_NOERR)
        die("clean rename failed");

    corrupt_sector(flash_start + flash_list_size);
    check_state_after_restart(TEST_VALID_JOURNAL_OPS);
}

static void
run_deferred_metadata_flush(void)
{
    romfs_file file;
    char appended[sizeof(TEST_PAYLOAD) + sizeof(TEST_APPEND)];
    uint32_t appended_len;
    uint32_t err;

    memcpy(flash_image, base_image, TEST_ROM_SIZE);
    start_romfs_or_die();

    memset(&file, 0, sizeof(file));
    err = romfs_open_append_path("/before.txt", &file, ROMFS_TYPE_MISC,
        io_buffer, false);
    if (err != ROMFS_NOERR)
        die_romfs("open append deferred", err);
    if (romfs_write_file(TEST_APPEND, TEST_APPEND_LEN, &file) !=
        TEST_APPEND_LEN)
        die_romfs("write deferred append", file.err);
    err = romfs_flush_file_deferred(&file);
    if (err != ROMFS_NOERR)
        die_romfs("flush deferred append", err);

    memcpy(appended, TEST_PAYLOAD, TEST_PAYLOAD_LEN);
    memcpy(appended + TEST_PAYLOAD_LEN, TEST_APPEND, TEST_APPEND_LEN);
    appended_len = TEST_PAYLOAD_LEN + TEST_APPEND_LEN;

    if (!read_file_payload("/before.txt", appended, appended_len))
        die("deferred metadata is not visible in memory");

    start_romfs_or_die();
    if (!read_file_payload("/before.txt", TEST_PAYLOAD, TEST_PAYLOAD_LEN))
        die("unsynced deferred metadata survived restart");

    memset(&file, 0, sizeof(file));
    err = romfs_open_append_path("/before.txt", &file, ROMFS_TYPE_MISC,
        io_buffer, false);
    if (err != ROMFS_NOERR)
        die_romfs("open append deferred sync", err);
    if (romfs_write_file(TEST_APPEND, TEST_APPEND_LEN, &file) !=
        TEST_APPEND_LEN)
        die_romfs("write deferred sync append", file.err);
    err = romfs_flush_file_deferred(&file);
    if (err != ROMFS_NOERR)
        die_romfs("flush deferred sync append", err);
    err = romfs_sync_metadata();
    if (err != ROMFS_NOERR)
        die_romfs("sync deferred metadata", err);

    start_romfs_or_die();
    if (!read_file_payload("/before.txt", appended, appended_len))
        die("synced deferred metadata did not survive restart");
}

static void
run_data_write_failure(void)
{
    romfs_file file;
    uint32_t err;

    memcpy(flash_image, base_image, TEST_ROM_SIZE);
    start_romfs_or_die();

    memset(&file, 0, sizeof(file));
    err = romfs_create_path("/writefail.txt", &file, ROMFS_MODE_READWRITE,
        ROMFS_TYPE_MISC, io_buffer, false);
    if (err != ROMFS_NOERR)
        die_romfs("create writefail", err);
    if (romfs_write_file(TEST_PAYLOAD, TEST_PAYLOAD_LEN, &file) !=
        TEST_PAYLOAD_LEN)
        die_romfs("write writefail", file.err);

    fail_enabled = true;
    fail_tripped = false;
    fail_after_ops = 0;
    write_erase_ops = 0;
    err = romfs_flush_file_deferred(&file);
    fail_enabled = false;
    if (err == ROMFS_NOERR)
        die("data write failure was reported as success");
    if (!fail_tripped)
        die("data write failure trigger did not trip");

    start_romfs_or_die();
    if (read_file_payload("/writefail.txt", TEST_PAYLOAD, TEST_PAYLOAD_LEN))
        die("failed data write became visible after restart");
}

int
main(void)
{
    alloc_buffers();
    format_base_image();

    for (int fail_after = 0; fail_after <= TEST_FLUSH_OPS; fail_after++)
        run_rename_power_cut_case(fail_after);
    run_primary_corruption_recovery();
    run_deferred_metadata_flush();
    run_data_write_failure();

    printf("romfs journal host power-cut test ok\n");
    return 0;
}
