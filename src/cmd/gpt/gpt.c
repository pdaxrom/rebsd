/* Compact GPT editor for ReBSD and host-side image tests. */

#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>

#ifndef GPT_HOST
#include <sys/disk.h>
#include <ioctl.h>
#endif

#ifndef O_BINARY
#define O_BINARY 0
#endif

#define GPT_SECTOR_SIZE             512u
#define GPT_HEADER_SIZE             92u
#define GPT_ENTRY_SIZE              128u
#define GPT_ENTRY_COUNT             128u
#define GPT_ENTRY_BYTES             (GPT_ENTRY_SIZE * GPT_ENTRY_COUNT)
#define GPT_ENTRY_SECTORS           (GPT_ENTRY_BYTES / GPT_SECTOR_SIZE)
#define GPT_FIRST_USABLE            34u
#define GPT_ALIGNMENT               2048u

typedef unsigned long long gpt_lba_t;

enum gpt_action {
    GPT_ACTION_NONE = 0,
    GPT_ACTION_PRINT,
    GPT_ACTION_CREATE,
    GPT_ACTION_MIGRATE,
    GPT_ACTION_ADD,
    GPT_ACTION_DELETE
};

struct gpt_header {
    gpt_lba_t current_lba;
    gpt_lba_t alternate_lba;
    gpt_lba_t first_usable;
    gpt_lba_t last_usable;
    gpt_lba_t entries_lba;
    unsigned entries_crc;
    unsigned char disk_guid[16];
};

struct gpt_table {
    gpt_lba_t media_sectors;
    gpt_lba_t first_usable;
    gpt_lba_t last_usable;
    unsigned char disk_guid[16];
    unsigned char entries[GPT_ENTRY_BYTES];
    int from_backup;
};

static struct gpt_table table;

static void
usage(void)
{
    fprintf(stderr,
        "usage: gpt -p device\n"
        "       gpt -c device\n"
        "       gpt -m [-n] device\n"
        "       gpt -a [-b first-sector] [-s sectors] [-t type-guid]\n"
        "           [-l label] device [partition]\n"
        "       gpt -d device partition\n"
        "\n"
        "       -p  print and validate GPT without writing\n"
        "       -c  create an empty GPT (destructive)\n"
        "       -m  migrate primary MBR partitions without moving data\n"
        "       -n  validate and print the migration plan without writing\n"
        "       -a  add a partition; defaults to first free entry and gap\n"
        "       -d  delete one GPT entry\n"
        "       -b  explicit first 512-byte sector\n"
        "       -s  explicit size in 512-byte sectors\n"
        "       -t  canonical partition type GUID\n"
        "       -l  ASCII partition label, at most 36 characters\n");
}

static int
select_action(enum gpt_action *action, enum gpt_action requested)
{
    if (*action != GPT_ACTION_NONE) {
        fprintf(stderr, "gpt: specify exactly one operation\n");
        return -1;
    }
    *action = requested;
    return 0;
}

static unsigned
get_le32(const unsigned char *data)
{
    return (unsigned)data[0] | ((unsigned)data[1] << 8) |
        ((unsigned)data[2] << 16) | ((unsigned)data[3] << 24);
}

static unsigned
get_le16(const unsigned char *data)
{
    return (unsigned)data[0] | ((unsigned)data[1] << 8);
}

static gpt_lba_t
get_le64(const unsigned char *data)
{
    return (gpt_lba_t)get_le32(data) |
        ((gpt_lba_t)get_le32(data + 4) << 32);
}

static void
put_le16(unsigned char *data, unsigned value)
{
    data[0] = (unsigned char)value;
    data[1] = (unsigned char)(value >> 8);
}

static void
put_le32(unsigned char *data, unsigned value)
{
    data[0] = (unsigned char)value;
    data[1] = (unsigned char)(value >> 8);
    data[2] = (unsigned char)(value >> 16);
    data[3] = (unsigned char)(value >> 24);
}

static void
put_le64(unsigned char *data, gpt_lba_t value)
{
    put_le32(data, (unsigned)value);
    put_le32(data + 4, (unsigned)(value >> 32));
}

static unsigned
crc32_update(unsigned crc, const void *arg, size_t length)
{
    const unsigned char *data;
    unsigned i;

    data = (const unsigned char *)arg;
    while (length-- != 0) {
        crc ^= *data++;
        for (i = 0; i < 8u; ++i)
            crc = (crc >> 1) ^ ((crc & 1u) ? 0xedb88320u : 0u);
    }
    return crc;
}

static unsigned
crc32(const void *arg, size_t length)
{
    return crc32_update(0xffffffffu, arg, length) ^ 0xffffffffu;
}

static int
parse_u64(const char *text, gpt_lba_t *value)
{
    gpt_lba_t result;
    gpt_lba_t limit;
    unsigned base;
    unsigned digit;
    const char *p;

    if (text == 0 || text[0] == 0 || text[0] == '-')
        return -1;
    p = text;
    base = 10;
    if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
        base = 16;
        p += 2;
        if (*p == 0)
            return -1;
    }
    result = 0;
    limit = ~(gpt_lba_t)0;
    while (*p != 0) {
        if (*p >= '0' && *p <= '9')
            digit = (unsigned)(*p - '0');
        else if (*p >= 'a' && *p <= 'f')
            digit = (unsigned)(*p - 'a') + 10u;
        else if (*p >= 'A' && *p <= 'F')
            digit = (unsigned)(*p - 'A') + 10u;
        else
            return -1;
        if (digit >= base || result > (limit - digit) / base)
            return -1;
        result = result * base + digit;
        ++p;
    }
    *value = result;
    return 0;
}

static int
parse_number(const char *text, unsigned limit, unsigned *value)
{
    gpt_lba_t parsed;

    if (parse_u64(text, &parsed) != 0 || parsed == 0 || parsed > limit)
        return -1;
    *value = (unsigned)parsed;
    return 0;
}

static int
hex_digit(int ch)
{
    if (ch >= '0' && ch <= '9')
        return ch - '0';
    if (ch >= 'a' && ch <= 'f')
        return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F')
        return ch - 'A' + 10;
    return -1;
}

static int
parse_guid(const char *text, unsigned char guid[16])
{
    unsigned char canonical[16];
    unsigned nibble;
    unsigned byte;
    int value;

    nibble = 0;
    for (; *text != 0; ++text) {
        if (*text == '-')
            continue;
        value = hex_digit((unsigned char)*text);
        if (value < 0 || nibble >= 32u)
            return -1;
        byte = nibble >> 1;
        if ((nibble & 1u) == 0)
            canonical[byte] = (unsigned char)(value << 4);
        else
            canonical[byte] |= (unsigned char)value;
        ++nibble;
    }
    if (nibble != 32u)
        return -1;
    guid[0] = canonical[3]; guid[1] = canonical[2];
    guid[2] = canonical[1]; guid[3] = canonical[0];
    guid[4] = canonical[5]; guid[5] = canonical[4];
    guid[6] = canonical[7]; guid[7] = canonical[6];
    memcpy(guid + 8, canonical + 8, 8u);
    return 0;
}

static void
format_guid(char text[37], const unsigned char guid[16])
{
    (void)snprintf(text, 37,
        "%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x",
        get_le32(guid), get_le16(guid + 4), get_le16(guid + 6),
        guid[8], guid[9], guid[10], guid[11], guid[12], guid[13],
        guid[14], guid[15]);
}

static int
guid_is_zero(const unsigned char guid[16])
{
    unsigned i;

    for (i = 0; i < 16u; ++i)
        if (guid[i] != 0)
            return 0;
    return 1;
}

static gpt_lba_t
random_step(gpt_lba_t *state)
{
    gpt_lba_t value;

    value = *state;
    value ^= value << 13;
    value ^= value >> 7;
    value ^= value << 17;
    if (value == 0)
        value = 0x9e3779b97f4a7c15ULL;
    *state = value;
    return value;
}

static void
make_guid(unsigned char guid[16], gpt_lba_t *state)
{
    gpt_lba_t a;
    gpt_lba_t b;
    unsigned i;

    a = random_step(state);
    b = random_step(state);
    for (i = 0; i < 8u; ++i) {
        guid[i] = (unsigned char)(a >> (i * 8u));
        guid[i + 8u] = (unsigned char)(b >> (i * 8u));
    }
    guid[7] = (unsigned char)((guid[7] & 0x0fu) | 0x40u);
    guid[8] = (unsigned char)((guid[8] & 0x3fu) | 0x80u);
}

static int
read_exact_at(int fd, gpt_lba_t lba, void *data, size_t length)
{
    unsigned char *cursor;
    off_t offset;
    ssize_t count;

    if (lba > 0x003fffffffffffffULL)
        return -1;
    offset = (off_t)(lba * GPT_SECTOR_SIZE);
    if (lseek(fd, offset, SEEK_SET) != offset)
        return -1;
    cursor = (unsigned char *)data;
    while (length != 0) {
        count = read(fd, cursor, length);
        if (count < 0 && errno == EINTR)
            continue;
        if (count <= 0)
            return -1;
        cursor += count;
        length -= (size_t)count;
    }
    return 0;
}

static int
write_exact_at(int fd, gpt_lba_t lba, const void *data, size_t length)
{
    const unsigned char *cursor;
    off_t offset;
    ssize_t count;

    if (lba > 0x003fffffffffffffULL)
        return -1;
    offset = (off_t)(lba * GPT_SECTOR_SIZE);
    if (lseek(fd, offset, SEEK_SET) != offset)
        return -1;
    cursor = (const unsigned char *)data;
    while (length != 0) {
        count = write(fd, cursor, length);
        if (count < 0 && errno == EINTR)
            continue;
        if (count <= 0)
            return -1;
        cursor += count;
        length -= (size_t)count;
    }
    return 0;
}

static int
get_media_sectors(int fd, gpt_lba_t *sectors)
{
    off_t current;
    off_t end;

#ifndef GPT_HOST
    disk_sector_t kernel_sectors;

    kernel_sectors = 0;
    if (ioctl(fd, DIOCGETSECTORS64, &kernel_sectors) == 0 &&
        kernel_sectors != 0) {
        *sectors = kernel_sectors;
        return 0;
    }
#endif
    current = lseek(fd, 0, SEEK_CUR);
    end = lseek(fd, 0, SEEK_END);
    if (current >= 0)
        (void)lseek(fd, current, SEEK_SET);
    if (end < 0 || (end & (GPT_SECTOR_SIZE - 1u)) != 0)
        return -1;
    *sectors = (gpt_lba_t)end / GPT_SECTOR_SIZE;
    return *sectors != 0 ? 0 : -1;
}

static int
protective_mbr_valid(const unsigned char sector[GPT_SECTOR_SIZE])
{
    return sector[510] == 0x55 && sector[511] == 0xaa &&
        sector[446 + 4] == 0xee && get_le32(sector + 446 + 8) == 1;
}

static int
decode_header(struct gpt_header *header, unsigned char sector[GPT_SECTOR_SIZE],
    gpt_lba_t header_lba, gpt_lba_t media_sectors)
{
    unsigned stored_crc;
    unsigned actual_crc;

    if (memcmp(sector, "EFI PART", 8u) != 0 ||
        get_le32(sector + 8) != 0x00010000u ||
        get_le32(sector + 12) != GPT_HEADER_SIZE ||
        get_le32(sector + 20) != 0)
        return -1;
    stored_crc = get_le32(sector + 16);
    put_le32(sector + 16, 0);
    actual_crc = crc32(sector, GPT_HEADER_SIZE);
    put_le32(sector + 16, stored_crc);
    if (stored_crc != actual_crc)
        return -1;
    header->current_lba = get_le64(sector + 24);
    header->alternate_lba = get_le64(sector + 32);
    header->first_usable = get_le64(sector + 40);
    header->last_usable = get_le64(sector + 48);
    memcpy(header->disk_guid, sector + 56, 16u);
    header->entries_lba = get_le64(sector + 72);
    header->entries_crc = get_le32(sector + 88);
    if (header->current_lba != header_lba ||
        header->alternate_lba !=
        (header_lba == 1 ? media_sectors - 1u : 1u) ||
        header->first_usable < GPT_FIRST_USABLE ||
        header->first_usable > header->last_usable ||
        header->last_usable > media_sectors - GPT_FIRST_USABLE ||
        get_le32(sector + 80) != GPT_ENTRY_COUNT ||
        get_le32(sector + 84) != GPT_ENTRY_SIZE ||
        header->entries_lba >= media_sectors ||
        GPT_ENTRY_SECTORS > media_sectors - header->entries_lba)
        return -1;
    if (header_lba == 1) {
        if (header->entries_lba <= 1 ||
            header->entries_lba + GPT_ENTRY_SECTORS >
            header->first_usable)
            return -1;
    } else if (header->entries_lba <= header->last_usable ||
        header->entries_lba + GPT_ENTRY_SECTORS > header_lba) {
        return -1;
    }
    return 0;
}

static int
entry_used(const unsigned char *entry)
{
    return !guid_is_zero(entry);
}

static int
validate_entries(const struct gpt_table *gpt)
{
    const unsigned char *entry;
    const unsigned char *other;
    gpt_lba_t first;
    gpt_lba_t last;
    gpt_lba_t other_first;
    gpt_lba_t other_last;
    unsigned i;
    unsigned j;

    for (i = 0; i < GPT_ENTRY_COUNT; ++i) {
        entry = gpt->entries + i * GPT_ENTRY_SIZE;
        if (!entry_used(entry))
            continue;
        if (guid_is_zero(entry + 16u))
            return -1;
        first = get_le64(entry + 32u);
        last = get_le64(entry + 40u);
        if (first < gpt->first_usable || last > gpt->last_usable ||
            first > last)
            return -1;
        for (j = 0; j < i; ++j) {
            other = gpt->entries + j * GPT_ENTRY_SIZE;
            if (!entry_used(other))
                continue;
            other_first = get_le64(other + 32u);
            other_last = get_le64(other + 40u);
            if (first <= other_last && other_first <= last)
                return -1;
        }
    }
    return 0;
}

static int
load_at(int fd, gpt_lba_t header_lba, struct gpt_table *gpt)
{
    unsigned char sector[GPT_SECTOR_SIZE];
    struct gpt_header header;

    if (read_exact_at(fd, header_lba, sector, sizeof(sector)) != 0 ||
        decode_header(&header, sector, header_lba, gpt->media_sectors) != 0 ||
        read_exact_at(fd, header.entries_lba, gpt->entries,
        sizeof(gpt->entries)) != 0 ||
        crc32(gpt->entries, sizeof(gpt->entries)) != header.entries_crc)
        return -1;
    gpt->first_usable = header.first_usable;
    gpt->last_usable = header.last_usable;
    memcpy(gpt->disk_guid, header.disk_guid, 16u);
    return validate_entries(gpt);
}

static int
load_table(int fd, struct gpt_table *gpt)
{
    unsigned char mbr[GPT_SECTOR_SIZE];

    if (get_media_sectors(fd, &gpt->media_sectors) != 0 ||
        gpt->media_sectors < 2u * GPT_FIRST_USABLE ||
        read_exact_at(fd, 0, mbr, sizeof(mbr)) != 0 ||
        !protective_mbr_valid(mbr))
        return -1;
    gpt->from_backup = 0;
    if (load_at(fd, 1, gpt) == 0)
        return 0;
    if (load_at(fd, gpt->media_sectors - 1u, gpt) == 0) {
        gpt->from_backup = 1;
        return 0;
    }
    return -1;
}

static void
build_header(unsigned char sector[GPT_SECTOR_SIZE], const struct gpt_table *gpt,
    gpt_lba_t current_lba, gpt_lba_t entries_lba, unsigned entries_crc)
{
    memset(sector, 0, GPT_SECTOR_SIZE);
    memcpy(sector, "EFI PART", 8u);
    put_le32(sector + 8, 0x00010000u);
    put_le32(sector + 12, GPT_HEADER_SIZE);
    put_le64(sector + 24, current_lba);
    put_le64(sector + 32,
        current_lba == 1 ? gpt->media_sectors - 1u : 1u);
    put_le64(sector + 40, gpt->first_usable);
    put_le64(sector + 48, gpt->last_usable);
    memcpy(sector + 56, gpt->disk_guid, 16u);
    put_le64(sector + 72, entries_lba);
    put_le32(sector + 80, GPT_ENTRY_COUNT);
    put_le32(sector + 84, GPT_ENTRY_SIZE);
    put_le32(sector + 88, entries_crc);
    put_le32(sector + 16, crc32(sector, GPT_HEADER_SIZE));
}

static void
build_pmbr(unsigned char sector[GPT_SECTOR_SIZE], gpt_lba_t media_sectors,
    const unsigned char *original_mbr)
{
    gpt_lba_t protected_sectors;

    memset(sector, 0, GPT_SECTOR_SIZE);
    if (original_mbr != 0)
        memcpy(sector, original_mbr, 446u);
    memset(sector + 446u, 0, 64u);
    sector[446 + 1] = 0x00;
    sector[446 + 2] = 0x02;
    sector[446 + 3] = 0x00;
    sector[446 + 4] = 0xee;
    sector[446 + 5] = 0xff;
    sector[446 + 6] = 0xff;
    sector[446 + 7] = 0xff;
    put_le32(sector + 446 + 8, 1u);
    protected_sectors = media_sectors - 1u;
    put_le32(sector + 446 + 12,
        protected_sectors > 0xffffffffu ? 0xffffffffu :
        (unsigned)protected_sectors);
    sector[510] = 0x55;
    sector[511] = 0xaa;
}

static int
write_table(int fd, const struct gpt_table *gpt,
    const unsigned char *original_mbr)
{
    unsigned char sector[GPT_SECTOR_SIZE];
    gpt_lba_t backup_entries;
    unsigned entries_crc;
#ifndef GPT_HOST
    struct stat status;
#endif

    if (validate_entries(gpt) != 0)
        return -1;
    entries_crc = crc32(gpt->entries, sizeof(gpt->entries));
    backup_entries = gpt->media_sectors - 1u - GPT_ENTRY_SECTORS;

    /* UEFI requires updating the backup table before the primary table. */
    if (write_exact_at(fd, backup_entries, gpt->entries,
        sizeof(gpt->entries)) != 0)
        return -1;
    build_header(sector, gpt, gpt->media_sectors - 1u, backup_entries,
        entries_crc);
    if (write_exact_at(fd, gpt->media_sectors - 1u, sector,
        sizeof(sector)) != 0 || fsync(fd) != 0)
        return -1;

    if (write_exact_at(fd, 2u, gpt->entries, sizeof(gpt->entries)) != 0)
        return -1;
    build_header(sector, gpt, 1u, 2u, entries_crc);
    if (write_exact_at(fd, 1u, sector, sizeof(sector)) != 0 ||
        fsync(fd) != 0)
        return -1;
    /* Make the complete GPT durable before replacing a legacy MBR. */
    build_pmbr(sector, gpt->media_sectors, original_mbr);
    if (write_exact_at(fd, 0, sector, sizeof(sector)) != 0 ||
        fsync(fd) != 0)
        return -1;
#ifndef GPT_HOST
    if (fstat(fd, &status) != 0)
        return -1;
    if (S_ISBLK(status.st_mode) && ioctl(fd, DIOCREINIT) != 0)
        return -1;
#endif
    return 0;
}

static int
initialize_table(int fd, struct gpt_table *gpt)
{
    gpt_lba_t seed;

    memset(gpt, 0, sizeof(*gpt));
    if (get_media_sectors(fd, &gpt->media_sectors) != 0 ||
        gpt->media_sectors < 2u * GPT_FIRST_USABLE) {
        fprintf(stderr, "gpt: device is too small or has unknown size\n");
        return -1;
    }
    gpt->first_usable = GPT_FIRST_USABLE;
    gpt->last_usable = gpt->media_sectors - GPT_FIRST_USABLE;
    seed = (gpt_lba_t)time(0) ^ ((gpt_lba_t)getpid() << 32) ^
        gpt->media_sectors;
    make_guid(gpt->disk_guid, &seed);
    return 0;
}

static int
mbr_type_guid(unsigned type, unsigned char guid[16], const char **name)
{
    const char *text;

    switch (type) {
    case 0x01: case 0x04: case 0x06: case 0x07:
    case 0x0b: case 0x0c: case 0x0e:
    case 0x11: case 0x14: case 0x16: case 0x17:
    case 0x1b: case 0x1c: case 0x1e:
        text = "EBD0A0A2-B9E5-4433-87C0-68B6B72699C7";
        *name = "Microsoft Basic Data";
        break;
    case 0x82:
        text = "0657FD6D-A4AB-43C4-84E5-0933C84B4F4F";
        *name = "Linux swap";
        break;
    case 0x83:
        text = "0FC63DAF-8483-4772-8E79-3D69D8477DE4";
        *name = "Linux filesystem";
        break;
    case 0x8e:
        text = "E6D6D379-F507-44C2-A23C-238F2A3DF928";
        *name = "Linux LVM";
        break;
    case 0xa5:
        text = "516E7CB4-6ECF-11D6-8FF8-00022D09712B";
        *name = "FreeBSD data";
        break;
    case 0xaf:
        text = "48465300-0000-11AA-AA11-00306543ECAC";
        *name = "Apple HFS/HFS+";
        break;
    case 0xef:
        text = "C12A7328-F81F-11D2-BA4B-00A0C93EC93B";
        *name = "EFI System";
        break;
    case 0xfd:
        text = "A19D880F-05FC-4D3B-A006-743F0F84911E";
        *name = "Linux RAID";
        break;
    default:
        return -1;
    }
    return parse_guid(text, guid);
}

static int
migrate_mbr(int fd, struct gpt_table *gpt,
    unsigned char original_mbr[GPT_SECTOR_SIZE])
{
    unsigned char *entry;
    unsigned char type_guid[16];
    const unsigned char *mbr_entry;
    const char *type_name;
    gpt_lba_t seed;
    gpt_lba_t first;
    gpt_lba_t sectors;
    gpt_lba_t last;
    unsigned status;
    unsigned type;
    unsigned count;
    unsigned i;

    if (initialize_table(fd, gpt) != 0 ||
        read_exact_at(fd, 0, original_mbr, GPT_SECTOR_SIZE) != 0) {
        fprintf(stderr, "gpt: cannot read the legacy MBR\n");
        return -1;
    }
    if (original_mbr[510] != 0x55 || original_mbr[511] != 0xaa) {
        fprintf(stderr, "gpt: no valid legacy MBR signature\n");
        return -1;
    }
    for (i = 0; i < 4u; ++i) {
        mbr_entry = original_mbr + 446u + i * 16u;
        if (mbr_entry[4] == 0xee) {
            fprintf(stderr, "gpt: MBR is already protective or hybrid\n");
            return -1;
        }
    }

    seed = (gpt_lba_t)time(0) ^ ((gpt_lba_t)getpid() << 32) ^
        gpt->media_sectors;
    count = 0;
    for (i = 0; i < 4u; ++i) {
        mbr_entry = original_mbr + 446u + i * 16u;
        status = mbr_entry[0];
        type = mbr_entry[4];
        first = get_le32(mbr_entry + 8u);
        sectors = get_le32(mbr_entry + 12u);
        if (type == 0)
            continue;
        if (status != 0 && status != 0x80) {
            fprintf(stderr, "gpt: MBR partition %u has invalid status 0x%x\n",
                i + 1u, status);
            return -1;
        }
        if (type == 0x05 || type == 0x0f || type == 0x85) {
            fprintf(stderr,
                "gpt: MBR partition %u is extended; logical partitions "
                "cannot be migrated safely\n", i + 1u);
            return -1;
        }
        if (sectors == 0) {
            fprintf(stderr, "gpt: MBR partition %u has zero length\n",
                i + 1u);
            return -1;
        }
        last = first + sectors - 1u;
        if (last < first || first < gpt->first_usable ||
            last > gpt->last_usable) {
            fprintf(stderr,
                "gpt: MBR partition %u overlaps GPT metadata or the "
                "media boundary\n", i + 1u);
            return -1;
        }
        if (mbr_type_guid(type, type_guid, &type_name) != 0) {
            fprintf(stderr,
                "gpt: MBR partition %u type 0x%02x has no safe GPT mapping\n",
                i + 1u, type);
            return -1;
        }
        entry = gpt->entries + i * GPT_ENTRY_SIZE;
        memcpy(entry, type_guid, 16u);
        make_guid(entry + 16u, &seed);
        put_le64(entry + 32u, first);
        put_le64(entry + 40u, last);
        printf("gpt: MBR partition %u type=0x%02x -> %s, "
            "sectors %llu..%llu\n", i + 1u, type, type_name, first, last);
        ++count;
    }
    if (count == 0) {
        fprintf(stderr, "gpt: legacy MBR contains no partitions\n");
        return -1;
    }
    if (validate_entries(gpt) != 0) {
        fprintf(stderr, "gpt: legacy MBR partitions overlap\n");
        return -1;
    }
    printf("gpt: migration preserves %u partition start%s and size%s; "
        "payload sectors will not move\n", count, count == 1u ? "" : "s",
        count == 1u ? "" : "s");
    return 0;
}

static void
entry_label(char text[37], const unsigned char *entry)
{
    unsigned value;
    unsigned i;

    for (i = 0; i < 36u; ++i) {
        value = get_le16(entry + 56u + i * 2u);
        if (value == 0)
            break;
        text[i] = value >= 0x20u && value <= 0x7eu ? (char)value : '?';
    }
    text[i] = 0;
}

static void
print_table(const char *device, const struct gpt_table *gpt)
{
    const unsigned char *entry;
    char type[37];
    char unique[37];
    char label[37];
    gpt_lba_t first;
    gpt_lba_t last;
    unsigned i;

    printf("%s: %llu sectors, usable %llu..%llu, %s GPT\n", device,
        gpt->media_sectors, gpt->first_usable, gpt->last_usable,
        gpt->from_backup ? "backup" : "primary");
    printf("Part                Start                  End"
        "              Sectors Label\n");
    for (i = 0; i < GPT_ENTRY_COUNT; ++i) {
        entry = gpt->entries + i * GPT_ENTRY_SIZE;
        if (!entry_used(entry))
            continue;
        first = get_le64(entry + 32u);
        last = get_le64(entry + 40u);
        format_guid(type, entry);
        format_guid(unique, entry + 16u);
        entry_label(label, entry);
        printf("%4u %20llu %20llu %20llu %s\n", i + 1u, first, last,
            last - first + 1u, label[0] != 0 ? label : "-");
        printf("     type=%s unique=%s attrs=0x%llx\n", type, unique,
            get_le64(entry + 48u));
    }
}

static gpt_lba_t
align_up(gpt_lba_t value, gpt_lba_t alignment)
{
    gpt_lba_t remainder;

    remainder = value % alignment;
    if (remainder == 0)
        return value;
    if (value > ~(gpt_lba_t)0 - (alignment - remainder))
        return ~(gpt_lba_t)0;
    return value + alignment - remainder;
}

static int
find_gap(const struct gpt_table *gpt, gpt_lba_t requested_start,
    gpt_lba_t requested_sectors, gpt_lba_t *startp, gpt_lba_t *sectorsp)
{
    const unsigned char *entry;
    gpt_lba_t candidate;
    gpt_lba_t next;
    gpt_lba_t first;
    gpt_lba_t last;
    unsigned i;
    int moved;

    candidate = requested_start;
    if (candidate == 0) {
        candidate = align_up(gpt->first_usable, GPT_ALIGNMENT);
        if (candidate > gpt->last_usable)
            candidate = gpt->first_usable;
    }
again:
    if (candidate < gpt->first_usable || candidate > gpt->last_usable)
        return -1;
    moved = 0;
    next = gpt->last_usable + 1u;
    for (i = 0; i < GPT_ENTRY_COUNT; ++i) {
        entry = gpt->entries + i * GPT_ENTRY_SIZE;
        if (!entry_used(entry))
            continue;
        first = get_le64(entry + 32u);
        last = get_le64(entry + 40u);
        if (candidate >= first && candidate <= last) {
            if (requested_start != 0)
                return -1;
            candidate = align_up(last + 1u, GPT_ALIGNMENT);
            moved = 1;
            break;
        }
        if (first > candidate && first < next)
            next = first;
    }
    if (moved)
        goto again;
    if (requested_sectors == 0)
        requested_sectors = next - candidate;
    if (requested_sectors == 0 || requested_sectors > next - candidate)
        return -1;
    *startp = candidate;
    *sectorsp = requested_sectors;
    return 0;
}

static int
add_partition(struct gpt_table *gpt, unsigned number,
    gpt_lba_t requested_start, gpt_lba_t requested_sectors,
    const unsigned char type_guid[16], const char *label)
{
    unsigned char *entry;
    gpt_lba_t start;
    gpt_lba_t sectors;
    gpt_lba_t seed;
    unsigned length;
    unsigned i;

    if (number == 0) {
        for (number = 1; number <= GPT_ENTRY_COUNT; ++number)
            if (!entry_used(gpt->entries +
                (number - 1u) * GPT_ENTRY_SIZE))
                break;
        if (number > GPT_ENTRY_COUNT) {
            fprintf(stderr, "gpt: partition entry array is full\n");
            return -1;
        }
    }
    entry = gpt->entries + (number - 1u) * GPT_ENTRY_SIZE;
    if (entry_used(entry)) {
        fprintf(stderr, "gpt: partition %u already exists\n", number);
        return -1;
    }
    if (find_gap(gpt, requested_start, requested_sectors, &start,
        &sectors) != 0) {
        fprintf(stderr, "gpt: requested partition does not fit\n");
        return -1;
    }
    memset(entry, 0, GPT_ENTRY_SIZE);
    memcpy(entry, type_guid, 16u);
    seed = (gpt_lba_t)time(0) ^ ((gpt_lba_t)getpid() << 32) ^ start ^
        ((gpt_lba_t)number << 48);
    make_guid(entry + 16u, &seed);
    put_le64(entry + 32u, start);
    put_le64(entry + 40u, start + sectors - 1u);
    length = label == 0 ? 0 : (unsigned)strlen(label);
    for (i = 0; i < length; ++i)
        put_le16(entry + 56u + i * 2u, (unsigned char)label[i]);
    printf("gpt: created partition %u at sector %llu, %llu sectors\n",
        number, start, sectors);
    return 0;
}

static int
delete_partition(struct gpt_table *gpt, unsigned number)
{
    unsigned char *entry;

    entry = gpt->entries + (number - 1u) * GPT_ENTRY_SIZE;
    if (!entry_used(entry)) {
        fprintf(stderr, "gpt: partition %u does not exist\n", number);
        return -1;
    }
    memset(entry, 0, GPT_ENTRY_SIZE);
    return 0;
}

static int
valid_label(const char *label)
{
    const unsigned char *p;
    unsigned length;

    if (label == 0)
        return 1;
    length = (unsigned)strlen(label);
    if (length > 36u)
        return 0;
    p = (const unsigned char *)label;
    while (*p != 0) {
        if (*p < 0x20u || *p > 0x7eu)
            return 0;
        ++p;
    }
    return 1;
}

int
main(int argc, char **argv)
{
    static const char default_type[] =
        "EBD0A0A2-B9E5-4433-87C0-68B6B72699C7";
    enum gpt_action action;
    const char *device;
    const char *type_text;
    const char *label;
    unsigned char type_guid[16];
    unsigned char original_mbr[GPT_SECTOR_SIZE];
    gpt_lba_t requested_start;
    gpt_lba_t requested_sectors;
    unsigned number;
    int open_flags;
    int fd;
    int opt;
    int remaining;
    int result;
    int dry_run;

    action = GPT_ACTION_NONE;
    type_text = default_type;
    label = 0;
    requested_start = 0;
    requested_sectors = 0;
    dry_run = 0;
    while ((opt = getopt(argc, argv, "pcmadnb:s:t:l:")) != -1) {
        switch (opt) {
        case 'p':
            if (select_action(&action, GPT_ACTION_PRINT) != 0)
                return 2;
            break;
        case 'c':
            if (select_action(&action, GPT_ACTION_CREATE) != 0)
                return 2;
            break;
        case 'm':
            if (select_action(&action, GPT_ACTION_MIGRATE) != 0)
                return 2;
            break;
        case 'n':
            dry_run = 1;
            break;
        case 'a':
            if (select_action(&action, GPT_ACTION_ADD) != 0)
                return 2;
            break;
        case 'd':
            if (select_action(&action, GPT_ACTION_DELETE) != 0)
                return 2;
            break;
        case 'b':
            if (parse_u64(optarg, &requested_start) != 0 ||
                requested_start == 0) {
                fprintf(stderr, "gpt: invalid first sector: %s\n", optarg);
                return 2;
            }
            break;
        case 's':
            if (parse_u64(optarg, &requested_sectors) != 0 ||
                requested_sectors == 0) {
                fprintf(stderr, "gpt: invalid sector count: %s\n", optarg);
                return 2;
            }
            break;
        case 't':
            type_text = optarg;
            break;
        case 'l':
            label = optarg;
            break;
        default:
            usage();
            return 2;
        }
    }
    remaining = argc - optind;
    if (action == GPT_ACTION_NONE || remaining < 1 ||
        ((action == GPT_ACTION_PRINT || action == GPT_ACTION_CREATE ||
        action == GPT_ACTION_MIGRATE) && remaining != 1) ||
        (action == GPT_ACTION_DELETE && remaining != 2) ||
        (action == GPT_ACTION_ADD && remaining > 2) ||
        (dry_run && action != GPT_ACTION_MIGRATE) ||
        (action != GPT_ACTION_ADD && (requested_start != 0 ||
        requested_sectors != 0 || label != 0 ||
        strcmp(type_text, default_type) != 0))) {
        usage();
        return 2;
    }
    if (parse_guid(type_text, type_guid) != 0 || guid_is_zero(type_guid)) {
        fprintf(stderr, "gpt: invalid type GUID: %s\n", type_text);
        return 2;
    }
    if (!valid_label(label)) {
        fprintf(stderr, "gpt: label must be at most 36 printable ASCII characters\n");
        return 2;
    }
    device = argv[optind++];
    number = 0;
    if (optind < argc &&
        parse_number(argv[optind], GPT_ENTRY_COUNT, &number) != 0) {
        fprintf(stderr, "gpt: invalid partition number: %s\n", argv[optind]);
        return 2;
    }
    open_flags = action == GPT_ACTION_PRINT || dry_run ? O_RDONLY : O_RDWR;
    fd = open(device, open_flags | O_BINARY);
    if (fd < 0) {
        perror(device);
        return 1;
    }
    result = 1;
    if (action == GPT_ACTION_CREATE) {
        if (initialize_table(fd, &table) != 0)
            goto done;
    } else if (action == GPT_ACTION_MIGRATE) {
        if (migrate_mbr(fd, &table, original_mbr) != 0)
            goto done;
        if (dry_run) {
            printf("gpt: migration check passed; no data written\n");
            result = 0;
            goto done;
        }
    } else if (load_table(fd, &table) != 0) {
        fprintf(stderr, "gpt: no valid protective MBR and GPT pair\n");
        goto done;
    }
    if (action == GPT_ACTION_PRINT) {
        print_table(device, &table);
        result = 0;
        goto done;
    }
    if (table.from_backup)
        fprintf(stderr, "gpt: warning: using backup GPT; write will repair both copies\n");
    if (action == GPT_ACTION_ADD && add_partition(&table, number,
        requested_start, requested_sectors, type_guid, label) != 0)
        goto done;
    if (action == GPT_ACTION_DELETE &&
        delete_partition(&table, number) != 0)
        goto done;
    if (write_table(fd, &table,
        action == GPT_ACTION_MIGRATE ? original_mbr : 0) != 0) {
        perror("gpt: write");
        goto done;
    }
    if (action == GPT_ACTION_MIGRATE)
        printf("gpt: legacy MBR converted; partition payload sectors "
            "were not moved\n");
    result = 0;
done:
    if (close(fd) != 0 && result == 0)
        result = 1;
    return result;
}
