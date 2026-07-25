/* Host tests for the transport-independent FAT16/FAT32 parser. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fs/fat/fat.h>

#define CHECK(expr) do {                                                \
    if (!(expr)) {                                                      \
        fprintf(stderr, "%s:%d: check failed: %s\n",                 \
            __FILE__, __LINE__, #expr);                                 \
        return 1;                                                       \
    }                                                                   \
} while (0)

static void
set_le16(unsigned char *data, unsigned value)
{
    data[0] = (unsigned char)value;
    data[1] = (unsigned char)(value >> 8);
}

static void
set_le32(unsigned char *data, unsigned value)
{
    data[0] = (unsigned char)value;
    data[1] = (unsigned char)(value >> 8);
    data[2] = (unsigned char)(value >> 16);
    data[3] = (unsigned char)(value >> 24);
}

static unsigned
get_le32(const unsigned char *data)
{
    return (unsigned)data[0] | ((unsigned)data[1] << 8) |
        ((unsigned)data[2] << 16) | ((unsigned)data[3] << 24);
}

static void
boot_common(unsigned char *boot, unsigned sectors_per_cluster,
    unsigned reserved, unsigned fats, unsigned total)
{
    memset(boot, 0, FAT_SECTOR_SIZE);
    set_le16(boot + 11, FAT_SECTOR_SIZE);
    boot[13] = (unsigned char)sectors_per_cluster;
    set_le16(boot + 14, reserved);
    boot[16] = (unsigned char)fats;
    set_le32(boot + 32, total);
    boot[510] = 0x55;
    boot[511] = 0xaa;
}

static int
test_fat16(void)
{
    unsigned char boot[FAT_SECTOR_SIZE];
    unsigned char fat_entry[4];
    struct fat_volume volume;
    unsigned sector, offset;

    boot_common(boot, 4, 1, 2, 32768);
    set_le16(boot + 17, 512);
    set_le16(boot + 22, 32);
    CHECK(fat_volume_parse(&volume, boot, 32768) == FAT_PARSE_OK);
    CHECK(volume.fv_type == FAT_TYPE_16);
    CHECK(volume.fv_data_start == 97);
    CHECK(volume.fv_cluster_count == 8167);
    CHECK(volume.fv_reserved_sectors == 1);
    CHECK(volume.fv_fat_count == 2 && volume.fv_active_fat == 0);
    CHECK(volume.fv_fat_mirrored);
    CHECK(fat_cluster_first_sector(&volume, 2) == 97);
    CHECK(fat_fat_position(&volume, 7, &sector, &offset) == FAT_PARSE_OK);
    CHECK(sector == 1 && offset == 14);
    set_le16(fat_entry, 0xfff8);
    CHECK(fat_cluster_is_eoc(&volume,
        fat_fat_decode(&volume, fat_entry)));
    return 0;
}

static int
test_fat32(void)
{
    unsigned char boot[FAT_SECTOR_SIZE];
    unsigned char fat_entry[4];
    struct fat_volume volume;
    unsigned sector, offset;

    boot_common(boot, 8, 32, 2, 30298527);
    set_le32(boot + 36, 29568);
    set_le32(boot + 44, 2);
    CHECK(fat_volume_parse(&volume, boot, 30298527) == FAT_PARSE_OK);
    CHECK(volume.fv_type == FAT_TYPE_32);
    CHECK(volume.fv_data_start == 59168);
    CHECK(volume.fv_root_cluster == 2);
    CHECK(volume.fv_cluster_count == 3779919);
    CHECK(volume.fv_fat_start == 32);
    CHECK(volume.fv_reserved_sectors == 32);
    CHECK(volume.fv_fat_count == 2 && volume.fv_active_fat == 0);
    CHECK(volume.fv_fat_mirrored);
    set_le32(fat_entry, 0xafffffff);
    CHECK(fat_cluster_is_eoc(&volume,
        fat_fat_decode(&volume, fat_entry)));

    /* Mirroring disabled, FAT copy 1 selected by BPB_ExtFlags. */
    set_le16(boot + 40, 0x0081);
    CHECK(fat_volume_parse(&volume, boot, 30298527) == FAT_PARSE_OK);
    CHECK(volume.fv_fat_start == 32 + 29568);
    CHECK(volume.fv_active_fat == 1);
    CHECK(!volume.fv_fat_mirrored);
    CHECK(fat_fat_position(&volume, 2, &sector, &offset) == FAT_PARSE_OK);
    CHECK(sector == 32 + 29568 && offset == 8);
    return 0;
}

static int
test_rejects_bad_bpb(void)
{
    unsigned char boot[FAT_SECTOR_SIZE];
    struct fat_volume volume;

    boot_common(boot, 3, 32, 2, 30298527);
    set_le32(boot + 36, 29568);
    set_le32(boot + 44, 2);
    CHECK(fat_volume_parse(&volume, boot, 30298527) == FAT_PARSE_INVALID);
    boot[13] = 8;
    CHECK(fat_volume_parse(&volume, boot, 1000) == FAT_PARSE_INVALID);
    set_le16(boot + 40, 0x0082);
    CHECK(fat_volume_parse(&volume, boot, 30298527) == FAT_PARSE_INVALID);
    set_le16(boot + 40, 0);
    set_le16(boot + 42, 1);
    CHECK(fat_volume_parse(&volume, boot, 30298527) ==
        FAT_PARSE_UNSUPPORTED);
    set_le16(boot + 42, 0);
    boot[510] = 0;
    CHECK(fat_volume_parse(&volume, boot, 30298527) == FAT_PARSE_INVALID);
    return 0;
}

static int
test_names(void)
{
    unsigned char entry[FAT_DIRENT_SIZE];
    unsigned char short_name[11];
    struct fat_dirent dirent;
    char name[16];

    memset(entry, ' ', 11);
    memcpy(entry, "README  TXT", 11);
    entry[11] = FAT_ATTR_ARCHIVE;
    entry[12] = 0x18;
    set_le16(entry + 20, 0x1234);
    set_le16(entry + 26, 0x5678);
    set_le32(entry + 28, 12345);
    CHECK(fat_short_name(entry, name, sizeof(name)) == 10);
    CHECK(strcmp(name, "readme.txt") == 0);
    CHECK(fat_ascii_name_equal(name, 10, "README.TXT", 10));
    CHECK(fat_lfn_checksum(entry) == 0x73);
    fat_dirent_parse(&dirent, entry);
    CHECK(dirent.fd_cluster == 0x12345678u);
    CHECK(dirent.fd_size == 12345);
    CHECK(fat_dirent_is_visible(entry));
    entry[11] = FAT_ATTR_LONG_NAME;
    CHECK(!fat_dirent_is_visible(entry));

    CHECK(fat_short_name_encode("hello.txt", 9, short_name) ==
        FAT_PARSE_OK);
    CHECK(memcmp(short_name, "HELLO   TXT", 11) == 0);
    fat_dirent_encode(entry, short_name, FAT_ATTR_ARCHIVE, 0x12345678u,
        0x01020304u);
    fat_dirent_parse(&dirent, entry);
    CHECK(dirent.fd_attr == FAT_ATTR_ARCHIVE);
    CHECK(dirent.fd_cluster == 0x12345678u);
    CHECK(dirent.fd_size == 0x01020304u);
    fat_dirent_set_cluster_size(entry, 0x07654321u, 0x10203040u);
    fat_dirent_parse(&dirent, entry);
    CHECK(dirent.fd_cluster == 0x07654321u);
    CHECK(dirent.fd_size == 0x10203040u);
    CHECK(fat_short_name_encode("too-long-name.txt", 17, short_name) ==
        FAT_PARSE_UNSUPPORTED);
    CHECK(fat_short_name_encode("two.dots.txt", 12, short_name) ==
        FAT_PARSE_UNSUPPORTED);
    CHECK(fat_short_name_encode("bad name", 8, short_name) ==
        FAT_PARSE_UNSUPPORTED);
    return 0;
}

static int
test_fat_encoding(void)
{
    unsigned char entry[4];
    struct fat_volume volume;

    memset(&volume, 0, sizeof(volume));
    volume.fv_type = FAT_TYPE_16;
    memset(entry, 0xa5, sizeof(entry));
    fat_fat_encode(&volume, entry, 0xfff8u);
    CHECK(entry[0] == 0xf8 && entry[1] == 0xff);

    volume.fv_type = FAT_TYPE_32;
    entry[0] = 0x78;
    entry[1] = 0x56;
    entry[2] = 0x34;
    entry[3] = 0xa2;
    fat_fat_encode(&volume, entry, 0x01234567u);
    CHECK(entry[0] == 0x67 && entry[1] == 0x45 && entry[2] == 0x23 &&
        entry[3] == 0xa1);
    return 0;
}

static int
test_directory_encoding(void)
{
    unsigned char sector[FAT_SECTOR_SIZE];
    struct fat_dirent dirent;
    unsigned i;

    memset(sector, 0xa5, sizeof(sector));
    fat_directory_encode(sector, 0x12345u, 0x6789u);
    CHECK(memcmp(sector, ".          ", 11) == 0);
    CHECK(memcmp(sector + FAT_DIRENT_SIZE, "..         ", 11) == 0);
    fat_dirent_parse(&dirent, sector);
    CHECK(dirent.fd_attr == FAT_ATTR_DIRECTORY);
    CHECK(dirent.fd_cluster == 0x12345u && dirent.fd_size == 0);
    fat_dirent_parse(&dirent, sector + FAT_DIRENT_SIZE);
    CHECK(dirent.fd_attr == FAT_ATTR_DIRECTORY);
    CHECK(dirent.fd_cluster == 0x6789u && dirent.fd_size == 0);
    for (i = 2u * FAT_DIRENT_SIZE; i < FAT_SECTOR_SIZE; ++i)
        CHECK(sector[i] == 0);
    return 0;
}

struct fat_ro_image {
    unsigned char boot[FAT_SECTOR_SIZE];
    unsigned char fat[FAT_SECTOR_SIZE];
    unsigned char root[FAT_SECTOR_SIZE];
    unsigned char directory[FAT_SECTOR_SIZE];
    unsigned char file_first[FAT_SECTOR_SIZE];
    unsigned char file_second[FAT_SECTOR_SIZE];
};

static int
fat_ro_image_read(void *arg, unsigned sector, unsigned char *data)
{
    struct fat_ro_image *image;
    const unsigned char *source;

    image = arg;
    source = NULL;
    switch (sector) {
    case 0:
        source = image->boot;
        break;
    case 1:
    case 33:
        source = image->fat;
        break;
    case 65:
        source = image->root;
        break;
    case 69:
        source = image->directory;
        break;
    case 70:
        source = image->file_first;
        break;
    case 71:
        source = image->file_second;
        break;
    default:
        memset(data, 0, FAT_SECTOR_SIZE);
        return sector < 8192 ? 0 : EIO;
    }
    memcpy(data, source, FAT_SECTOR_SIZE);
    return 0;
}

static int
test_read_only_reader(void)
{
    struct fat_ro_image image;
    struct fat_ro reader;
    struct fat_ro_node node;
    struct fat_ro_node root;
    unsigned char short_name[11];
    unsigned char output[700];
    unsigned count;

    memset(&image, 0, sizeof(image));
    boot_common(image.boot, 1, 1, 2, 8192);
    set_le16(image.boot + 17, 64);
    set_le16(image.boot + 22, 32);
    set_le16(image.fat, 0xfff8);
    set_le16(image.fat + 2, 0xffff);
    set_le16(image.fat + 4, 0xffff);
    set_le16(image.fat + 6, 4);
    set_le16(image.fat + 8, 0xffff);

    CHECK(fat_short_name_encode("boot", 4, short_name) == FAT_PARSE_OK);
    fat_dirent_encode(image.root, short_name, FAT_ATTR_DIRECTORY, 2, 0);
    fat_directory_encode(image.directory, 2, 0);
    CHECK(fat_short_name_encode("root.txt", 8, short_name) ==
        FAT_PARSE_OK);
    fat_dirent_encode(image.directory + 2 * FAT_DIRENT_SIZE, short_name,
        FAT_ATTR_ARCHIVE | FAT_ATTR_READ_ONLY, 3, sizeof(output));
    memset(image.file_first, '.', sizeof(image.file_first));
    memcpy(image.file_first, "REBSD FAT ROOT\n", 15);
    memset(image.file_second, '.', sizeof(image.file_second));
    memcpy(image.file_second, "REBSD FAT SECOND CLUSTER\n", 25);

    CHECK(fat_ro_mount(&reader, 8192, fat_ro_image_read, &image) == 0);
    CHECK(reader.fr_volume.fv_type == FAT_TYPE_16);
    CHECK(fat_ro_lookup(&reader, "/boot/root.txt", &node) == 0);
    CHECK(node.fn_cluster == 3 && node.fn_size == sizeof(output));
    memset(output, 0, sizeof(output));
    CHECK(fat_ro_read(&reader, &node, 0, output, sizeof(output), &count) ==
        0);
    CHECK(count == sizeof(output));
    CHECK(memcmp(output, "REBSD FAT ROOT\n", 15) == 0);
    CHECK(memcmp(output + FAT_SECTOR_SIZE,
        "REBSD FAT SECOND CLUSTER\n", 25) == 0);
    count = 1;
    CHECK(fat_ro_read(&reader, &node, sizeof(output), output, 1, &count) ==
        0);
    CHECK(count == 0);
    CHECK(fat_ro_lookup(&reader, "/BOOT/MISSING.TXT", &node) == ENOENT);
    CHECK(fat_ro_lookup(&reader, "/boot/root.txt/child", &node) ==
        ENOTDIR);
    CHECK(fat_ro_lookup(&reader, "/boot/root.txt/", &node) == ENOTDIR);
    CHECK(fat_ro_lookup(&reader, "/component-too-long", &node) ==
        ENAMETOOLONG);
    CHECK(fat_ro_lookup(&reader, "/", &root) == 0);
    CHECK((root.fn_attr & FAT_ATTR_DIRECTORY) != 0);
    CHECK(fat_ro_read(&reader, &root, 0, output, 1, &count) == EISDIR);

    set_le16(image.fat + 6, 0xffff);
    CHECK(fat_ro_lookup(&reader, "/boot/root.txt", &node) == 0);
    CHECK(fat_ro_read(&reader, &node, 0, output, sizeof(output), &count) ==
        EIO);
    return 0;
}

#define FAT32_TEST_DATA_START       59168u
#define FAT32_TEST_CLUSTER_SECTORS  8u
#define FAT32_TEST_FILE_SIZE        (FAT32_TEST_CLUSTER_SECTORS * 512u + 100u)

struct fat32_ro_image {
    unsigned char boot[FAT_SECTOR_SIZE];
    unsigned char fat[FAT_SECTOR_SIZE];
    unsigned char root[FAT_SECTOR_SIZE];
};

static int
fat32_ro_image_read(void *arg, unsigned sector, unsigned char *data)
{
    struct fat32_ro_image *image;

    image = arg;
    memset(data, 0, FAT_SECTOR_SIZE);
    if (sector == 0)
        memcpy(data, image->boot, FAT_SECTOR_SIZE);
    else if (sector == 32)
        memcpy(data, image->fat, FAT_SECTOR_SIZE);
    else if (sector == FAT32_TEST_DATA_START)
        memcpy(data, image->root, FAT_SECTOR_SIZE);
    else if (sector >= FAT32_TEST_DATA_START +
        FAT32_TEST_CLUSTER_SECTORS &&
        sector < FAT32_TEST_DATA_START +
        3 * FAT32_TEST_CLUSTER_SECTORS) {
        memset(data, '.', FAT_SECTOR_SIZE);
        if (sector == FAT32_TEST_DATA_START +
            FAT32_TEST_CLUSTER_SECTORS)
            memcpy(data, "REBSD FAT32 ROOT\n", 17);
        else if (sector == FAT32_TEST_DATA_START +
            2 * FAT32_TEST_CLUSTER_SECTORS)
            memcpy(data, "REBSD FAT32 SECOND CLUSTER\n", 27);
    }
    return sector < 30298527u ? 0 : EIO;
}

static int
test_fat32_read_only_reader(void)
{
    struct fat32_ro_image image;
    struct fat_ro reader;
    struct fat_ro_node node;
    unsigned char short_name[11];
    unsigned char output[FAT32_TEST_FILE_SIZE];
    unsigned count;

    memset(&image, 0, sizeof(image));
    boot_common(image.boot, FAT32_TEST_CLUSTER_SECTORS, 32, 2,
        30298527u);
    set_le32(image.boot + 36, 29568);
    set_le32(image.boot + 44, 2);
    set_le32(image.fat + 8, 0x0fffffffu);
    set_le32(image.fat + 12, 4);
    set_le32(image.fat + 16, 0x0fffffffu);
    CHECK(fat_short_name_encode("root.txt", 8, short_name) ==
        FAT_PARSE_OK);
    fat_dirent_encode(image.root, short_name, FAT_ATTR_ARCHIVE, 3,
        sizeof(output));

    CHECK(fat_ro_mount(&reader, 30298527u, fat32_ro_image_read, &image) ==
        0);
    CHECK(reader.fr_volume.fv_type == FAT_TYPE_32);
    CHECK(fat_ro_lookup(&reader, "/ROOT.TXT", &node) == 0);
    CHECK(node.fn_cluster == 3 && node.fn_size == sizeof(output));
    CHECK(fat_ro_read(&reader, &node, 0, output, sizeof(output), &count) ==
        0);
    CHECK(count == sizeof(output));
    CHECK(memcmp(output, "REBSD FAT32 ROOT\n", 17) == 0);
    CHECK(memcmp(output + FAT32_TEST_CLUSTER_SECTORS * FAT_SECTOR_SIZE,
        "REBSD FAT32 SECOND CLUSTER\n", 27) == 0);
    return 0;
}

static int
test_image(const char *path, unsigned expected_type)
{
    unsigned char boot[FAT_SECTOR_SIZE];
    struct fat_volume volume;
    FILE *file;
    long bytes;
    unsigned media_sectors;
    unsigned partition_start;
    int parsed;

    file = fopen(path, "rb");
    CHECK(file != NULL);
    CHECK(fseek(file, 0, SEEK_END) == 0);
    bytes = ftell(file);
    CHECK(bytes >= (long)FAT_SECTOR_SIZE);
    CHECK((unsigned long)bytes / FAT_SECTOR_SIZE <= 0xfffffffful);
    CHECK(fseek(file, 0, SEEK_SET) == 0);
    CHECK(fread(boot, sizeof(boot), 1, file) == 1);
    media_sectors = (unsigned)((unsigned long)bytes / FAT_SECTOR_SIZE);
    parsed = fat_volume_parse(&volume, boot, media_sectors);
    if (parsed != FAT_PARSE_OK && boot[510] == 0x55 &&
        boot[511] == 0xaa) {
        partition_start = get_le32(boot + 446 + 8);
        media_sectors = get_le32(boot + 446 + 12);
        CHECK(partition_start <=
            (unsigned)((unsigned long)bytes / FAT_SECTOR_SIZE));
        CHECK(media_sectors <=
            (unsigned)((unsigned long)bytes / FAT_SECTOR_SIZE) -
            partition_start);
        CHECK(fseek(file, (long)((unsigned long)partition_start *
            FAT_SECTOR_SIZE), SEEK_SET) == 0);
        CHECK(fread(boot, sizeof(boot), 1, file) == 1);
        parsed = fat_volume_parse(&volume, boot, media_sectors);
    }
    CHECK(fclose(file) == 0);
    CHECK(parsed == FAT_PARSE_OK);
    CHECK(volume.fv_type == expected_type);
    printf("fat_test: %s is FAT%u (%u sectors)\n", path,
        volume.fv_type, volume.fv_total_sectors);
    return 0;
}

int
main(int argc, char **argv)
{
    CHECK(test_fat16() == 0);
    CHECK(test_fat32() == 0);
    CHECK(test_rejects_bad_bpb() == 0);
    CHECK(test_names() == 0);
    CHECK(test_fat_encoding() == 0);
    CHECK(test_directory_encoding() == 0);
    CHECK(test_read_only_reader() == 0);
    CHECK(test_fat32_read_only_reader() == 0);
    if (argc == 3)
        CHECK(test_image(argv[1], (unsigned)strtoul(argv[2], NULL, 0)) == 0);
    else if (argc != 1) {
        fprintf(stderr, "usage: %s [fat-image expected-type]\n", argv[0]);
        return 2;
    }
    puts("fat_test: all tests passed");
    return 0;
}
