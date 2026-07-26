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
I386_USER_SRC    = $(I386_SRC)/user
I386_TOOLS_SRC   = $(I386_SRC)/tools
I386_DISK_SRC    = $(TOPSRC)/sys/disk
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
HOSTCC          ?= cc
FSUTIL_DIR       = $(TOPOBJ)/tools/fsutil
FSUTIL           = $(FSUTIL_DIR)/fsutil
I386_ROOTFS_STAGE = rootfs-stage
I386_ROOTFS_KBYTES = 128
I386_ROOTFS_EPOCH = 1
I386_VERSION_GENERATOR = $(TOPSRC)/tools/build/gen-vers.py
I386_COMPILER_VERSION = $(shell $(CC) --version 2>/dev/null | sed -n '1p')
I386_GIT_REV = $(or $(shell git -C $(TOPSRC) rev-parse HEAD 2>/dev/null),unknown)
I386_GIT_BRANCH = $(or $(shell git -C $(TOPSRC) symbolic-ref --short -q HEAD 2>/dev/null),detached)
I386_GIT_DIRTY = $(shell test -n "$$(git -C $(TOPSRC) status --porcelain --untracked-files=normal 2>/dev/null)" && echo 1 || echo 0)

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
                   pic.o pit.o pci.o ide.o memory.o paging.o \
                   libkern.o disk_subr.o disk.o disk_memory.o devsw.o \
                   ufs_alloc.o ufs_bio.o ufs_bmap.o ufs_fio.o \
                   ufs_inode.o ufs_mount.o ufs_namei.o ufs_subr.o \
                   ufs_syscalls.o ufs_syscalls2.o \
                   sys_inode.o sys_generic.o sys_pipe.o vfs_vnops.o \
                   kern_descrip.o kern_shm.o kern_subr.o kern_time.o \
                   vm_vnode.o vfs_bootstrap.o \
                   disk_bootstrap.o \
                   exec_aout.o exec_conf.o exec_elf.o exec_elf_loader.o \
                   exec_script.o exec_subr.o kern_exec.o init_process.o \
                   rootfs_image.o \
                   init_sysent.o stubs.o kern_mman.o kern_prot.o \
                   kern_prot2.o kern_proc.o kern_fork.o \
                   kern_exit.o kern_resource.o kern_sig.o kern_sig2.o \
                   kern_sysctl.o kern_sysv_shm.o sys_process.o \
                   kern_synch.o kern_clock.o subr_prf.o \
                   tty.o tty_subr.o vm_sched.o scheduler.o vers.o \
                   copyio.o \
                   copyio_selftest.o vm_machdep.o uarea_selftest.o \
                   context_selftest.o config.o sysctl.o process_bootstrap.o \
                   vm_param.o \
                   vm_phys.o vm_page.o vm_phys_board.o pmap.o \
                   $(VM_CORE_OBJS) vmspace_bootstrap.o boot_main.o

.DEFAULT_GOAL := all

.PHONY: all kernel bios-image toolchain-check rootfs-smoke ide-smoke \
	bios-image-smoke ide-absent-smoke bios-ide-absent-smoke \
	run run-serial debug boot-smoke bios-boot-smoke trap-smoke \
	exception-smoke boot-smoke-matrix clean

.PHONY: FORCE
FORCE:

all: toolchain-check kernel bios-image

kernel: rebsd-i686.bzimg

bios-image: rebsd-i686-bios-floppy.img

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
	    $(I386_SRC)/include/tss.h \
	    $(I386_SRC)/include/machine/layout.h
	$(CC) $(I686_ASFLAGS) $(I686_CPPFLAGS) -I$(I386_SRC)/include \
	    -c $< -o $@

privilege.o: $(I386_COMMON_SRC)/privilege.S $(I386_SRC)/include/context.h
	$(CC) $(I686_ASFLAGS) $(I686_CPPFLAGS) -I$(I386_SRC)/include \
	    -c $< -o $@

bootstrap_user.o: $(I386_USER_SRC)/bootstrap.S \
	    $(I386_SRC)/include/user_bootstrap.h
	$(CC) $(I686_ASFLAGS) -I$(I386_SRC)/include -c $< -o $@

bootstrap-user.elf: bootstrap_user.o $(I386_USER_SRC)/bootstrap.ld
	$(LD) $(I686_LDFLAGS) -T $(I386_USER_SRC)/bootstrap.ld \
	    -o $@ bootstrap_user.o

$(FSUTIL): $(wildcard $(TOPSRC)/tools/fsutil/*.c) \
	    $(TOPSRC)/tools/fsutil/bsdfs.h \
	    $(TOPSRC)/tools/fsutil/manifest.h
	$(MAKE) -C $(TOPSRC)/tools/fsutil OBJDIR=$(FSUTIL_DIR) \
	    CC="$(HOSTCC)" all

$(I386_ROOTFS_STAGE)/sbin/init: bootstrap-user.elf
	mkdir -p $(I386_ROOTFS_STAGE)/sbin
	install -m 0555 $< $@

rootfs.img: $(FSUTIL) $(I386_SRC)/rootfs.manifest \
	    $(I386_ROOTFS_STAGE)/sbin/init
	rm -f $@
	SOURCE_DATE_EPOCH=$(I386_ROOTFS_EPOCH) $(FSUTIL) --new \
	    --endian=little --size=$(I386_ROOTFS_KBYTES) \
	    --manifest=$(I386_SRC)/rootfs.manifest \
	    $@ $(I386_ROOTFS_STAGE)
	$(FSUTIL) --check $@

rootfs_image.o: rootfs.img
	$(OBJCOPY) -I binary -O elf32-i386 -B i386 \
	    --rename-section \
	    .data=.rodata.rootfs,alloc,load,readonly,data,contents \
	    $< $@

rootfs-smoke: rootfs.img bootstrap-user.elf
	rm -f rootfs.check.img
	SOURCE_DATE_EPOCH=$(I386_ROOTFS_EPOCH) $(FSUTIL) --new \
	    --endian=little --size=$(I386_ROOTFS_KBYTES) \
	    --manifest=$(I386_SRC)/rootfs.manifest \
	    rootfs.check.img $(I386_ROOTFS_STAGE)
	$(FSUTIL) --check rootfs.check.img
	cmp rootfs.img rootfs.check.img
	rm -f rootfs.check.img

early_console.o: $(I386_PC_SRC)/early_console.c \
	    $(I386_SRC)/include/boot.h $(I386_SRC)/include/io.h \
	    $(TOPSRC)/sys/include/reboot.h $(TOPSRC)/sys/include/types.h \
	    | $(I386_GEN_INCLUDE)/sys $(I386_GEN_INCLUDE)/machine
	$(CC) $(I686_CFLAGS) $(I686_CPPFLAGS) $(I386_VM_INCLUDES) \
	    -c $< -o $@

interrupt.o: $(I386_PC_SRC)/interrupt.c \
	    $(I386_SRC)/include/boot.h $(I386_SRC)/include/interrupt.h \
	    $(I386_SRC)/include/process.h \
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

pci.o: $(I386_PC_SRC)/pci.c $(I386_SRC)/include/boot.h \
	    $(I386_SRC)/include/io.h $(I386_SRC)/include/pci.h
	$(CC) $(I686_CFLAGS) $(I686_CPPFLAGS) -I$(I386_SRC)/include \
	    -c $< -o $@

ide.o: $(I386_PC_SRC)/ide.c $(I386_SRC)/include/boot.h \
	    $(I386_SRC)/include/ide.h $(I386_SRC)/include/io.h \
	    $(I386_DISK_SRC)/disk.h $(TOPSRC)/sys/include/errno.h \
	    | $(I386_GEN_INCLUDE)/sys $(I386_GEN_INCLUDE)/machine
	$(CC) $(I686_CFLAGS) $(I686_CPPFLAGS) $(I386_VM_INCLUDES) \
	    -c $< -o $@

disk_subr.o: $(I386_DISK_SRC)/disk_subr.c $(I386_DISK_SRC)/disk.h \
	    $(TOPSRC)/sys/include/disk.h $(TOPSRC)/sys/include/types.h \
	    | $(I386_GEN_INCLUDE)/sys $(I386_GEN_INCLUDE)/machine
	$(CC) $(I686_CFLAGS) $(I686_CPPFLAGS) $(I386_VM_INCLUDES) \
	    -c $< -o $@

disk.o: $(I386_DISK_SRC)/disk.c $(I386_DISK_SRC)/disk.h \
	    $(TOPSRC)/sys/include/buf.h $(TOPSRC)/sys/include/disk.h \
	    $(TOPSRC)/sys/include/errno.h $(TOPSRC)/sys/include/types.h \
	    | $(I386_GEN_INCLUDE)/sys $(I386_GEN_INCLUDE)/machine
	$(CC) $(I686_CFLAGS) $(I686_CPPFLAGS) $(I386_VM_INCLUDES) \
	    -DDISK_READ_CACHE_SLOTS=1u \
	    -DDISK_READ_AHEAD_MAX_SECTORS=16u \
	    -DDISK_WRITE_CACHE_SLOTS=1u \
	    -DDISK_WRITE_BACK_MAX_SECTORS=32u \
	    -c $< -o $@

disk_memory.o: $(I386_DISK_SRC)/disk_memory.c $(I386_DISK_SRC)/disk.h \
	    $(TOPSRC)/sys/include/disk.h $(TOPSRC)/sys/include/errno.h \
	    $(TOPSRC)/sys/include/types.h \
	    | $(I386_GEN_INCLUDE)/sys $(I386_GEN_INCLUDE)/machine
	$(CC) $(I686_CFLAGS) $(I686_CPPFLAGS) $(I386_VM_INCLUDES) \
	    -c $< -o $@

devsw.o: $(I386_PC_SRC)/devsw.c $(I386_DISK_SRC)/disk.h \
	    $(TOPSRC)/sys/include/buf.h $(TOPSRC)/sys/include/conf.h \
	    $(TOPSRC)/sys/include/errno.h $(TOPSRC)/sys/include/inode.h \
	    | $(I386_GEN_INCLUDE)/sys $(I386_GEN_INCLUDE)/machine
	$(CC) $(I686_CFLAGS) $(I686_CPPFLAGS) $(I386_VM_INCLUDES) \
	    -c $< -o $@

ufs_bio.o: $(TOPSRC)/sys/kernel/ufs_bio.c \
	    $(TOPSRC)/sys/include/buf.h $(TOPSRC)/sys/include/conf.h \
	    $(TOPSRC)/sys/include/user.h \
	    | $(I386_GEN_INCLUDE)/sys $(I386_GEN_INCLUDE)/machine
	$(CC) $(I686_CFLAGS) $(I686_CPPFLAGS) $(I386_VM_INCLUDES) \
	    -c $< -o $@

ufs_alloc.o: $(TOPSRC)/sys/kernel/ufs_alloc.c \
	    | $(I386_GEN_INCLUDE)/sys $(I386_GEN_INCLUDE)/machine
	$(CC) $(I686_CFLAGS) $(I686_CPPFLAGS) $(I386_VM_INCLUDES) \
	    -c $< -o $@

ufs_bmap.o: $(TOPSRC)/sys/kernel/ufs_bmap.c \
	    | $(I386_GEN_INCLUDE)/sys $(I386_GEN_INCLUDE)/machine
	$(CC) $(I686_CFLAGS) $(I686_CPPFLAGS) $(I386_VM_INCLUDES) \
	    -c $< -o $@

ufs_fio.o: $(TOPSRC)/sys/kernel/ufs_fio.c \
	    | $(I386_GEN_INCLUDE)/sys $(I386_GEN_INCLUDE)/machine
	$(CC) $(I686_CFLAGS) $(I686_CPPFLAGS) $(I386_VM_INCLUDES) \
	    -c $< -o $@

ufs_inode.o: $(TOPSRC)/sys/kernel/ufs_inode.c \
	    | $(I386_GEN_INCLUDE)/sys $(I386_GEN_INCLUDE)/machine
	$(CC) $(I686_CFLAGS) $(I686_CPPFLAGS) $(I386_VM_INCLUDES) \
	    -c $< -o $@

ufs_mount.o: $(TOPSRC)/sys/kernel/ufs_mount.c \
	    | $(I386_GEN_INCLUDE)/sys $(I386_GEN_INCLUDE)/machine
	$(CC) $(I686_CFLAGS) $(I686_CPPFLAGS) $(I386_VM_INCLUDES) \
	    -c $< -o $@

ufs_namei.o: $(TOPSRC)/sys/kernel/ufs_namei.c \
	    $(TOPSRC)/sys/include/inode.h $(TOPSRC)/sys/include/namei.h \
	    $(TOPSRC)/sys/include/mount.h \
	    | $(I386_GEN_INCLUDE)/sys $(I386_GEN_INCLUDE)/machine
	$(CC) $(I686_CFLAGS) $(I686_CPPFLAGS) $(I386_VM_INCLUDES) \
	    -c $< -o $@

ufs_subr.o: $(TOPSRC)/sys/kernel/ufs_subr.c \
	    | $(I386_GEN_INCLUDE)/sys $(I386_GEN_INCLUDE)/machine
	$(CC) $(I686_CFLAGS) $(I686_CPPFLAGS) $(I386_VM_INCLUDES) \
	    -c $< -o $@

sys_inode.o: $(TOPSRC)/sys/kernel/sys_inode.c \
	    | $(I386_GEN_INCLUDE)/sys $(I386_GEN_INCLUDE)/machine
	$(CC) $(I686_CFLAGS) $(I686_CPPFLAGS) $(I386_VM_INCLUDES) \
	    -c $< -o $@

kern_subr.o: $(TOPSRC)/sys/kernel/kern_subr.c \
	    | $(I386_GEN_INCLUDE)/sys $(I386_GEN_INCLUDE)/machine
	$(CC) $(I686_CFLAGS) $(I686_CPPFLAGS) $(I386_VM_INCLUDES) \
	    -c $< -o $@

kern_descrip.o: $(TOPSRC)/sys/kernel/kern_descrip.c \
	    | $(I386_GEN_INCLUDE)/sys $(I386_GEN_INCLUDE)/machine
	$(CC) $(I686_CFLAGS) $(I686_CPPFLAGS) $(I386_VM_INCLUDES) \
	    -c $< -o $@

kern_shm.o: $(TOPSRC)/sys/kernel/kern_shm.c \
	    | $(I386_GEN_INCLUDE)/sys $(I386_GEN_INCLUDE)/machine
	$(CC) $(I686_CFLAGS) $(I686_CPPFLAGS) $(I386_VM_INCLUDES) \
	    -c $< -o $@

kern_time.o: $(TOPSRC)/sys/kernel/kern_time.c \
	    | $(I386_GEN_INCLUDE)/sys $(I386_GEN_INCLUDE)/machine
	$(CC) $(I686_CFLAGS) $(I686_CPPFLAGS) $(I386_VM_INCLUDES) \
	    -c $< -o $@

sys_generic.o: $(TOPSRC)/sys/kernel/sys_generic.c \
	    | $(I386_GEN_INCLUDE)/sys $(I386_GEN_INCLUDE)/machine
	$(CC) $(I686_CFLAGS) $(I686_CPPFLAGS) $(I386_VM_INCLUDES) \
	    -c $< -o $@

sys_pipe.o: $(TOPSRC)/sys/kernel/sys_pipe.c \
	    | $(I386_GEN_INCLUDE)/sys $(I386_GEN_INCLUDE)/machine
	$(CC) $(I686_CFLAGS) $(I686_CPPFLAGS) $(I386_VM_INCLUDES) \
	    -c $< -o $@

ufs_syscalls.o: $(TOPSRC)/sys/kernel/ufs_syscalls.c \
	    | $(I386_GEN_INCLUDE)/sys $(I386_GEN_INCLUDE)/machine
	$(CC) $(I686_CFLAGS) $(I686_CPPFLAGS) $(I386_VM_INCLUDES) \
	    -c $< -o $@

ufs_syscalls2.o: $(TOPSRC)/sys/kernel/ufs_syscalls2.c \
	    | $(I386_GEN_INCLUDE)/sys $(I386_GEN_INCLUDE)/machine
	$(CC) $(I686_CFLAGS) $(I686_CPPFLAGS) $(I386_VM_INCLUDES) \
	    -c $< -o $@

vfs_vnops.o: $(TOPSRC)/sys/kernel/vfs_vnops.c \
	    | $(I386_GEN_INCLUDE)/sys $(I386_GEN_INCLUDE)/machine
	$(CC) $(I686_CFLAGS) $(I686_CPPFLAGS) $(I386_VM_INCLUDES) \
	    -c $< -o $@

disk_bootstrap.o: $(I386_PC_SRC)/disk_bootstrap.c \
	    $(I386_SRC)/include/boot.h \
	    $(I386_SRC)/include/disk_bootstrap.h \
	    $(I386_SRC)/include/vfs_bootstrap.h \
	    $(I386_SRC)/include/ide.h $(I386_DISK_SRC)/disk.h \
	    $(TOPSRC)/sys/include/buf.h $(TOPSRC)/sys/include/disk.h \
	    $(TOPSRC)/sys/include/errno.h $(TOPSRC)/sys/include/fcntl.h \
	    $(TOPSRC)/sys/include/ioctl.h \
	    | $(I386_GEN_INCLUDE)/sys $(I386_GEN_INCLUDE)/machine
	$(CC) $(I686_CFLAGS) $(I686_CPPFLAGS) $(I386_VM_INCLUDES) \
	    -c $< -o $@

vfs_bootstrap.o: $(I386_PC_SRC)/vfs_bootstrap.c \
	    $(I386_SRC)/include/boot.h \
	    $(I386_SRC)/include/vfs_bootstrap.h \
	    $(I386_SRC)/include/machine/layout.h \
	    $(TOPSRC)/sys/include/buf.h $(TOPSRC)/sys/include/conf.h \
	    $(TOPSRC)/sys/include/inode.h $(TOPSRC)/sys/include/mount.h \
	    $(TOPSRC)/sys/include/namei.h $(TOPSRC)/sys/include/uio.h \
	    | $(I386_GEN_INCLUDE)/sys $(I386_GEN_INCLUDE)/machine
	$(CC) $(I686_CFLAGS) $(I686_CPPFLAGS) $(I386_VM_INCLUDES) \
	    -c $< -o $@

vm_vnode.o: $(I386_VM_SRC)/vm_vnode.c \
	    | $(I386_GEN_INCLUDE)/sys $(I386_GEN_INCLUDE)/machine
	$(CC) $(I686_CFLAGS) $(I686_CPPFLAGS) $(I386_VM_INCLUDES) \
	    -c $< -o $@

memory.o: $(I386_PC_SRC)/memory.c $(I386_SRC)/include/boot.h \
	    $(I386_SRC)/include/memory.h $(TOPSRC)/sys/include/systm.h \
	    | $(I386_GEN_INCLUDE)/sys $(I386_GEN_INCLUDE)/machine
	$(CC) $(I686_CFLAGS) $(I686_CPPFLAGS) $(I386_VM_INCLUDES) \
	    -c $< -o $@

paging.o: $(I386_PC_SRC)/paging.c $(I386_SRC)/include/memory.h \
	    $(I386_SRC)/include/paging.h
	$(CC) $(I686_CFLAGS) $(I686_CPPFLAGS) -I$(I386_SRC)/include \
	    -c $< -o $@

libkern.o: $(TOPSRC)/sys/kernel/libkern.c \
	    | $(I386_GEN_INCLUDE)/sys $(I386_GEN_INCLUDE)/machine
	$(CC) $(I686_CFLAGS) $(I686_CPPFLAGS) $(I386_VM_INCLUDES) \
	    -c $< -o $@

exec_elf_loader.o: $(TOPSRC)/sys/kernel/exec_elf_loader.c \
	    $(TOPSRC)/sys/include/exec_elf.h \
	    $(TOPSRC)/sys/include/exec_elf_loader.h \
	    $(I386_VM_SRC)/vmspace.h \
	    | $(I386_GEN_INCLUDE)/sys $(I386_GEN_INCLUDE)/machine
	$(CC) $(I686_CFLAGS) $(I686_CPPFLAGS) $(I386_VM_INCLUDES) \
	    -c $< -o $@

init_process.o: $(TOPSRC)/sys/kernel/init_process.c \
	    $(TOPSRC)/sys/include/param.h $(TOPSRC)/sys/include/proc.h \
	    $(TOPSRC)/sys/include/systm.h $(TOPSRC)/sys/include/user.h \
	    $(I386_VM_SRC)/pmap.h $(I386_VM_SRC)/vmspace.h \
	    | $(I386_GEN_INCLUDE)/sys $(I386_GEN_INCLUDE)/machine
	$(CC) $(I686_CFLAGS) $(I686_CPPFLAGS) $(I386_VM_INCLUDES) \
	    -c $< -o $@

exec_aout.o exec_conf.o exec_elf.o exec_script.o kern_exec.o \
kern_mman.o kern_prot2.o kern_sysctl.o kern_sysv_shm.o: %.o: \
	    $(TOPSRC)/sys/kernel/%.c \
	    | $(I386_GEN_INCLUDE)/sys $(I386_GEN_INCLUDE)/machine
	$(CC) $(I686_CFLAGS) $(I686_CPPFLAGS) $(I386_VM_INCLUDES) \
	    -c $< -o $@

exec_subr.o: $(TOPSRC)/sys/kernel/exec_subr.c \
	    $(TOPSRC)/sys/include/debug.h $(TOPSRC)/sys/include/exec.h \
	    $(I386_VM_SRC)/vmspace.h \
	    | $(I386_GEN_INCLUDE)/sys $(I386_GEN_INCLUDE)/machine
	$(CC) $(I686_CFLAGS) $(I686_CPPFLAGS) $(I386_VM_INCLUDES) \
	    -c $< -o $@

init_sysent.o: $(TOPSRC)/sys/kernel/init_sysent.c \
	    $(TOPSRC)/sys/include/systm.h $(TOPSRC)/sys/include/user.h \
	    | $(I386_GEN_INCLUDE)/sys $(I386_GEN_INCLUDE)/machine
	$(CC) $(I686_CFLAGS) $(I686_CPPFLAGS) $(I386_VM_INCLUDES) \
	    -c $< -o $@

stubs.o: $(TOPSRC)/sys/kernel/stubs.c \
	    $(TOPSRC)/sys/include/proc.h $(TOPSRC)/sys/include/signalvar.h \
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

kern_proc.o: $(TOPSRC)/sys/kernel/kern_proc.c \
	    $(TOPSRC)/sys/include/proc.h $(TOPSRC)/sys/include/systm.h \
	    $(TOPSRC)/sys/include/user.h $(TOPSRC)/sys/include/time.h \
	    $(TOPSRC)/sys/include/resource.h \
	    | $(I386_GEN_INCLUDE)/sys $(I386_GEN_INCLUDE)/machine
	$(CC) $(I686_CFLAGS) $(I686_CPPFLAGS) $(I386_VM_INCLUDES) \
	    -c $< -o $@

kern_fork.o: $(TOPSRC)/sys/kernel/kern_fork.c \
	    $(TOPSRC)/sys/include/file.h $(TOPSRC)/sys/include/inode.h \
	    $(TOPSRC)/sys/include/kernel.h $(TOPSRC)/sys/include/proc.h \
	    $(TOPSRC)/sys/include/systm.h $(TOPSRC)/sys/include/user.h \
	    $(I386_VM_SRC)/vmspace.h \
	    | $(I386_GEN_INCLUDE)/sys $(I386_GEN_INCLUDE)/machine
	$(CC) $(I686_CFLAGS) $(I686_CPPFLAGS) $(I386_VM_INCLUDES) \
	    -c $< -o $@

kern_exit.o: $(TOPSRC)/sys/kernel/kern_exit.c \
	    $(TOPSRC)/sys/include/file.h $(TOPSRC)/sys/include/inode.h \
	    $(TOPSRC)/sys/include/kernel.h $(TOPSRC)/sys/include/proc.h \
	    $(TOPSRC)/sys/include/resource.h $(TOPSRC)/sys/include/systm.h \
	    $(TOPSRC)/sys/include/user.h $(TOPSRC)/sys/include/wait.h \
	    $(I386_VM_SRC)/vmspace.h \
	    | $(I386_GEN_INCLUDE)/sys $(I386_GEN_INCLUDE)/machine
	$(CC) $(I686_CFLAGS) $(I686_CPPFLAGS) $(I386_VM_INCLUDES) \
	    -c $< -o $@

kern_resource.o: $(TOPSRC)/sys/kernel/kern_resource.c \
	    $(TOPSRC)/sys/include/kernel.h $(TOPSRC)/sys/include/proc.h \
	    $(TOPSRC)/sys/include/resource.h $(TOPSRC)/sys/include/systm.h \
	    $(TOPSRC)/sys/include/user.h \
	    | $(I386_GEN_INCLUDE)/sys $(I386_GEN_INCLUDE)/machine
	$(CC) $(I686_CFLAGS) $(I686_CPPFLAGS) $(I386_VM_INCLUDES) \
	    -c $< -o $@

kern_sig.o: $(TOPSRC)/sys/kernel/kern_sig.c \
	    $(TOPSRC)/sys/include/proc.h $(TOPSRC)/sys/include/signalvar.h \
	    $(TOPSRC)/sys/include/systm.h $(TOPSRC)/sys/include/user.h \
	    $(I386_VM_SRC)/vmspace.h \
	    | $(I386_GEN_INCLUDE)/sys $(I386_GEN_INCLUDE)/machine
	$(CC) $(I686_CFLAGS) $(I686_CPPFLAGS) $(I386_VM_INCLUDES) \
	    -c $< -o $@

kern_sig2.o: $(TOPSRC)/sys/kernel/kern_sig2.c \
	    $(TOPSRC)/sys/include/proc.h $(TOPSRC)/sys/include/signalvar.h \
	    $(TOPSRC)/sys/include/systm.h $(TOPSRC)/sys/include/user.h \
	    | $(I386_GEN_INCLUDE)/sys $(I386_GEN_INCLUDE)/machine
	$(CC) $(I686_CFLAGS) $(I686_CPPFLAGS) $(I386_VM_INCLUDES) \
	    -c $< -o $@

sys_process.o: $(TOPSRC)/sys/kernel/sys_process.c \
	    $(TOPSRC)/sys/include/proc.h $(TOPSRC)/sys/include/ptrace.h \
	    $(TOPSRC)/sys/include/systm.h $(TOPSRC)/sys/include/user.h \
	    | $(I386_GEN_INCLUDE)/sys $(I386_GEN_INCLUDE)/machine
	$(CC) $(I686_CFLAGS) $(I686_CPPFLAGS) $(I386_VM_INCLUDES) \
	    -c $< -o $@

kern_synch.o: $(TOPSRC)/sys/kernel/kern_synch.c \
	    $(TOPSRC)/sys/include/proc.h $(TOPSRC)/sys/include/systm.h \
	    $(TOPSRC)/sys/include/user.h $(I386_VM_SRC)/vmspace.h \
	    | $(I386_GEN_INCLUDE)/sys $(I386_GEN_INCLUDE)/machine
	$(CC) $(I686_CFLAGS) $(I686_CPPFLAGS) $(I386_VM_INCLUDES) \
	    -c $< -o $@

kern_clock.o: $(TOPSRC)/sys/kernel/kern_clock.c \
	    $(TOPSRC)/sys/include/callout.h $(TOPSRC)/sys/include/kernel.h \
	    $(TOPSRC)/sys/include/proc.h $(TOPSRC)/sys/include/systm.h \
	    $(TOPSRC)/sys/include/user.h \
	    | $(I386_GEN_INCLUDE)/sys $(I386_GEN_INCLUDE)/machine
	$(CC) $(I686_CFLAGS) $(I686_CPPFLAGS) $(I386_VM_INCLUDES) \
	    -c $< -o $@

vm_sched.o: $(TOPSRC)/sys/kernel/vm_sched.c \
	    | $(I386_GEN_INCLUDE)/sys $(I386_GEN_INCLUDE)/machine
	$(CC) $(I686_CFLAGS) $(I686_CPPFLAGS) $(I386_VM_INCLUDES) \
	    -c $< -o $@

vers.c: FORCE $(I386_VERSION_GENERATOR) \
	    $(TOPSRC)/sys/include/rebsd_version.h
	$(PYTHON) $(I386_VERSION_GENERATOR) --output $@ \
	    --version-header "$(TOPSRC)/sys/include/rebsd_version.h" \
	    --ident I686_PC --build-number 1 \
	    --build-date "reproducible i686 build" --compiler gcc \
	    --build-user unknown --build-host unknown \
	    --toolchain "$(notdir $(CC))" \
	    --toolchain-version "$(I386_COMPILER_VERSION)" \
	    --git-rev "$(I386_GIT_REV)" --git-branch "$(I386_GIT_BRANCH)" \
	    --dirty "$(I386_GIT_DIRTY)" --target i386 --cpu i686 \
	    --fpu soft --byteorder little

vers.o: vers.c
	$(CC) $(I686_CFLAGS) $(I686_CPPFLAGS) $(I386_VM_INCLUDES) \
	    -c $< -o $@

subr_prf.o: $(TOPSRC)/sys/kernel/subr_prf.c \
	    $(TOPSRC)/sys/include/systm.h \
	    | $(I386_GEN_INCLUDE)/sys $(I386_GEN_INCLUDE)/machine
	$(CC) $(I686_CFLAGS) $(I686_CPPFLAGS) $(I386_VM_INCLUDES) \
	    -c $< -o $@

tty.o: $(TOPSRC)/sys/kernel/tty.c \
	    $(TOPSRC)/sys/include/tty.h $(TOPSRC)/sys/include/systm.h \
	    | $(I386_GEN_INCLUDE)/sys $(I386_GEN_INCLUDE)/machine
	$(CC) $(I686_CFLAGS) $(I686_CPPFLAGS) $(I386_VM_INCLUDES) \
	    -c $< -o $@

tty_subr.o: $(TOPSRC)/sys/kernel/tty_subr.c \
	    $(TOPSRC)/sys/include/clist.h $(TOPSRC)/sys/include/tty.h \
	    | $(I386_GEN_INCLUDE)/sys $(I386_GEN_INCLUDE)/machine
	$(CC) $(I686_CFLAGS) $(I686_CPPFLAGS) $(I386_VM_INCLUDES) \
	    -c $< -o $@

scheduler.o: $(I386_PC_SRC)/scheduler.c \
	    $(I386_SRC)/include/boot.h \
	    $(I386_SRC)/include/machine/machparam.h \
	    $(TOPSRC)/sys/include/systm.h $(TOPSRC)/sys/include/time.h \
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

config.o: $(I386_PC_SRC)/config.c \
	    $(TOPSRC)/sys/include/proc.h $(TOPSRC)/sys/include/time.h \
	    $(TOPSRC)/sys/include/resource.h $(TOPSRC)/sys/include/kernel.h \
	    | $(I386_GEN_INCLUDE)/sys $(I386_GEN_INCLUDE)/machine
	$(CC) $(I686_CFLAGS) $(I686_CPPFLAGS) $(I386_VM_INCLUDES) \
	    -c $< -o $@

sysctl.o: $(I386_PC_SRC)/sysctl.c \
	    $(I386_SRC)/include/machine/cpu.h \
	    $(TOPSRC)/sys/include/sysctl.h $(TOPSRC)/sys/include/systm.h \
	    | $(I386_GEN_INCLUDE)/sys $(I386_GEN_INCLUDE)/machine
	$(CC) $(I686_CFLAGS) $(I686_CPPFLAGS) $(I386_VM_INCLUDES) \
	    -c $< -o $@

process_bootstrap.o: $(I386_PC_SRC)/process_bootstrap.c \
	    $(I386_SRC)/include/boot.h $(I386_SRC)/include/context.h \
	    $(I386_SRC)/include/interrupt.h \
	    $(I386_SRC)/include/privilege.h \
	    $(I386_SRC)/include/process.h $(I386_SRC)/include/syscall.h \
	    $(I386_SRC)/include/tss.h \
	    $(I386_SRC)/include/user_bootstrap.h \
	    $(I386_SRC)/include/vmspace_bootstrap.h \
	    $(I386_SRC)/include/machine/layout.h \
	    $(TOPSRC)/sys/include/exec.h \
	    $(TOPSRC)/sys/include/file.h \
	    $(TOPSRC)/sys/include/proc.h \
	    $(TOPSRC)/sys/include/user.h \
	    $(I386_VM_SRC)/vmspace.h \
	    | $(I386_GEN_INCLUDE)/sys $(I386_GEN_INCLUDE)/machine
	$(CC) $(I686_CFLAGS) $(I686_CPPFLAGS) $(I386_VM_INCLUDES) \
	    -c $< -o $@

$(I386_GEN_INCLUDE)/sys:
	mkdir -p $(I386_GEN_INCLUDE)
	rm -f $@
	ln -s $(abspath $(TOPSRC)/sys/include) $@

$(I386_GEN_INCLUDE)/machine:
	mkdir -p $(I386_GEN_INCLUDE)
	rm -f $@
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

vm_phys_board.o: $(I386_PC_SRC)/vm_phys_board.c \
	    $(I386_SRC)/include/memory.h \
	    $(I386_VM_SRC)/vm_phys.h \
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
	    $(TOPSRC)/sys/include/proc.h $(TOPSRC)/sys/include/user.h \
	    $(I386_VM_SRC)/vmspace.h $(I386_VM_SRC)/vm_object.h \
	    | $(I386_GEN_INCLUDE)/sys $(I386_GEN_INCLUDE)/machine
	$(CC) $(I686_CFLAGS) $(I686_CPPFLAGS) $(I386_VM_INCLUDES) \
	    -c $< -o $@

boot_main.o: $(I386_PC_SRC)/boot_main.c \
	    $(I386_SRC)/include/boot.h $(I386_SRC)/include/copyio.h \
	    $(I386_SRC)/include/disk_bootstrap.h \
	    $(I386_SRC)/include/ide.h \
	    $(I386_SRC)/include/interrupt.h \
	    $(I386_SRC)/include/memory.h $(I386_SRC)/include/paging.h \
	    $(I386_SRC)/include/pci.h \
	    $(I386_SRC)/include/pmap_bootstrap.h $(I386_SRC)/include/privilege.h \
	    $(I386_SRC)/include/process.h \
	    $(I386_SRC)/include/signal_machdep.h \
	    $(I386_SRC)/include/syscall.h \
	    $(I386_SRC)/include/tss.h \
	    $(I386_SRC)/include/trap.h \
	    $(I386_SRC)/include/user_return.h \
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

rebsd-i686-bios-floppy.img: rebsd-i686.bzimg \
	    $(I386_TOOLS_SRC)/mkbios.py
	$(PYTHON) $(I386_TOOLS_SRC)/mkbios.py \
	    --kernel $< --output $@

bios-image-smoke: rebsd-i686.bzimg rebsd-i686-bios-floppy.img
	$(PYTHON) $(I386_TOOLS_SRC)/mkbios.py \
	    --kernel rebsd-i686.bzimg \
	    --output rebsd-i686-bios-floppy.check.img
	cmp rebsd-i686-bios-floppy.img \
	    rebsd-i686-bios-floppy.check.img
	rm -f rebsd-i686-bios-floppy.check.img

ide-smoke: rootfs-smoke

run run-serial: rebsd-i686.bzimg rootfs.img
	$(QEMU_I386) -machine $(QEMU_MACHINE) -cpu $(QEMU_CPU) \
	    -accel tcg -m $(QEMU_MEMORY) -kernel $< \
	    -drive file=rootfs.img,format=raw,if=ide,index=0,media=disk,snapshot=on \
	    -serial stdio -display none -monitor none \
	    -no-reboot -no-shutdown

debug: rebsd-i686.bzimg rootfs.img
	$(QEMU_I386) -machine $(QEMU_MACHINE) -cpu $(QEMU_CPU) \
	    -accel tcg -m $(QEMU_MEMORY) -kernel $< \
	    -drive file=rootfs.img,format=raw,if=ide,index=0,media=disk,snapshot=on \
	    -serial stdio -display none -monitor none \
	    -no-reboot -no-shutdown -S -s

boot-smoke: rebsd-i686.bzimg rootfs.img
	$(PYTHON) $(I386_TOOLS_SRC)/qemu-boot-smoke.py \
	    --qemu $(QEMU_I386) --machine $(QEMU_MACHINE) \
	    --cpu $(QEMU_CPU) --memory $(QEMU_MEMORY) --kernel $< \
	    --disk rootfs.img

bios-boot-smoke: rebsd-i686-bios-floppy.img rootfs.img
	$(PYTHON) $(I386_TOOLS_SRC)/qemu-boot-smoke.py \
	    --qemu $(QEMU_I386) --machine $(QEMU_MACHINE) \
	    --cpu $(QEMU_CPU) --memory $(QEMU_MEMORY) \
	    --bios-image rebsd-i686-bios-floppy.img \
	    --disk rootfs.img

bios-ide-absent-smoke: rebsd-i686-bios-floppy.img
	$(PYTHON) $(I386_TOOLS_SRC)/qemu-boot-smoke.py \
	    --qemu $(QEMU_I386) --machine $(QEMU_MACHINE) \
	    --cpu $(QEMU_CPU) --memory $(QEMU_MEMORY) \
	    --bios-image rebsd-i686-bios-floppy.img \
	    --expect-no-disk

ide-absent-smoke: rebsd-i686.bzimg
	$(PYTHON) $(I386_TOOLS_SRC)/qemu-boot-smoke.py \
	    --qemu $(QEMU_I386) --machine $(QEMU_MACHINE) \
	    --cpu $(QEMU_CPU) --memory $(QEMU_MEMORY) --kernel $< \
	    --expect-no-disk

trap-smoke: rebsd-i686.bzimg rootfs.img
	$(PYTHON) $(I386_TOOLS_SRC)/qemu-boot-smoke.py \
	    --qemu $(QEMU_I386) --machine $(QEMU_MACHINE) \
	    --cpu $(QEMU_CPU) --memory $(QEMU_MEMORY) --kernel $< \
	    --disk rootfs.img \
	    --expect-exception divide
	$(PYTHON) $(I386_TOOLS_SRC)/qemu-boot-smoke.py \
	    --qemu $(QEMU_I386) --machine $(QEMU_MACHINE) \
	    --cpu $(QEMU_CPU) --memory $(QEMU_MEMORY) --kernel $< \
	    --disk rootfs.img \
	    --expect-exception gp
	$(PYTHON) $(I386_TOOLS_SRC)/qemu-boot-smoke.py \
	    --qemu $(QEMU_I386) --machine $(QEMU_MACHINE) \
	    --cpu $(QEMU_CPU) --memory $(QEMU_MEMORY) --kernel $< \
	    --disk rootfs.img \
	    --expect-exception page

exception-smoke: trap-smoke

boot-smoke-matrix: rebsd-i686.bzimg rootfs.img
	@for memory in 32M 64M 128M 256M 768M 1024M; do \
	    echo "boot-smoke-matrix: memory=$$memory"; \
	    $(PYTHON) $(I386_TOOLS_SRC)/qemu-boot-smoke.py \
	        --qemu $(QEMU_I386) --machine $(QEMU_MACHINE) \
	        --cpu $(QEMU_CPU) --memory $$memory --kernel $< \
	        --disk rootfs.img || exit 1; \
	done

clean:
	rm -f setup.o setup.elf setup.bin setup.map \
	    bootstrap_user.o bootstrap-user.elf bootstrap_user_image.o \
	    vers.c \
	    rootfs.img rootfs.check.img rootfs_image.o \
	    rebsd-i686-bios-floppy.img \
	    rebsd-i686-bios-floppy.check.img \
	    $(KERNEL_OBJS) rebsd-i686.elf rebsd-i686.bin \
	    rebsd-i686.bzimg rebsd-i686.map
	rm -rf $(I386_ROOTFS_STAGE)
	rm -f $(I386_GEN_INCLUDE)/sys $(I386_GEN_INCLUDE)/machine
	rmdir $(I386_GEN_INCLUDE) 2>/dev/null || true

endif
