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

I686_ARCH_FLAGS = -m32 -march=i686 -mtune=generic
I686_CODE_FLAGS = -ffreestanding -fno-builtin -fno-stack-protector \
                  -fno-pic -fno-pie -fno-omit-frame-pointer \
                  -fno-asynchronous-unwind-tables -fno-unwind-tables \
                  -mno-sse -mno-sse2 -Wa,--noexecstack
I686_WARN_FLAGS = -Wall -Wextra -Werror

I686_CPPFLAGS   = -DKERNEL -DI386 -D__i386__
I686_CFLAGS     = $(I686_ARCH_FLAGS) $(I686_CODE_FLAGS) $(I686_WARN_FLAGS) \
                  -std=gnu11 -Os
I686_ASFLAGS    = $(I686_ARCH_FLAGS) $(I686_CODE_FLAGS) \
                  -x assembler-with-cpp
I686_LDFLAGS    = -m elf_i386 -z noexecstack
