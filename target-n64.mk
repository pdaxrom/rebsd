MACHINE     = mips
DESTDIR     ?= $(TOPSRC)
RELEASE     = 0.0
BUILD       = $(shell git rev-list HEAD --count)
VERSION     = $(RELEASE)-$(BUILD)

N64_TOOLCHAIN ?= /Users/sash/Library/n64-toolchain-opengl
N64_PREFIX    = $(N64_TOOLCHAIN)/bin/mips64-elf-

N64_ARCH      = -EB -march=vr4300 -mtune=vr4300 -mips3 -mabi=32 -mhard-float
N64_CODE      = -G0 -mno-abicalls -fno-pic -fomit-frame-pointer \
                -finline-hint-functions
N64_INCLUDES  = -I$(TOPSRC)/sys/n64/include -I$(TOPSRC)/include

CC            = $(N64_PREFIX)gcc $(N64_ARCH) $(N64_CODE) $(N64_INCLUDES) -Werror
CXX           = $(N64_PREFIX)g++ $(N64_ARCH) $(N64_CODE) $(N64_INCLUDES) -Werror
LD            = $(N64_PREFIX)ld -m elf32ebmip
AR            = $(N64_PREFIX)ar
RANLIB        = $(N64_PREFIX)ranlib
SIZE          = $(N64_PREFIX)size
NM            = $(N64_PREFIX)nm
OBJDUMP       = $(N64_PREFIX)objdump
OBJCOPY       = $(N64_PREFIX)objcopy
READELF       = $(N64_PREFIX)readelf
AS            = $(CC) -x assembler-with-cpp -c
YACC          = byacc
LEX           = flex
INSTALL       = install -m 644
INSTALLDIR    = install -m 755 -d
TAGSFILE      = tags
MANROFF       = nroff -man -h
DOCROFF       = nroff -mdoc -h
ELF2AOUT      = $(TOPSRC)/tools/elf2aout/elf2aout

CFLAGS        = -Os -nostdinc

LDFLAGS       = --nmagic -T$(TOPSRC)/src/elf32-mips.ld $(TOPSRC)/src/crt0.o -L$(TOPSRC)/src
LIBS          = -lc
