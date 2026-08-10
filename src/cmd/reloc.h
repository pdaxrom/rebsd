#ifndef REBSD_CMD_RELOC_H
#define REBSD_CMD_RELOC_H

/* Internal relocation representation shared by the MIPS assembler/linker. */
struct reloc {
    unsigned flags;
#define RSMASK  0x70
#define RABS        0
#define RCTORS      0x10
#define RTEXT       0x20
#define RDATA       0x30
#define RBSS        0x40
#define RDTORS      0x50
#define RSTRNG      0x60
#define REXT        0x70
#define RGPREL  0x08
#define RFMASK  0x07
#define RBYTE16     0x00
#define RBYTE32     0x01
#define RHIGH16     0x02
#define RHIGH16S    0x03
#define RWORD16     0x04
#define RWORD26     0x05
    unsigned index;
    unsigned offset;
};

#endif
