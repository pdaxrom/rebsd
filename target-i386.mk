# Common GCC/binutils policy for the ReBSD i686 port.

MACHINE         = i386
I686_TOOLCHAIN ?= /Users/sash/Library/i686-toolchain
I686_PREFIX    ?= $(I686_TOOLCHAIN)/bin/i686-elf-

CC              = $(I686_PREFIX)gcc
LD              = $(I686_PREFIX)ld
AR              = $(I686_PREFIX)ar
RANLIB          = $(I686_PREFIX)ranlib
SIZE            = $(I686_PREFIX)size
NM              = $(I686_PREFIX)nm
OBJCOPY         = $(I686_PREFIX)objcopy
OBJDUMP         = $(I686_PREFIX)objdump
READELF         = $(I686_PREFIX)readelf
AS              = $(CC) -x assembler-with-cpp -c
YACC            = byacc
LEX             = flex
INSTALL         = install -m 644
INSTALLDIR      = install -m 755 -d
TAGSFILE        = tags
MANROFF         = nroff -Tascii -man -h
DOCROFF         = nroff -Tascii -mdoc -h

I686_ARCH_FLAGS = -m32 -march=i686 -mtune=generic
I686_CODE_FLAGS = -ffreestanding -fno-builtin -fno-stack-protector \
                  -fno-pic -fno-pie -fno-omit-frame-pointer \
                  -ffunction-sections -fdata-sections \
                  -fno-asynchronous-unwind-tables -fno-unwind-tables \
                  -mno-sse -mno-sse2 -Wa,--noexecstack
I686_FORMAT_WARN_FLAGS = -Wformat=2 -Werror=format
I686_WARN_FLAGS = -Wall -Wextra -Werror $(I686_FORMAT_WARN_FLAGS)

I686_CPPFLAGS   = -DKERNEL -DI386 -D__i386__ \
                  -DVM_PHYS_MAX_REGIONS=512
I686_CFLAGS     = $(I686_ARCH_FLAGS) $(I686_CODE_FLAGS) $(I686_WARN_FLAGS) \
                  -std=gnu11 -Os
I686_ASFLAGS    = $(I686_ARCH_FLAGS) $(I686_CODE_FLAGS) \
                  -x assembler-with-cpp
I686_LDFLAGS    = -m elf_i386 -z noexecstack

# Userland policy.  The rootfs build adds its staged include paths to CC and
# supplies the final linker script/crt0 through LDFLAGS.
CFLAGS          = -m32 -march=i686 -mtune=generic -ffreestanding \
                  -fno-builtin -fno-stack-protector -fno-pic -fno-pie \
                  -fno-asynchronous-unwind-tables -fno-unwind-tables \
                  -mno-sse -mno-sse2 -mlong-double-64 \
                  $(I686_FORMAT_WARN_FLAGS) -Os
LIBS            = -lc
ELF2AOUT        = cp
