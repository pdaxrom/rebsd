#
# Early N64 machine-layer build. This intentionally stops before linking the
# whole kernel while the port is being brought up.
#
TOPSRC          = $(shell cd ../..; pwd)
include $(TOPSRC)/target-n64.mk

N64_KERNEL_ARCH = -EB -march=vr4300 -mtune=vr4300 -mips3 -mabi=32
N64_KERNEL_BASE = -G0 -mno-abicalls -fno-pic -ffreestanding -fno-builtin \
                  -fomit-frame-pointer
N64_KERNEL_CODE = $(N64_KERNEL_BASE) -msoft-float
N64_FPU_CODE    = $(N64_KERNEL_BASE) -mhard-float

CC              = $(N64_PREFIX)gcc $(N64_KERNEL_ARCH) $(N64_KERNEL_CODE)
AS              = $(N64_PREFIX)gcc $(N64_KERNEL_ARCH) $(N64_KERNEL_CODE)
ASFPU           = $(N64_PREFIX)gcc $(N64_KERNEL_ARCH) $(N64_FPU_CODE)
LD              = $(N64_PREFIX)ld -m elf32ebmip
OBJDUMP         = $(N64_PREFIX)objdump
SIZE            = $(N64_PREFIX)size
STRIP           = $(N64_PREFIX)strip
N64_TOOL        = $(N64_TOOLCHAIN)/bin/n64tool

DEPFLAGS        = -MT $@ -MP -MD -MF .deps/$*.dep
DEFS            = -I. -I$(TOPSRC)/include -DKERNEL -DN64 $(DEPFLAGS)
CFLAGS          = -O2 -Wall -Werror -nostdinc $(DEFS)
ASFLAGS         = -x assembler-with-cpp -c $(DEFS)
FPU_ASFLAGS     = -x assembler-with-cpp -c $(DEFS)
STAGE0_CFLAGS   = $(N64_KERNEL_ARCH) $(N64_KERNEL_BASE) -msoft-float \
                  -ffreestanding -fno-builtin -fno-stack-protector \
                  -fno-pie -Wall -Wextra -Werror -O2 -MMD -MP
STAGE0_ASFLAGS  = $(N64_KERNEL_ARCH) $(N64_KERNEL_BASE) -msoft-float \
                  -x assembler-with-cpp -MMD -MP -c

OBJS            = startup.o n64cart_uart.o cons.o devsw.o clock.o machdep.o fpu.o
PREFLIGHT_OBJS  = startup.o n64cart_uart.o preflight.o
STAGE0_OBJS     = stage0.o stage0_loader.o stage0_kernel_blob.o

all: preflight preflight.elf preflight.z64

preflight: $(OBJS)
	$(SIZE) $(OBJS)

preflight.elf: $(PREFLIGHT_OBJS) n64.ld
	$(LD) -nostdlib -T n64.ld $(PREFLIGHT_OBJS) -o $@
	$(SIZE) $@
	$(OBJDUMP) -d -S $@ > preflight.dis

stage0.o: stage0.S | .deps
	$(N64_PREFIX)gcc $(STAGE0_ASFLAGS) $< -o $@

stage0_loader.o: stage0_loader.c | .deps
	$(N64_PREFIX)gcc $(STAGE0_CFLAGS) -c $< -o $@

stage0_kernel_blob.o: stage0_kernel_blob.S preflight.elf | .deps
	$(N64_PREFIX)gcc $(STAGE0_ASFLAGS) -DN64_KERNEL_ELF=\"$(abspath preflight.elf)\" $< -o $@

stage0.elf: $(STAGE0_OBJS) stage0_linker.ld
	$(LD) -T stage0_linker.ld -nostdlib -o $@ $(STAGE0_OBJS)

stage0.stripped.elf: stage0.elf
	$(STRIP) -s -o $@ $<

preflight.z64: stage0.stripped.elf
	$(N64_TOOL) --toc --title "RETROBSD N64" --output $@ --align 256 stage0.stripped.elf

startup.o: startup.S | machine sys .deps
	$(AS) $(ASFLAGS) $< -o $@

fpu.o: fpu.S fpu.h io.h | machine sys .deps
	$(ASFPU) $(FPU_ASFLAGS) $< -o $@

%.o: %.c | machine sys .deps
	$(CC) $(CFLAGS) -c $< -o $@

machine:
	ln -s . $@

sys:
	ln -s ../include $@

.deps:
	mkdir .deps

clean:
	rm -rf *.o *.elf *.bin *.dis *.nm *.z64 machine sys .deps

.PHONY: all preflight clean
