ifeq ($(REBSD_OBJ_BUILD),)
REBSD_TOPSRC_REL = ../..
include ../../mk/obj-dispatch.mk
else

TOPSRC          ?= ../..
BOARD           ?= pc
I386_SRC         = $(TOPSRC)/sys/i386
I386_BOOT_SRC    = $(I386_SRC)/boot
I386_COMMON_SRC  = $(I386_SRC)/common
I386_PC_SRC      = $(I386_SRC)/pc
I386_TOOLS_SRC   = $(I386_SRC)/tools
I386_VM_SRC      = $(TOPSRC)/sys/vm
I386_GEN_INCLUDE = $(CURDIR)/include
I386_VM_INCLUDES = -I$(I386_GEN_INCLUDE) -I$(TOPSRC)/sys \
                   -I$(I386_SRC)/include

ifeq ($(BOARD),pc)
else
$(error Unsupported i386 BOARD=$(BOARD); expected pc)
endif

include $(TOPSRC)/target-i386.mk

QEMU_I386       ?= qemu-system-i386
QEMU_MACHINE    ?= pc-i440fx-9.2
QEMU_CPU        ?= pentium3
QEMU_MEMORY     ?= 64M
PYTHON          ?= python3

SETUP_SECTS      = 8
SETUP_BYTES      = 4608

VM_CORE_OBJS     = vm_map.o vm_object.o vm_shm.o vm_sysv_shm.o \
                   vmspace.o vmspace_access.o vmspace_fault.o \
                   vmspace_fault_api.o vmspace_fault_validate.o
KERNEL_OBJS      = entry.o interrupt_entry.o context.o privilege.o \
                   early_console.o interrupt.o tss.o privilege_selftest.o \
                   syscall_selftest.o signal.o signal_selftest.o \
                   user_return.o user_return_selftest.o \
                   trap.o trap_selftest.o \
                   pic.o pit.o memory.o paging.o \
                   libkern.o sysent_bootstrap.o kern_prot.o copyio.o \
                   copyio_selftest.o vm_machdep.o uarea_selftest.o \
                   context_selftest.o process_bootstrap.o vm_param.o \
                   vm_phys.o vm_page.o vm_bootstrap.o pmap.o \
                   $(VM_CORE_OBJS) vmspace_bootstrap.o boot_main.o

.DEFAULT_GOAL := all

.PHONY: all kernel toolchain-check run run-serial debug boot-smoke \
	trap-smoke exception-smoke boot-smoke-matrix clean

all: toolchain-check kernel

kernel: rebsd-i686.bzimg

toolchain-check:
	$(PYTHON) $(I386_TOOLS_SRC)/toolchain-check.py \
	    --prefix $(I686_PREFIX) \
	    --work-dir $(TOPOBJ)/tmp/i686-toolchain-check

setup.o: $(I386_BOOT_SRC)/setup.S
	$(CC) $(I686_ASFLAGS) -DSETUP_SECTS=$(SETUP_SECTS) -c $< -o $@

setup.elf: setup.o $(I386_BOOT_SRC)/setup.ld
	$(LD) $(I686_LDFLAGS) -T $(I386_BOOT_SRC)/setup.ld \
	    -Map setup.map -o $@ setup.o

setup.bin: setup.elf
	$(OBJCOPY) -O binary $< $@
	@test "$$(wc -c < $@ | tr -d ' ')" = "$(SETUP_BYTES)" || { \
	    echo "setup.bin has unexpected size" >&2; exit 1; \
	}

entry.o: $(I386_BOOT_SRC)/entry.S
	$(CC) $(I686_ASFLAGS) $(I686_CPPFLAGS) -c $< -o $@

interrupt_entry.o: $(I386_BOOT_SRC)/interrupt_entry.S
	$(CC) $(I686_ASFLAGS) $(I686_CPPFLAGS) -c $< -o $@

context.o: $(I386_COMMON_SRC)/context.S $(I386_SRC)/include/context.h \
	    $(I386_SRC)/include/tss.h
	$(CC) $(I686_ASFLAGS) $(I686_CPPFLAGS) -I$(I386_SRC)/include \
	    -c $< -o $@

privilege.o: $(I386_COMMON_SRC)/privilege.S $(I386_SRC)/include/context.h
	$(CC) $(I686_ASFLAGS) $(I686_CPPFLAGS) -I$(I386_SRC)/include \
	    -c $< -o $@

early_console.o: $(I386_PC_SRC)/early_console.c \
	    $(I386_SRC)/include/boot.h $(I386_SRC)/include/io.h
	$(CC) $(I686_CFLAGS) $(I686_CPPFLAGS) -I$(I386_SRC)/include \
	    -c $< -o $@

interrupt.o: $(I386_PC_SRC)/interrupt.c \
	    $(I386_SRC)/include/boot.h $(I386_SRC)/include/interrupt.h \
	    $(I386_SRC)/include/syscall.h \
	    $(I386_SRC)/include/trap.h \
	    $(I386_SRC)/include/user_return.h \
	    $(I386_SRC)/include/vmspace_bootstrap.h
	$(CC) $(I686_CFLAGS) $(I686_CPPFLAGS) -I$(I386_SRC)/include \
	    -c $< -o $@

tss.o: $(I386_PC_SRC)/tss.c $(I386_SRC)/include/boot.h \
	    $(I386_SRC)/include/interrupt.h $(I386_SRC)/include/tss.h
	$(CC) $(I686_CFLAGS) $(I686_CPPFLAGS) -I$(I386_SRC)/include \
	    -c $< -o $@

privilege_selftest.o: $(I386_PC_SRC)/privilege.c \
	    $(I386_SRC)/include/context.h $(I386_SRC)/include/interrupt.h \
	    $(I386_SRC)/include/privilege.h $(I386_SRC)/include/tss.h \
	    $(I386_SRC)/include/vmspace_bootstrap.h \
	    $(TOPSRC)/sys/include/user.h $(I386_VM_SRC)/vmspace.h \
	    | $(I386_GEN_INCLUDE)/sys $(I386_GEN_INCLUDE)/machine
	$(CC) $(I686_CFLAGS) $(I686_CPPFLAGS) $(I386_VM_INCLUDES) \
	    -c $< -o $@

syscall_selftest.o: $(I386_PC_SRC)/syscall.c \
	    $(I386_SRC)/include/interrupt.h $(I386_SRC)/include/privilege.h \
	    $(I386_SRC)/include/syscall.h $(I386_SRC)/include/tss.h \
	    $(I386_SRC)/include/vmspace_bootstrap.h \
	    $(TOPSRC)/sys/include/systm.h $(TOPSRC)/sys/include/user.h \
	    $(I386_VM_SRC)/vmspace.h \
	    | $(I386_GEN_INCLUDE)/sys $(I386_GEN_INCLUDE)/machine
	$(CC) $(I686_CFLAGS) $(I686_CPPFLAGS) $(I386_VM_INCLUDES) \
	    -c $< -o $@

signal.o: $(I386_COMMON_SRC)/signal.c \
	    $(I386_SRC)/include/context.h \
	    $(I386_SRC)/include/interrupt.h \
	    $(I386_SRC)/include/signal_machdep.h \
	    $(TOPSRC)/sys/include/signal.h \
	    $(TOPSRC)/sys/include/signalvar.h $(TOPSRC)/sys/include/user.h \
	    $(I386_VM_SRC)/vmspace.h \
	    | $(I386_GEN_INCLUDE)/sys $(I386_GEN_INCLUDE)/machine
	$(CC) $(I686_CFLAGS) $(I686_CPPFLAGS) $(I386_VM_INCLUDES) \
	    -c $< -o $@

signal_selftest.o: $(I386_PC_SRC)/signal_selftest.c \
	    $(I386_SRC)/include/context.h \
	    $(I386_SRC)/include/interrupt.h \
	    $(I386_SRC)/include/signal_machdep.h \
	    $(I386_SRC)/include/vmspace_bootstrap.h \
	    $(TOPSRC)/sys/include/signal.h \
	    $(TOPSRC)/sys/include/signalvar.h $(TOPSRC)/sys/include/user.h \
	    $(I386_VM_SRC)/vmspace.h \
	    | $(I386_GEN_INCLUDE)/sys $(I386_GEN_INCLUDE)/machine
	$(CC) $(I686_CFLAGS) $(I686_CPPFLAGS) $(I386_VM_INCLUDES) \
	    -c $< -o $@

user_return.o: $(I386_COMMON_SRC)/user_return.c \
	    $(I386_SRC)/include/interrupt.h \
	    $(I386_SRC)/include/user_return.h \
	    $(TOPSRC)/sys/include/signalvar.h \
	    $(TOPSRC)/sys/include/systm.h $(TOPSRC)/sys/include/user.h \
	    | $(I386_GEN_INCLUDE)/sys $(I386_GEN_INCLUDE)/machine
	$(CC) $(I686_CFLAGS) $(I686_CPPFLAGS) $(I386_VM_INCLUDES) \
	    -c $< -o $@

user_return_selftest.o: $(I386_PC_SRC)/user_return_selftest.c \
	    $(I386_SRC)/include/interrupt.h \
	    $(I386_SRC)/include/privilege.h \
	    $(I386_SRC)/include/signal_machdep.h \
	    $(I386_SRC)/include/syscall.h $(I386_SRC)/include/tss.h \
	    $(I386_SRC)/include/user_return.h \
	    $(I386_SRC)/include/vmspace_bootstrap.h \
	    $(TOPSRC)/sys/include/signal.h \
	    $(TOPSRC)/sys/include/signalvar.h $(TOPSRC)/sys/include/user.h \
	    $(I386_VM_SRC)/vmspace.h \
	    | $(I386_GEN_INCLUDE)/sys $(I386_GEN_INCLUDE)/machine
	$(CC) $(I686_CFLAGS) $(I686_CPPFLAGS) $(I386_VM_INCLUDES) \
	    -c $< -o $@

trap.o: $(I386_COMMON_SRC)/trap.c \
	    $(I386_SRC)/include/interrupt.h $(I386_SRC)/include/trap.h \
	    $(TOPSRC)/sys/include/signal.h $(TOPSRC)/sys/include/user.h \
	    | $(I386_GEN_INCLUDE)/sys $(I386_GEN_INCLUDE)/machine
	$(CC) $(I686_CFLAGS) $(I686_CPPFLAGS) $(I386_VM_INCLUDES) \
	    -c $< -o $@

trap_selftest.o: $(I386_PC_SRC)/trap_selftest.c \
	    $(I386_SRC)/include/interrupt.h \
	    $(I386_SRC)/include/privilege.h \
	    $(I386_SRC)/include/signal_machdep.h \
	    $(I386_SRC)/include/syscall.h $(I386_SRC)/include/trap.h \
	    $(I386_SRC)/include/tss.h $(I386_SRC)/include/user_return.h \
	    $(I386_SRC)/include/vmspace_bootstrap.h \
	    $(TOPSRC)/sys/include/signal.h \
	    $(TOPSRC)/sys/include/signalvar.h $(TOPSRC)/sys/include/user.h \
	    $(I386_VM_SRC)/vmspace.h \
	    | $(I386_GEN_INCLUDE)/sys $(I386_GEN_INCLUDE)/machine
	$(CC) $(I686_CFLAGS) $(I686_CPPFLAGS) $(I386_VM_INCLUDES) \
	    -c $< -o $@

pic.o: $(I386_PC_SRC)/pic.c $(I386_SRC)/include/interrupt.h \
	    $(I386_SRC)/include/io.h
	$(CC) $(I686_CFLAGS) $(I686_CPPFLAGS) -I$(I386_SRC)/include \
	    -c $< -o $@

pit.o: $(I386_PC_SRC)/pit.c $(I386_SRC)/include/interrupt.h \
	    $(I386_SRC)/include/io.h
	$(CC) $(I686_CFLAGS) $(I686_CPPFLAGS) -I$(I386_SRC)/include \
	    -c $< -o $@

memory.o: $(I386_PC_SRC)/memory.c $(I386_SRC)/include/boot.h \
	    $(I386_SRC)/include/memory.h
	$(CC) $(I686_CFLAGS) $(I686_CPPFLAGS) -I$(I386_SRC)/include \
	    -c $< -o $@

paging.o: $(I386_PC_SRC)/paging.c $(I386_SRC)/include/memory.h \
	    $(I386_SRC)/include/paging.h
	$(CC) $(I686_CFLAGS) $(I686_CPPFLAGS) -I$(I386_SRC)/include \
	    -c $< -o $@

libkern.o: $(I386_COMMON_SRC)/libkern.c \
	    | $(I386_GEN_INCLUDE)/sys $(I386_GEN_INCLUDE)/machine
	$(CC) $(I686_CFLAGS) $(I686_CPPFLAGS) $(I386_VM_INCLUDES) \
	    -c $< -o $@

sysent_bootstrap.o: $(I386_COMMON_SRC)/sysent_bootstrap.c \
	    $(TOPSRC)/sys/include/systm.h $(TOPSRC)/sys/include/user.h \
	    | $(I386_GEN_INCLUDE)/sys $(I386_GEN_INCLUDE)/machine
	$(CC) $(I686_CFLAGS) $(I686_CPPFLAGS) $(I386_VM_INCLUDES) \
	    -c $< -o $@

kern_prot.o: $(TOPSRC)/sys/kernel/kern_prot.c \
	    $(TOPSRC)/sys/include/proc.h $(TOPSRC)/sys/include/systm.h \
	    $(TOPSRC)/sys/include/user.h \
	    | $(I386_GEN_INCLUDE)/sys $(I386_GEN_INCLUDE)/machine
	$(CC) $(I686_CFLAGS) $(I686_CPPFLAGS) $(I386_VM_INCLUDES) \
	    -c $< -o $@

copyio.o: $(I386_COMMON_SRC)/copyio.c \
	    $(I386_VM_SRC)/vmspace.h \
	    | $(I386_GEN_INCLUDE)/sys $(I386_GEN_INCLUDE)/machine
	$(CC) $(I686_CFLAGS) $(I686_CPPFLAGS) $(I386_VM_INCLUDES) \
	    -c $< -o $@

copyio_selftest.o: $(I386_PC_SRC)/copyio_selftest.c \
	    $(I386_SRC)/include/copyio.h \
	    $(I386_SRC)/include/vmspace_bootstrap.h \
	    $(I386_VM_SRC)/vmspace.h \
	    | $(I386_GEN_INCLUDE)/sys $(I386_GEN_INCLUDE)/machine
	$(CC) $(I686_CFLAGS) $(I686_CPPFLAGS) $(I386_VM_INCLUDES) \
	    -c $< -o $@

vm_machdep.o: $(I386_COMMON_SRC)/vm_machdep.c \
	    $(I386_SRC)/include/context.h $(I386_SRC)/include/interrupt.h \
	    $(I386_SRC)/include/memory.h \
	    $(I386_SRC)/include/machine/elf_machdep.h \
	    $(I386_SRC)/include/machine/layout.h \
	    $(I386_GEN_INCLUDE)/sys/user.h $(I386_VM_SRC)/pmap.h \
	    $(I386_VM_SRC)/vmspace.h \
	    | $(I386_GEN_INCLUDE)/sys $(I386_GEN_INCLUDE)/machine
	$(CC) $(I686_CFLAGS) $(I686_CPPFLAGS) $(I386_VM_INCLUDES) \
	    -c $< -o $@

uarea_selftest.o: $(I386_PC_SRC)/uarea_selftest.c \
	    $(I386_SRC)/include/context.h \
	    $(I386_SRC)/include/interrupt.h \
	    $(I386_SRC)/include/process.h \
	    $(I386_SRC)/include/machine/elf_machdep.h \
	    $(I386_SRC)/include/machine/layout.h \
	    $(I386_GEN_INCLUDE)/sys/user.h $(I386_VM_SRC)/vmspace.h \
	    | $(I386_GEN_INCLUDE)/sys $(I386_GEN_INCLUDE)/machine
	$(CC) $(I686_CFLAGS) $(I686_CPPFLAGS) $(I386_VM_INCLUDES) \
	    -c $< -o $@

context_selftest.o: $(I386_PC_SRC)/context_selftest.c \
	    $(I386_SRC)/include/context.h \
	    $(I386_SRC)/include/interrupt.h \
	    $(I386_SRC)/include/process.h \
	    $(I386_SRC)/include/tss.h \
	    $(I386_SRC)/include/vmspace_bootstrap.h \
	    $(I386_SRC)/include/machine/elf_machdep.h \
	    $(I386_SRC)/include/machine/layout.h \
	    $(I386_GEN_INCLUDE)/sys/proc.h $(I386_GEN_INCLUDE)/sys/user.h \
	    $(I386_VM_SRC)/vmspace.h \
	    | $(I386_GEN_INCLUDE)/sys $(I386_GEN_INCLUDE)/machine
	$(CC) $(I686_CFLAGS) $(I686_CPPFLAGS) $(I386_VM_INCLUDES) \
	    -c $< -o $@

process_bootstrap.o: $(I386_PC_SRC)/process_bootstrap.c \
	    $(I386_SRC)/include/process.h $(I386_SRC)/include/tss.h \
	    $(I386_SRC)/include/vmspace_bootstrap.h \
	    $(TOPSRC)/sys/include/proc.h $(TOPSRC)/sys/include/user.h \
	    $(I386_VM_SRC)/vmspace.h \
	    | $(I386_GEN_INCLUDE)/sys $(I386_GEN_INCLUDE)/machine
	$(CC) $(I686_CFLAGS) $(I686_CPPFLAGS) $(I386_VM_INCLUDES) \
	    -c $< -o $@

$(I386_GEN_INCLUDE)/sys:
	mkdir -p $(I386_GEN_INCLUDE)
	ln -s $(abspath $(TOPSRC)/sys/include) $@

$(I386_GEN_INCLUDE)/machine:
	mkdir -p $(I386_GEN_INCLUDE)
	ln -s $(abspath $(I386_SRC)/include/machine) $@

vm_param.o: $(I386_VM_SRC)/vm_param.c \
	    $(I386_VM_SRC)/vm_param.h $(I386_VM_SRC)/vm_types.h \
	    | $(I386_GEN_INCLUDE)/sys $(I386_GEN_INCLUDE)/machine
	$(CC) $(I686_CFLAGS) $(I686_CPPFLAGS) $(I386_VM_INCLUDES) \
	    -c $< -o $@

vm_phys.o: $(I386_VM_SRC)/vm_phys.c \
	    $(I386_VM_SRC)/vm_phys.h $(I386_VM_SRC)/vm_page.h \
	    | $(I386_GEN_INCLUDE)/sys $(I386_GEN_INCLUDE)/machine
	$(CC) $(I686_CFLAGS) $(I686_CPPFLAGS) $(I386_VM_INCLUDES) \
	    -c $< -o $@

vm_page.o: $(I386_VM_SRC)/vm_page.c \
	    $(I386_VM_SRC)/vm_page.h $(I386_VM_SRC)/vm_phys.h \
	    | $(I386_GEN_INCLUDE)/sys $(I386_GEN_INCLUDE)/machine
	$(CC) $(I686_CFLAGS) $(I686_CPPFLAGS) $(I386_VM_INCLUDES) \
	    -c $< -o $@

vm_bootstrap.o: $(I386_PC_SRC)/vm_bootstrap.c \
	    $(I386_SRC)/include/memory.h \
	    $(I386_SRC)/include/vm_bootstrap.h \
	    | $(I386_GEN_INCLUDE)/sys $(I386_GEN_INCLUDE)/machine
	$(CC) $(I686_CFLAGS) $(I686_CPPFLAGS) $(I386_VM_INCLUDES) \
	    -c $< -o $@

pmap.o: $(I386_COMMON_SRC)/pmap.c \
	    $(I386_SRC)/include/memory.h $(I386_SRC)/include/paging.h \
	    $(I386_VM_SRC)/pmap.h $(I386_VM_SRC)/vm_page.h \
	    | $(I386_GEN_INCLUDE)/sys $(I386_GEN_INCLUDE)/machine
	$(CC) $(I686_CFLAGS) $(I686_CPPFLAGS) $(I386_VM_INCLUDES) \
	    -c $< -o $@

$(VM_CORE_OBJS): %.o: $(I386_VM_SRC)/%.c \
	    | $(I386_GEN_INCLUDE)/sys $(I386_GEN_INCLUDE)/machine
	$(CC) $(I686_CFLAGS) $(I686_CPPFLAGS) $(I386_VM_INCLUDES) \
	    -c $< -o $@

vmspace_bootstrap.o: $(I386_PC_SRC)/vmspace_bootstrap.c \
	    $(I386_SRC)/include/vmspace_bootstrap.h \
	    $(I386_VM_SRC)/vmspace.h $(I386_VM_SRC)/vm_object.h \
	    | $(I386_GEN_INCLUDE)/sys $(I386_GEN_INCLUDE)/machine
	$(CC) $(I686_CFLAGS) $(I686_CPPFLAGS) $(I386_VM_INCLUDES) \
	    -c $< -o $@

boot_main.o: $(I386_PC_SRC)/boot_main.c \
	    $(I386_SRC)/include/boot.h $(I386_SRC)/include/copyio.h \
	    $(I386_SRC)/include/interrupt.h \
	    $(I386_SRC)/include/memory.h $(I386_SRC)/include/paging.h \
	    $(I386_SRC)/include/pmap_bootstrap.h $(I386_SRC)/include/privilege.h \
	    $(I386_SRC)/include/process.h \
	    $(I386_SRC)/include/signal_machdep.h \
	    $(I386_SRC)/include/syscall.h \
	    $(I386_SRC)/include/tss.h \
	    $(I386_SRC)/include/trap.h \
	    $(I386_SRC)/include/user_return.h \
	    $(I386_SRC)/include/vm_bootstrap.h \
	    $(I386_SRC)/include/vmspace_bootstrap.h \
	    | $(I386_GEN_INCLUDE)/sys $(I386_GEN_INCLUDE)/machine
	$(CC) $(I686_CFLAGS) $(I686_CPPFLAGS) $(I386_VM_INCLUDES) \
	    -c $< -o $@

rebsd-i686.elf: $(KERNEL_OBJS) $(I386_PC_SRC)/kernel.ld
	$(LD) $(I686_LDFLAGS) --gc-sections -T $(I386_PC_SRC)/kernel.ld \
	    -Map rebsd-i686.map -o $@ $(KERNEL_OBJS)
	$(SIZE) $@

rebsd-i686.bin: rebsd-i686.elf
	$(OBJCOPY) -O binary $< $@

rebsd-i686.bzimg: setup.bin rebsd-i686.bin \
	    $(I386_TOOLS_SRC)/mkboot.py
	$(PYTHON) $(I386_TOOLS_SRC)/mkboot.py \
	    --setup setup.bin --kernel rebsd-i686.bin --output $@

run run-serial: rebsd-i686.bzimg
	$(QEMU_I386) -machine $(QEMU_MACHINE) -cpu $(QEMU_CPU) \
	    -accel tcg -m $(QEMU_MEMORY) -kernel $< \
	    -serial stdio -display none -monitor none \
	    -no-reboot -no-shutdown

debug: rebsd-i686.bzimg
	$(QEMU_I386) -machine $(QEMU_MACHINE) -cpu $(QEMU_CPU) \
	    -accel tcg -m $(QEMU_MEMORY) -kernel $< \
	    -serial stdio -display none -monitor none \
	    -no-reboot -no-shutdown -S -s

boot-smoke: rebsd-i686.bzimg
	$(PYTHON) $(I386_TOOLS_SRC)/qemu-boot-smoke.py \
	    --qemu $(QEMU_I386) --machine $(QEMU_MACHINE) \
	    --cpu $(QEMU_CPU) --memory $(QEMU_MEMORY) --kernel $<

trap-smoke: rebsd-i686.bzimg
	$(PYTHON) $(I386_TOOLS_SRC)/qemu-boot-smoke.py \
	    --qemu $(QEMU_I386) --machine $(QEMU_MACHINE) \
	    --cpu $(QEMU_CPU) --memory $(QEMU_MEMORY) --kernel $< \
	    --expect-exception divide
	$(PYTHON) $(I386_TOOLS_SRC)/qemu-boot-smoke.py \
	    --qemu $(QEMU_I386) --machine $(QEMU_MACHINE) \
	    --cpu $(QEMU_CPU) --memory $(QEMU_MEMORY) --kernel $< \
	    --expect-exception gp
	$(PYTHON) $(I386_TOOLS_SRC)/qemu-boot-smoke.py \
	    --qemu $(QEMU_I386) --machine $(QEMU_MACHINE) \
	    --cpu $(QEMU_CPU) --memory $(QEMU_MEMORY) --kernel $< \
	    --expect-exception page

exception-smoke: trap-smoke

boot-smoke-matrix: rebsd-i686.bzimg
	@for memory in 32M 64M 128M 256M 768M 1024M; do \
	    echo "boot-smoke-matrix: memory=$$memory"; \
	    $(PYTHON) $(I386_TOOLS_SRC)/qemu-boot-smoke.py \
	        --qemu $(QEMU_I386) --machine $(QEMU_MACHINE) \
	        --cpu $(QEMU_CPU) --memory $$memory --kernel $< || exit 1; \
	done

clean:
	rm -f setup.o setup.elf setup.bin setup.map \
	    $(KERNEL_OBJS) rebsd-i686.elf rebsd-i686.bin \
	    rebsd-i686.bzimg rebsd-i686.map
	rm -f $(I386_GEN_INCLUDE)/sys $(I386_GEN_INCLUDE)/machine
	rmdir $(I386_GEN_INCLUDE) 2>/dev/null || true

endif
