#ifndef _I386_ELF_BOOTSTRAP_H_
#define _I386_ELF_BOOTSTRAP_H_

#define I386_ELF_MAX_LOAD_SEGMENTS 8u

struct vmspace;

struct i386_elf_segment {
    unsigned ies_start;
    unsigned ies_size;
    unsigned ies_protection;
};

struct i386_elf_image {
    unsigned iei_entry;
    unsigned iei_segment_count;
    struct i386_elf_segment iei_segments[I386_ELF_MAX_LOAD_SEGMENTS];
};

int i386_elf_load_image(struct vmspace *, const void *, unsigned,
    struct i386_elf_image *);
int i386_elf_unload_image(struct vmspace *, struct i386_elf_image *);

#endif
