MACHINE     = mips
DESTDIR     ?= $(TOPSRC)
RELEASE     = 0.0
BUILD       = $(shell git rev-list HEAD --count)
VERSION     = $(RELEASE)-$(BUILD)

MIPS_TOOLCHAIN ?= /Users/sash/Library/n64-toolchain-opengl
MIPS_PREFIX    = $(MIPS_TOOLCHAIN)/bin/mips64-elf-

MIPS_ARCH      ?= -EB -mips32 -mabi=32
MIPS_CODE      ?= -G0 -mno-abicalls -fno-pic -ffreestanding -fno-builtin \
                  -fomit-frame-pointer
MIPS_TARGET_CPU ?= $(if $(MIPS_ROOTFS_CPU),$(MIPS_ROOTFS_CPU),mips32r2)
MIPS_TARGET_CPU_CFLAGS_vr4300 = -DTARGET_VR4300 -DTARGET_MIPS_STRICT_ALIGN64 \
                                -DTARGET_MIPS_SH_ALLOC_GUARD
MIPS_TARGET_CPU_CFLAGS_mips32r2 = -DTARGET_MIPS32R2 \
                                  -DTARGET_MIPS_SH_ALLOC_GUARD
MIPS_TARGET_ENDIAN ?= $(if $(MIPS_ROOTFS_ENDIAN),$(MIPS_ROOTFS_ENDIAN),big)
MIPS_TARGET_ENDIAN_CFLAGS_big = -DTARGET_BIG_ENDIAN
MIPS_TARGET_ENDIAN_CFLAGS_little = -DTARGET_LITTLE_ENDIAN

CC            = $(MIPS_PREFIX)gcc $(MIPS_ARCH) $(MIPS_CODE)
LD            = $(MIPS_PREFIX)ld -m elf32ebmip
AR            = $(MIPS_PREFIX)ar
RANLIB        = $(MIPS_PREFIX)ranlib
SIZE          = $(MIPS_PREFIX)size
NM            = $(MIPS_PREFIX)nm
OBJDUMP       = $(MIPS_PREFIX)objdump
OBJCOPY       = $(MIPS_PREFIX)objcopy
READELF       = $(MIPS_PREFIX)readelf
AS            = $(CC) -x assembler-with-cpp -c
YACC          = byacc
LEX           = flex
INSTALL       = install -m 644
INSTALLDIR    = install -m 755 -d
TAGSFILE      = tags
MANROFF       = nroff -Tascii -man -h
DOCROFF       = nroff -Tascii -mdoc -h
CFLAGS        = -Os -nostdinc \
                $(MIPS_TARGET_ENDIAN_CFLAGS_$(MIPS_TARGET_ENDIAN)) \
                $(MIPS_TARGET_CPU_CFLAGS_$(MIPS_TARGET_CPU)) \
                -DTARGET_NO_ABICALLS
LIBS          = -lc
