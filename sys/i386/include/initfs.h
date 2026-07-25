#ifndef _I386_INITFS_H_
#define _I386_INITFS_H_

struct i386_initfs_file {
    const void *iif_data;
    unsigned iif_size;
};

int i386_initfs_find(const void *, unsigned, const char *,
    struct i386_initfs_file *);
int i386_initfs_find_embedded(const char *, struct i386_initfs_file *);

#endif
