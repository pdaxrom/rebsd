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

DEPFLAGS        = -MT $@ -MP -MD -MF .deps/$*.dep
DEFS            = -I. -I$(TOPSRC)/include -DKERNEL -DN64 $(DEPFLAGS)
CFLAGS          = -O2 -Wall -Werror -nostdinc $(DEFS)
ASFLAGS         = -x assembler-with-cpp -c $(DEFS)
FPU_ASFLAGS     = -x assembler-with-cpp -c $(DEFS)

OBJS            = startup.o n64cart_uart.o cons.o devsw.o clock.o fpu.o

all: preflight

preflight: $(OBJS)
	$(SIZE) $(OBJS)

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
	rm -rf *.o *.elf *.bin *.dis *.nm machine sys .deps

.PHONY: all preflight clean
