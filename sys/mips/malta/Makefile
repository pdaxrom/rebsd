PARAM = -DMALTA
PARAM += -DMIPS32
PARAM += -DMALTA_UART_ENABLED
PARAM += -DMALTA_ENABLED
PARAM += -DPTY_ENABLED
PARAM += -DPTY_NUNITS=4
PARAM += -DCONS_MINOR=0
PARAM += -DCONS_MAJOR=0
#
# Makefile template for shared MIPS ports.
#
MALTA_CPU_TUNE ?= 24kf
MIPS_ARCH ?= -EB -mips32 -mabi=32 -mtune=$(MALTA_CPU_TUNE)
MALTA_QEMU_RAM ?= 32M

include ../../../target-mips.mk

TOPSRC          = ../../..
S               = ../..
BOARD_SRC       = ../malta
MIPS_ROOTFS_KBYTES ?= 16384
MIPS_ROOTFS_SRC ?= $(TOPSRC)/sys/mips/n64/rootfs.stage
MIPS_ROOTFS_OVERLAY ?= $(TOPSRC)/sys/mips/malta/rootfs
MIPS_ROOTFS_MANIFEST ?= $(TOPSRC)/sys/mips/malta/rootfs.manifest
MIPS_ROOTFS_BUILD_MANIFEST = rootfs.generated.manifest
MIPS_ROOTFS_STAGE = rootfs.stage
MIPS_ROOTFS_STAMP = $(MIPS_ROOTFS_STAGE)/.stamp
MIPS_ROOTFS_OVERLAY_FILES = $(shell find $(MIPS_ROOTFS_OVERLAY) -type f -o -type l 2>/dev/null)
MIPS_ROOTFS_COPY_DIRS = usr/include
MIPS_ROOTFS_COPY_PATHS = \
    bin/cat bin/chmod bin/df bin/diskspeed bin/echo bin/expr bin/false \
    bin/login bin/ls bin/mkdir bin/mv bin/od bin/ps bin/pwd bin/rm \
    bin/rmdir bin/romfsctl bin/sed bin/sh bin/stty bin/sync bin/test \
    bin/tr bin/true bin/uname bin/vmstat bin/w bin/wc \
    usr/bin/ar usr/bin/as usr/bin/cc usr/bin/cpp usr/bin/ld \
    usr/bin/matrix-as-vr4300 usr/bin/matrix-as-vr4300.sh usr/bin/nm \
    usr/bin/pcc usr/bin/ranlib usr/bin/size usr/bin/smoke-as-vr4300 \
    usr/bin/smoke-as-vr4300.sh usr/bin/strip \
    sbin/init sbin/mkfs sbin/mount sbin/pstat sbin/umount \
    usr/lib/crt0.o usr/lib/libc.a usr/lib/libm.a usr/libexec/ccom \
    libexec/getty \
    etc/gettytab etc/group etc/passwd etc/profile \
    root/cc-pcc-smoke.sh root/ll-smoke.c root/ll-smoke.sh \
    root/ll-abi-smoke.c root/ll-abi-smoke.s root/ll-abi-smoke.sh \
    root/romfs-smoke.sh \
    root/pcc-fpu-smoke.c root/pcc-smoke.c root/pcc-smoke.sh \
    root/sh-comsubst-smoke.sh root/sh-fail-smoke.sh root/types-smoke.c \
    root/types-smoke.sh
MIPS_ROOTFS_COPY_FILES = $(addprefix $(MIPS_ROOTFS_SRC)/,$(MIPS_ROOTFS_COPY_PATHS))
MIPS_TRACE       ?= 0
FSUTIL          = $(TOPSRC)/tools/fsutil/fsutil

CPPFLAGS        = -I. -I$(TOPSRC)/include ${PARAM} -DKERNEL -DMIPS \
                  -DROMFS_ENABLED \
                  -DHW_MACHINE_NAME=\"mips\" \
                  -DHW_MODEL_NAME='"MIPS Malta 24Kf"'
ifeq ($(MIPS_TRACE),1)
CPPFLAGS        += -DMIPS_TRACE
endif
CFLAGS          = -O2 -Wall -Werror -Wno-array-bounds -nostdinc \
                  $(CPPFLAGS) -MT $@ -MP -MD -MF .deps/$*.dep
VERS_CFLAGS     = -O2 -Wall -Werror -Wno-array-bounds -nostdinc $(CPPFLAGS)
ASFLAGS         = -x assembler-with-cpp -c $(CPPFLAGS) -MT $@ -MP -MD \
                  -MF .deps/$*.dep
FPU_ASFLAGS     = $(ASFLAGS)

COMPILE_C       = ${CC} ${CFLAGS} -c $<
COMPILE_S       = ${AS} ${ASFLAGS} $< -o $@
COMPILE_FPU_S   = ${AS} ${FPU_ASFLAGS} $< -o $@

OBJS = exec_aout.o exec_conf.o exec_elf.o exec_script.o exec_subr.o \
	init_main.o init_sysent.o kern_clock.o kern_descrip.o \
	kern_exec.o kern_exit.o kern_fork.o kern_glob.o kern_mman.o \
	kern_proc.o kern_prot.o kern_prot2.o kern_resource.o kern_sig.o \
	kern_sig2.o kern_subr.o kern_synch.o kern_sysctl.o kern_time.o \
	subr_prf.o subr_rmap.o sys_generic.o sys_inode.o sys_pipe.o \
	sys_process.o syscalls.o tty.o tty_pty.o tty_subr.o tty_tty.o \
	ufs_alloc.o ufs_bio.o ufs_bmap.o ufs_dsort.o ufs_fio.o \
	ufs_inode.o ufs_mount.o ufs_namei.o ufs_subr.o ufs_syscalls.o \
	ufs_syscalls2.o vfs_vnops.o vm_sched.o vm_swap.o vm_swp.o \
	startup.o exception_entry.o exception.o fpu.o cons.o signal.o \
	sysctl.o libkern.o stubs.o uart.o devsw.o clock.o machdep.o \
	romdisk.o romfs_core.o romfs_vfs.o cartflash.o ramswap.o \
	config.o

CFILES = $S/kernel/exec_aout.c $S/kernel/exec_conf.c $S/kernel/exec_elf.c \
	$S/kernel/exec_script.c $S/kernel/exec_subr.c \
	$S/kernel/init_main.c $S/kernel/init_sysent.c \
	$S/kernel/kern_clock.c $S/kernel/kern_descrip.c \
	$S/kernel/kern_exec.c $S/kernel/kern_exit.c \
	$S/kernel/kern_fork.c $S/kernel/kern_glob.c \
	$S/kernel/kern_mman.c $S/kernel/kern_proc.c \
	$S/kernel/kern_prot.c $S/kernel/kern_prot2.c \
	$S/kernel/kern_resource.c $S/kernel/kern_sig.c \
	$S/kernel/kern_sig2.c $S/kernel/kern_subr.c \
	$S/kernel/kern_synch.c $S/kernel/kern_sysctl.c \
	$S/kernel/kern_time.c $S/kernel/subr_prf.c $S/kernel/subr_rmap.c \
	$S/kernel/sys_generic.c $S/kernel/sys_inode.c \
	$S/kernel/sys_pipe.c $S/kernel/sys_process.c \
	$S/kernel/syscalls.c $S/kernel/tty.c $S/kernel/tty_pty.c \
	$S/kernel/tty_subr.c $S/kernel/tty_tty.c $S/kernel/ufs_alloc.c \
	$S/kernel/ufs_bio.c $S/kernel/ufs_bmap.c $S/kernel/ufs_dsort.c \
	$S/kernel/ufs_fio.c $S/kernel/ufs_inode.c $S/kernel/ufs_mount.c \
	$S/kernel/ufs_namei.c $S/kernel/ufs_subr.c \
	$S/kernel/ufs_syscalls.c $S/kernel/ufs_syscalls2.c \
	$S/kernel/vfs_vnops.c $S/kernel/vm_sched.c $S/kernel/vm_swap.c \
	$S/kernel/vm_swp.c $S/mips/common/exception.c \
	$S/mips/common/cons.c $S/mips/common/signal.c \
	$S/mips/common/sysctl.c $S/mips/common/libkern.c \
	$S/mips/common/stubs.c $S/mips/malta/uart.c \
	$S/mips/malta/devsw.c $S/mips/malta/clock.c \
	$S/mips/malta/machdep.c $S/mips/malta/romdisk.c \
	$S/mips/common/romfs_core.c $S/mips/common/romfs_vfs.c \
	$S/mips/malta/cartflash.c $S/mips/malta/ramswap.c \
	$S/mips/malta/config.c swapunix.c

SYSTEM_OBJ      = ${OBJS} ioconf.o rootfs.o
SYSTEM_DEP      = Makefile ioconf.c ../malta/malta.ld
SYSTEM_LD_HEAD  = printf '%s\n' 'const char version[] = "2.11 BSD Unix for Malta: local build\n";' > vers.c; \
                  ${CC} ${VERS_CFLAGS} -MT vers.o -MP -MD -MF .deps/vers.dep -c vers.c -o vers.o; \
                  rm -f $@
SYSTEM_LD       = ${LD} -nostdlib -T ../malta/malta.ld ${SYSTEM_OBJ} vers.o -o $@
SYSTEM_LD_TAIL  = ${SIZE} $@; \
                  ${OBJDUMP} -d -S $@ > $(basename $@).dis; \
                  ${NM} -n $@ > $(basename $@).nm

all: unix malta-early.elf

kernel: unix

malta-early.o: ../malta/early_uart.S | .deps
	$(CC) $(ASFLAGS) $< -o $@

malta-early.elf: malta-early.o ../malta/malta.ld
	$(LD) -nostdlib -T ../malta/malta.ld malta-early.o -o $@
	$(SIZE) $@
	$(OBJDUMP) -d -S $@ > malta-early.dis

run-early: malta-early.elf
	qemu-system-mips -M malta -m $(MALTA_QEMU_RAM) -nographic -serial mon:stdio \
	    -no-reboot -kernel malta-early.elf

run: unix.elf
	qemu-system-mips -M malta -m $(MALTA_QEMU_RAM) -nographic -serial mon:stdio \
	    -no-reboot -kernel unix.elf

$(MIPS_ROOTFS_STAMP): $(MIPS_ROOTFS_MANIFEST) $(MIPS_ROOTFS_OVERLAY_FILES) $(MIPS_ROOTFS_COPY_FILES)
	rm -rf $(MIPS_ROOTFS_STAGE)
	mkdir -p $(MIPS_ROOTFS_STAGE)
	cp -pR $(MIPS_ROOTFS_OVERLAY)/. $(MIPS_ROOTFS_STAGE)/
	mkdir -p $(MIPS_ROOTFS_STAGE)/bin $(MIPS_ROOTFS_STAGE)/sbin \
	    $(MIPS_ROOTFS_STAGE)/lib $(MIPS_ROOTFS_STAGE)/libexec \
	    $(MIPS_ROOTFS_STAGE)/usr/bin $(MIPS_ROOTFS_STAGE)/usr/lib \
	    $(MIPS_ROOTFS_STAGE)/usr/libexec $(MIPS_ROOTFS_STAGE)/usr/include \
	    $(MIPS_ROOTFS_STAGE)/etc $(MIPS_ROOTFS_STAGE)/root
	for path in . $(MIPS_ROOTFS_COPY_DIRS); do \
	    test "$$path" != . || continue; \
	    mkdir -p $(MIPS_ROOTFS_STAGE)/$$path; \
	    cp -pR $(MIPS_ROOTFS_SRC)/$$path/. $(MIPS_ROOTFS_STAGE)/$$path/; \
	done
	for path in $(MIPS_ROOTFS_COPY_PATHS); do \
	    cp -p $(MIPS_ROOTFS_SRC)/$$path $(MIPS_ROOTFS_STAGE)/$$path; \
	done
	touch $@

$(MIPS_ROOTFS_BUILD_MANIFEST): $(MIPS_ROOTFS_MANIFEST) $(MIPS_ROOTFS_STAMP)
	cp $(MIPS_ROOTFS_MANIFEST) $@
	find $(MIPS_ROOTFS_STAGE)/usr/include -mindepth 1 -type d | sort | while read path; do \
	    rel=$${path#$(MIPS_ROOTFS_STAGE)}; \
	    printf '\ndir %s\n' "$$rel" >> $@; \
	done
	find $(MIPS_ROOTFS_STAGE)/usr/include -type f ! -name '.gitignore' \
	    ! -name Makefile ! -name Makefile.install | sort | while read path; do \
	    rel=$${path#$(MIPS_ROOTFS_STAGE)}; \
	    printf '\nfile %s\n' "$$rel" >> $@; \
	done
	find $(MIPS_ROOTFS_STAGE)/usr/include -maxdepth 1 -type l ! -name pic32 | \
	    sort | while read path; do \
	    rel=$${path#$(MIPS_ROOTFS_STAGE)}; \
	    target=$$(readlink "$$path"); \
	    printf '\nsymlink %s\ntarget %s\n' "$$rel" "$$target" >> $@; \
	done

rootfs.img: $(FSUTIL) $(MIPS_ROOTFS_STAMP) $(MIPS_ROOTFS_BUILD_MANIFEST)
	rm -f $@
	$(FSUTIL) --new --endian=big --size=$(MIPS_ROOTFS_KBYTES) \
	    --manifest=$(MIPS_ROOTFS_BUILD_MANIFEST) $@ $(MIPS_ROOTFS_STAGE)
	$(FSUTIL) --check $@

rootfs.o: rootfs.img
	$(LD) -r -b binary -o $@ rootfs.img
	$(OBJCOPY) --rename-section .data=.romdisk,alloc,load,readonly,data,contents $@

ioconf.o: ioconf.c ${SYSTEM_DEP} | machine sys .deps
	${COMPILE_C}

${SYSTEM_OBJ} swapunix.o: | machine sys .deps

$(FSUTIL):
	$(MAKE) -C $(TOPSRC)/tools/fsutil

reconfig ioconf.c swapunix.c: Config ../../../tools/kconfig/kconfig | machine sys
	../../../tools/kconfig/kconfig Config
	$(MAKE) clean

machine:
	ln -s .. $@

sys:
	ln -s ../../include $@

clean clean-all:
	rm -rf *.o *.elf *.bin *.img *.dis *.nm a.out .deps machine sys \
	    vers.c rootfs.stage rootfs.generated.manifest

.deps:
	mkdir -p .deps

unix: unix.elf

unix.elf: ${SYSTEM_DEP} ${SYSTEM_OBJ} swapunix.o
	${SYSTEM_LD_HEAD}
	${SYSTEM_LD} swapunix.o
	${SYSTEM_LD_TAIL}

swapunix.o: swapunix.c ${SYSTEM_DEP}
	${COMPILE_C}

all: unix

exec_aout.o: $S/kernel/exec_aout.c ${SYSTEM_DEP}
	${COMPILE_C}

exec_conf.o: $S/kernel/exec_conf.c ${SYSTEM_DEP}
	${COMPILE_C}

exec_elf.o: $S/kernel/exec_elf.c ${SYSTEM_DEP}
	${COMPILE_C}

exec_script.o: $S/kernel/exec_script.c ${SYSTEM_DEP}
	${COMPILE_C}

exec_subr.o: $S/kernel/exec_subr.c ${SYSTEM_DEP}
	${COMPILE_C}

init_main.o: $S/kernel/init_main.c ${SYSTEM_DEP}
	${COMPILE_C}

init_sysent.o: $S/kernel/init_sysent.c ${SYSTEM_DEP}
	${COMPILE_C}

kern_clock.o: $S/kernel/kern_clock.c ${SYSTEM_DEP}
	${COMPILE_C}

kern_descrip.o: $S/kernel/kern_descrip.c ${SYSTEM_DEP}
	${COMPILE_C}

kern_exec.o: $S/kernel/kern_exec.c ${SYSTEM_DEP}
	${COMPILE_C}

kern_exit.o: $S/kernel/kern_exit.c ${SYSTEM_DEP}
	${COMPILE_C}

kern_fork.o: $S/kernel/kern_fork.c ${SYSTEM_DEP}
	${COMPILE_C}

kern_glob.o: $S/kernel/kern_glob.c ${SYSTEM_DEP}
	${COMPILE_C}

kern_mman.o: $S/kernel/kern_mman.c ${SYSTEM_DEP}
	${COMPILE_C}

kern_proc.o: $S/kernel/kern_proc.c ${SYSTEM_DEP}
	${COMPILE_C}

kern_prot.o: $S/kernel/kern_prot.c ${SYSTEM_DEP}
	${COMPILE_C}

kern_prot2.o: $S/kernel/kern_prot2.c ${SYSTEM_DEP}
	${COMPILE_C}

kern_resource.o: $S/kernel/kern_resource.c ${SYSTEM_DEP}
	${COMPILE_C}

kern_sig.o: $S/kernel/kern_sig.c ${SYSTEM_DEP}
	${COMPILE_C}

kern_sig2.o: $S/kernel/kern_sig2.c ${SYSTEM_DEP}
	${COMPILE_C}

kern_subr.o: $S/kernel/kern_subr.c ${SYSTEM_DEP}
	${COMPILE_C}

kern_synch.o: $S/kernel/kern_synch.c ${SYSTEM_DEP}
	${COMPILE_C}

kern_sysctl.o: $S/kernel/kern_sysctl.c ${SYSTEM_DEP}
	${COMPILE_C}

kern_time.o: $S/kernel/kern_time.c ${SYSTEM_DEP}
	${COMPILE_C}

subr_prf.o: $S/kernel/subr_prf.c ${SYSTEM_DEP}
	${COMPILE_C}

subr_rmap.o: $S/kernel/subr_rmap.c ${SYSTEM_DEP}
	${COMPILE_C}

sys_generic.o: $S/kernel/sys_generic.c ${SYSTEM_DEP}
	${COMPILE_C}

sys_inode.o: $S/kernel/sys_inode.c ${SYSTEM_DEP}
	${COMPILE_C}

sys_pipe.o: $S/kernel/sys_pipe.c ${SYSTEM_DEP}
	${COMPILE_C}

sys_process.o: $S/kernel/sys_process.c ${SYSTEM_DEP}
	${COMPILE_C}

syscalls.o: $S/kernel/syscalls.c ${SYSTEM_DEP}
	${COMPILE_C}

tty.o: $S/kernel/tty.c ${SYSTEM_DEP}
	${COMPILE_C}

tty_pty.o: $S/kernel/tty_pty.c ${SYSTEM_DEP}
	${COMPILE_C}

tty_subr.o: $S/kernel/tty_subr.c ${SYSTEM_DEP}
	${COMPILE_C}

tty_tty.o: $S/kernel/tty_tty.c ${SYSTEM_DEP}
	${COMPILE_C}

ufs_alloc.o: $S/kernel/ufs_alloc.c ${SYSTEM_DEP}
	${COMPILE_C}

ufs_bio.o: $S/kernel/ufs_bio.c ${SYSTEM_DEP}
	${COMPILE_C}

ufs_bmap.o: $S/kernel/ufs_bmap.c ${SYSTEM_DEP}
	${COMPILE_C}

ufs_dsort.o: $S/kernel/ufs_dsort.c ${SYSTEM_DEP}
	${COMPILE_C}

ufs_fio.o: $S/kernel/ufs_fio.c ${SYSTEM_DEP}
	${COMPILE_C}

ufs_inode.o: $S/kernel/ufs_inode.c ${SYSTEM_DEP}
	${COMPILE_C}

ufs_mount.o: $S/kernel/ufs_mount.c ${SYSTEM_DEP}
	${COMPILE_C}

ufs_namei.o: $S/kernel/ufs_namei.c ${SYSTEM_DEP}
	${COMPILE_C}

ufs_subr.o: $S/kernel/ufs_subr.c ${SYSTEM_DEP}
	${COMPILE_C}

ufs_syscalls.o: $S/kernel/ufs_syscalls.c ${SYSTEM_DEP}
	${COMPILE_C}

ufs_syscalls2.o: $S/kernel/ufs_syscalls2.c ${SYSTEM_DEP}
	${COMPILE_C}

vfs_vnops.o: $S/kernel/vfs_vnops.c ${SYSTEM_DEP}
	${COMPILE_C}

vm_sched.o: $S/kernel/vm_sched.c ${SYSTEM_DEP}
	${COMPILE_C}

vm_swap.o: $S/kernel/vm_swap.c ${SYSTEM_DEP}
	${COMPILE_C}

vm_swp.o: $S/kernel/vm_swp.c ${SYSTEM_DEP}
	${COMPILE_C}

startup.o: $S/mips/common/startup.S ${SYSTEM_DEP}
	${COMPILE_S}

exception_entry.o: $S/mips/common/exception_entry.S ${SYSTEM_DEP}
	${COMPILE_S}

exception.o: $S/mips/common/exception.c ${SYSTEM_DEP}
	${COMPILE_C}

fpu.o: $S/mips/common/fpu.S ${SYSTEM_DEP}
	${COMPILE_FPU_S}

cons.o: $S/mips/common/cons.c ${SYSTEM_DEP}
	${COMPILE_C}

signal.o: $S/mips/common/signal.c ${SYSTEM_DEP}
	${COMPILE_C}

sysctl.o: $S/mips/common/sysctl.c ${SYSTEM_DEP}
	${COMPILE_C}

libkern.o: $S/mips/common/libkern.c ${SYSTEM_DEP}
	${COMPILE_C}

stubs.o: $S/mips/common/stubs.c ${SYSTEM_DEP}
	${COMPILE_C}

uart.o: $S/mips/malta/uart.c ${SYSTEM_DEP}
	${COMPILE_C}

devsw.o: $S/mips/malta/devsw.c ${SYSTEM_DEP}
	${COMPILE_C}

clock.o: $S/mips/malta/clock.c ${SYSTEM_DEP}
	${COMPILE_C}

machdep.o: $S/mips/malta/machdep.c ${SYSTEM_DEP}
	${COMPILE_C}

romdisk.o: $S/mips/malta/romdisk.c ${SYSTEM_DEP}
	${COMPILE_C}

romfs_core.o: $S/mips/common/romfs_core.c ${SYSTEM_DEP}
	${COMPILE_C}

romfs_vfs.o: $S/mips/common/romfs_vfs.c ${SYSTEM_DEP}
	${COMPILE_C}

cartflash.o: $S/mips/malta/cartflash.c ${SYSTEM_DEP}
	${COMPILE_C}

ramswap.o: $S/mips/malta/ramswap.c ${SYSTEM_DEP}
	${COMPILE_C}

config.o: $S/mips/malta/config.c ${SYSTEM_DEP}
	${COMPILE_C}


-include .deps/*.dep
