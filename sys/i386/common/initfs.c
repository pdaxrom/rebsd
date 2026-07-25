#include <sys/errno.h>
#include <sys/param.h>
#include <sys/systm.h>

#include "initfs.h"

#define I386_INITFS_MAGIC          0x53464952u
#define I386_INITFS_VERSION        1u
#define I386_INITFS_MAX_ENTRIES    64u
#define I386_INITFS_MAX_PATH       255u
#define I386_INITFS_DATA_ALIGN     16u

struct i386_initfs_header {
    unsigned iih_magic;
    unsigned iih_version;
    unsigned iih_entry_count;
    unsigned iih_image_size;
};

struct i386_initfs_entry {
    unsigned iie_path_offset;
    unsigned iie_path_length;
    unsigned iie_data_offset;
    unsigned iie_data_length;
};

extern const unsigned char _binary_initfs_img_start[];
extern const unsigned char _binary_initfs_img_end[];

static int
i386_initfs_range_valid(unsigned image_size, unsigned offset,
    unsigned length)
{
    return offset <= image_size && length <= image_size - offset;
}

static int
i386_initfs_query_length(const char *path, unsigned *result)
{
    unsigned length;

    if (path == (const char *)0 || result == (unsigned *)0 ||
        path[0] != '/')
        return EINVAL;
    for (length = 0; length <= I386_INITFS_MAX_PATH; ++length) {
        if (path[length] == '\0') {
            if (length == 0)
                return EINVAL;
            *result = length + 1u;
            return 0;
        }
    }
    return ENAMETOOLONG;
}

static int
i386_initfs_path_valid(const unsigned char *path, unsigned length)
{
    unsigned index;

    if (length < 2u || length > I386_INITFS_MAX_PATH + 1u ||
        path[0] != '/' || path[length - 1u] != '\0')
        return 0;
    for (index = 0; index + 1u < length; ++index) {
        if (path[index] == '\0')
            return 0;
    }
    return 1;
}

static int
i386_initfs_path_equal(const unsigned char *left, const char *right,
    unsigned length)
{
    unsigned index;

    for (index = 0; index < length; ++index) {
        if (left[index] != (unsigned char)right[index])
            return 0;
    }
    return 1;
}

int
i386_initfs_find(const void *image, unsigned image_size, const char *path,
    struct i386_initfs_file *result)
{
    const unsigned char *bytes;
    const unsigned char *entry_path;
    struct i386_initfs_header header;
    struct i386_initfs_entry entry;
    unsigned table_size;
    unsigned table_end;
    unsigned query_length;
    unsigned index;
    int found;
    int error;

    if (image == (const void *)0 ||
        result == (struct i386_initfs_file *)0)
        return EINVAL;
    result->iif_data = (const void *)0;
    result->iif_size = 0;
    error = i386_initfs_query_length(path, &query_length);
    if (error != 0)
        return error;
    if (image_size < sizeof(header))
        return ENOEXEC;
    bytes = (const unsigned char *)image;
    bcopy(bytes, &header, sizeof(header));
    if (header.iih_magic != I386_INITFS_MAGIC ||
        header.iih_version != I386_INITFS_VERSION ||
        header.iih_entry_count == 0 ||
        header.iih_entry_count > I386_INITFS_MAX_ENTRIES ||
        header.iih_image_size != image_size ||
        header.iih_entry_count >
        (~(unsigned)sizeof(header)) / sizeof(entry))
        return ENOEXEC;
    table_size = header.iih_entry_count * sizeof(entry);
    table_end = sizeof(header) + table_size;
    if (table_end > image_size)
        return ENOEXEC;

    found = 0;
    for (index = 0; index < header.iih_entry_count; ++index) {
        bcopy(bytes + sizeof(header) + index * sizeof(entry),
            &entry, sizeof(entry));
        if (entry.iie_path_offset < table_end ||
            !i386_initfs_range_valid(image_size,
            entry.iie_path_offset, entry.iie_path_length) ||
            entry.iie_data_offset < table_end ||
            (entry.iie_data_offset & (I386_INITFS_DATA_ALIGN - 1u)) != 0 ||
            !i386_initfs_range_valid(image_size,
            entry.iie_data_offset, entry.iie_data_length))
            return ENOEXEC;
        entry_path = bytes + entry.iie_path_offset;
        if (!i386_initfs_path_valid(entry_path,
            entry.iie_path_length))
            return ENOEXEC;
        if (entry.iie_path_length == query_length &&
            i386_initfs_path_equal(entry_path, path, query_length)) {
            if (found)
                return ENOEXEC;
            result->iif_data = bytes + entry.iie_data_offset;
            result->iif_size = entry.iie_data_length;
            found = 1;
        }
    }
    return found ? 0 : ENOENT;
}

int
i386_initfs_find_embedded(const char *path,
    struct i386_initfs_file *result)
{
    unsigned long image_size;

    image_size = (unsigned long)(_binary_initfs_img_end -
        _binary_initfs_img_start);
    if (image_size > ~0u)
        return EOVERFLOW;
    return i386_initfs_find(_binary_initfs_img_start,
        (unsigned)image_size, path, result);
}
