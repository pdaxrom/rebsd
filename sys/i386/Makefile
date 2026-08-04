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
I386_CONSOLE_SRC = $(TOPSRC)/sys/console
KCONFIG_DIR      = $(TOPSRC)/tools/kconfig
KCONFIG          = $(OBJTOP)/tools/kconfig/kconfig
KCONFIG_SRCS     = $(KCONFIG_DIR)/Makefile $(KCONFIG_DIR)/config.h \
                   $(KCONFIG_DIR)/config.y $(KCONFIG_DIR)/lang.l \
                   $(KCONFIG_DIR)/main.c $(KCONFIG_DIR)/mkioconf.c \
                   $(KCONFIG_DIR)/mkmakefile.c $(KCONFIG_DIR)/mkswapconf.c
I386_DISK_SRC    = $(TOPSRC)/sys/disk
I386_FAT_SRC     = $(TOPSRC)/sys/fs/fat
I386_INPUT_SRC   = $(TOPSRC)/sys/input
I386_PCI_SRC     = $(TOPSRC)/sys/pci
I386_USB_SRC     = $(TOPSRC)/sys/usb
I386_RTC_SRC     = $(TOPSRC)/sys/rtc
I386_RUNTIME_SRC = $(TOPSRC)/src/libc/runtime
I386_VM_SRC      = $(TOPSRC)/sys/vm
I386_NET_SRC     = $(TOPSRC)/sys/net
I386_NETINET_SRC = $(TOPSRC)/sys/netinet
I386_GEN_INCLUDE = $(CURDIR)/include
I386_VM_INCLUDES = -I$(I386_GEN_INCLUDE) -I$(TOPSRC)/sys \
                   -I$(I386_SRC)/include
I386_INET_INCLUDES = -I$(TOPSRC)/include/sys -I$(I386_NET_SRC) \
                     -I$(I386_NETINET_SRC)

ifeq ($(BOARD),pc)
else
$(error Unsupported i386 BOARD=$(BOARD); expected pc)
endif

include $(TOPSRC)/target-i386.mk
include $(TOPSRC)/mk/rootfs-userland.mk

# Keep every kernel object tied to the complete header graph seen by GCC.
# Explicit source prerequisites below document ownership, while generated
# dependency files prevent mixed kernel ABIs after a common header changes.
I686_DEPFLAGS = -MT $@ -MMD -MP -MF .deps/$(@:.o=.d)
I686_CFLAGS  += $(I686_DEPFLAGS)
I686_ASFLAGS += $(I686_DEPFLAGS)

I386_ROOTFS_CMD_SUBDIRS = $(REBSD_ROOTFS_CMD_SUBDIRS) lsusb lspci
I386_ROOTFS_CMD_EXTRA_SUBDIRS = $(REBSD_ROOTFS_CMD_EXTRA_SUBDIRS) \
                               lsusb lspci

# The direct bring-up build carries the same service feature symbols that
# kconfig emits in PARAM for the declarative configuration.
I686_CPPFLAGS  += -DDMA_ENABLED -DDISK_ENABLED -DUSB_ENABLED \
                  -DUMASS_ENABLED -DUHUB_ENABLED -DUKBD_ENABLED \
                  -DUMS_ENABLED -DI8042_ENABLED \
                  -DFAT_ENABLED \
                  -DEHCI_ENABLED -DOHCI_ENABLED -DUHCI_ENABLED \
                  -DETHER_ENABLED -DETHER_NUNITS=1 \
                  -DRTL8169_ENABLED \
                  -DI386_RTC_ENABLED \
                  -DI386_FPU_ENABLED \
                  -DINET_ENABLED -DINET -DSUPERVISOR

I386_SSE ?= 0
ifeq ($(I386_SSE),1)
I686_CPPFLAGS += -DI386_SSE_ENABLED
endif

I686_INET_CFLAGS = $(filter-out -Wextra,$(I686_CFLAGS)) \
                   -Wno-comment -Wno-endif-labels -Wno-implicit-int \
                   -Wno-implicit-function-declaration \
                   -Wno-parentheses -Wno-return-mismatch \
                   -Wno-return-type -Wno-old-style-definition

QEMU_I386       ?= qemu-system-i386
QEMU_MACHINE    ?= pc-i440fx-9.2
QEMU_CPU        ?= pentium3
QEMU_MEMORY     ?= 64M
PYTHON          ?= python3
HOSTCC          ?= cc
FSUTIL_DIR       = $(TOPOBJ)/tools/fsutil
FSUTIL           = $(FSUTIL_DIR)/fsutil
I386_ROOTFS_STAGE = rootfs-stage
I386_ROOTFS_BASE = rootfs-base
I386_ROOTFS_BASE_STAMP = i386-rootfs-base.stamp
I386_ROOTFS_KBYTES ?= 16384
I386_ROOTFS_EPOCH = 1
I386_FAT_SMOKE_IMAGE = fat-smoke.img
I386_FAT32_SMOKE_IMAGE = fat32-oversize-smoke.img
I386_SWAP_SMOKE_IMAGE = swap-smoke.img
I386_HOST_MKFS_FAT = mkfs.fat.host
I386_FAT_SMOKE_TOOL = $(I386_TOOLS_SRC)/mkfat-smoke.py
I386_SWAP_SMOKE_TOOL = $(I386_TOOLS_SRC)/mkswap-smoke.py
I386_ROOTFS_BOARD_DIR = $(I386_PC_SRC)/rootfs
I386_ROOTFS_DEVICES = $(I386_PC_SRC)/rootfs.devices
I386_ROOTFS_MANIFEST = rootfs.generated.manifest
I386_ROOTFS_MANIFEST_TOOL = $(TOPSRC)/sys/mips/tools/rootfs-manifest.py
I386_ROOTFS_MAKEFILES = $(TOPSRC)/sys/i386/Makefile \
                        $(TOPSRC)/mk/rootfs-userland.mk
I386_INSTALL_USER_HEADERS = $(TOPSRC)/tools/build/install-user-headers.sh
I386_TERMCAP = $(TOPSRC)/src/libtermlib/termcap/termcap.small
I386_MAGIC_DB = $(TOPSRC)/src/libmagic/magic.mgc.little
I386_MAKEWHATIS_SED = $(TOPSRC)/src/man/makewhatis.sed
I386_MAN_SRCS = $(shell find $(TOPSRC)/src/man -type f \
                    \( -name '*.[0-9]' -o -name Makefile \) 2>/dev/null)
I386_BISON ?= $(if $(wildcard /opt/homebrew/opt/bison/bin/bison),/opt/homebrew/opt/bison/bin/bison,bison)
I386_USERLAND_BUILD = $(CURDIR)/i386-userland-build
I386_USERLAND_SRC = $(I386_USERLAND_BUILD)/src
I386_USERLAND_DESTDIR = $(I386_USERLAND_BUILD)/rootfs-install
I386_USERLAND_STAMP = i386-userland.gcc.elf.stamp
I386_USER_LDSCRIPT = i386-user.ld
I386_USER_LDSCRIPT_SRC = $(I386_SRC)/user/user.ld.S
I386_KERNEL_LDSCRIPT = i386-kernel.ld
I386_KERNEL_LDSCRIPT_SRC = $(I386_PC_SRC)/kernel.ld.S
I386_USER_NONE = __i386_none__
I386_USERLAND_INCLUDES = -I$(abspath $(I386_ROOTFS_BASE)/usr/include)
I386_USERLAND_CC = $(CC) $(I686_ARCH_FLAGS) \
                   -DTARGET_LITTLE_ENDIAN \
                   -ffreestanding -fno-builtin -fno-stack-protector \
                   -fno-pic -fno-pie -fno-asynchronous-unwind-tables \
                   -fno-unwind-tables -mno-sse -mno-sse2 \
                   -mlong-double-64 -nostdinc \
                   $(I386_USERLAND_INCLUDES) \
                   $(I686_FORMAT_WARN_FLAGS) -Werror
I386_USERLAND_LDFLAGS = --nmagic -z noexecstack \
                        -T$(abspath $(I386_USER_LDSCRIPT)) \
                        $(abspath $(I386_USERLAND_SRC)/crt0.o) \
                        -L$(abspath $(I386_USERLAND_SRC))
I386_USERLAND_SRCS = $(shell find $(TOPSRC)/src -type f \
                       \( -name '*.[chS]' -o -name Makefile \) 2>/dev/null)
I386_ROOTFS_COMMON_FILES = $(TOPSRC)/sys/mips/rootfs/etc/group \
                           $(TOPSRC)/sys/mips/rootfs/etc/hosts \
                           $(TOPSRC)/sys/mips/rootfs/etc/inittab \
                           $(TOPSRC)/sys/mips/rootfs/etc/inetd.conf \
                           $(TOPSRC)/sys/mips/rootfs/etc/passwd \
                           $(TOPSRC)/sys/mips/rootfs/etc/profile \
                           $(TOPSRC)/sys/mips/rootfs/etc/rc \
                           $(TOPSRC)/sys/mips/rootfs/etc/rc0.d/S99halt \
                           $(TOPSRC)/sys/mips/rootfs/etc/rc6.d/S99reboot \
                           $(TOPSRC)/sys/mips/rootfs/etc/services \
                           $(TOPSRC)/sys/mips/rootfs/root/.profile
I386_ROOTFS_MACHINE_HEADERS = $(shell find $(I386_SRC)/include/machine \
                               -type f -name '*.h' 2>/dev/null)
I386_ROOTFS_COMMON_HEADERS = $(shell find $(TOPSRC)/include \
                               $(TOPSRC)/sys/include $(TOPSRC)/sys/net \
                               $(TOPSRC)/sys/netinet \
                               \( -type f -name '*.h' -o -type l \) \
                               2>/dev/null)
I386_SRC_MAKE = $(MAKE) -C $(I386_USERLAND_SRC) \
                TARGET_PLATFORM=i386 \
                DESTDIR=$(abspath $(I386_USERLAND_DESTDIR)) \
                CC="$(I386_USERLAND_CC)" \
                AS="$(I386_USERLAND_CC) -x assembler-with-cpp -c" \
                LD="$(LD)" AR="$(AR)" RANLIB="$(RANLIB)" \
                NM="$(NM)" SIZE="$(SIZE)" OBJDUMP="$(OBJDUMP)" \
                ELF2AOUT=cp \
                LDFLAGS="$(I386_USERLAND_LDFLAGS)" \
                SRC_ONLY_LIBS="startup-i386 $(REBSD_ROOTFS_LIBS)" \
                LIBC_ONLY="$(REBSD_ROOTFS_LIBC_DIRS) i386" \
                SRC_ONLY_SUBDIR="$(REBSD_ROOTFS_SUBDIRS)" \
                CMD_ONLY_SUBDIR="$(I386_ROOTFS_CMD_SUBDIRS)" \
                CMD_ONLY_STD="$(REBSD_ROOTFS_CMD_STDS)" \
                CMD_ONLY_SCRIPT="$(REBSD_ROOTFS_CMD_SCRIPTS)" \
                CMD_ONLY_NSTD="$(REBSD_ROOTFS_CMD_NSTDS)" \
                CMD_ONLY_SETUID="$(I386_USER_NONE)" \
                CMD_ONLY_OPERATOR="$(REBSD_ROOTFS_CMD_OPERATORS)" \
                CMD_ONLY_KMEM="$(I386_USER_NONE)" \
                CMD_ONLY_TTY="$(I386_USER_NONE)" \
                CMD_EXTRA_SUBDIR="$(I386_ROOTFS_CMD_EXTRA_SUBDIRS)" \
                SMUX_SUBDIRS=retro \
                CMD_BUILD_STRIP=
I386_AWK_MAKE = $(MAKE) -C $(I386_USERLAND_SRC)/cmd/awk \
                TARGET_PLATFORM=i386 \
                DESTDIR=$(abspath $(I386_USERLAND_DESTDIR)) \
                CC="$(I386_USERLAND_CC)" \
                AS="$(I386_USERLAND_CC) -x assembler-with-cpp -c" \
                LD="$(LD)" AR="$(AR)" RANLIB="$(RANLIB)" \
                NM="$(NM)" SIZE="$(SIZE)" OBJDUMP="$(OBJDUMP)" \
                ELF2AOUT=cp LDFLAGS="$(I386_USERLAND_LDFLAGS)" \
                YACC="$(I386_BISON) -y"
I386_VERSION_GENERATOR = $(TOPSRC)/tools/build/gen-vers.py
I386_COMPILER_VERSION = $(shell $(CC) --version 2>/dev/null | sed -n '1p')
I386_GIT_REV = $(or $(shell git -C $(TOPSRC) rev-parse HEAD 2>/dev/null),unknown)
I386_GIT_BRANCH = $(or $(shell git -C $(TOPSRC) symbolic-ref --short -q HEAD 2>/dev/null),detached)
I386_GIT_DIRTY = $(shell test -n "$$(git -C $(TOPSRC) status --porcelain --untracked-files=normal 2>/dev/null)" && echo 1 || echo 0)

SETUP_SECTS      = 8
SETUP_BYTES      = 4608

VM_CORE_OBJS     = vm_map.o vm_object.o vm_shm.o vm_sysv_shm.o \
                   swap_linux.o \
                   vmspace.o vmspace_access.o vmspace_fault.o \
                   vmspace_fault_api.o vmspace_fault_validate.o
INET_OBJS        = sys_socket.o uipc_domain.o uipc_mbuf.o uipc_proto.o \
                   uipc_socket.o uipc_socket2.o uipc_syscalls.o \
                   af.o if.o if_ether.o if_loop.o raw_cb.o raw_usrreq.o route.o \
                   in.o in_pcb.o in_proto.o ip_icmp.o ip_input.o \
                   ip_output.o raw_ip.o tcp_debug.o tcp_input.o \
                   tcp_output.o tcp_subr.o tcp_timer.o tcp_usrreq.o \
                   udp_usrreq.o in_cksum.o net_glue.o netinit.o
I386_RUNTIME_OBJS = divdi3.o divmoddi4.o moddi3.o udivdi3.o \
                    udivmoddi4.o umoddi3.o
KERNEL_OBJS      = entry.o interrupt_entry.o context.o privilege.o fpu.o \
                   early_console.o vtconsole.o interrupt.o tss.o syscall.o \
                   signal.o user_return.o trap.o \
                   pic.o pit.o pci_core.o pciide.o i386_pci.o \
                   rtl8169_hw.o if_re.o \
                   ide.o memory.o paging.o \
                   libkern.o cons.o subr_dma.o clock_subr.o todr.o \
                   mc146818.o i386_rtc.o \
                   kbdmap.o ps2.o mouse.o i8042.o \
                   disk_subr.o disk.o romdisk.o ramcomp.o ramdisk.o i386_romdisk.o \
                   i386_ramdisk.o \
                   fat_subr.o fat_vfs.o \
                   usb_subr.o usb_core.o usb_service.o usb_task.o \
                   uhub.o ukbd_decode.o ukbd.o umsmap.o ums.o \
                   ehci.o ohci.o uhci.o \
                   umass_bbb.o umass.o i386_dma.o usb_pci.o \
                   devsw.o \
                   ufs_alloc.o ufs_bio.o ufs_bmap.o ufs_fio.o \
                   ufs_inode.o ufs_mount.o ufs_namei.o ufs_subr.o \
                   ufs_syscalls.o ufs_syscalls2.o \
                   sys_inode.o sys_generic.o sys_pipe.o sys_fifo.o vfs_vnops.o \
                   kern_descrip.o kern_shm.o kern_subr.o kern_time.o \
                   nullzero.o \
                   vm_vnode.o \
                   disk_bootstrap.o \
                   exec_aout.o exec_conf.o exec_elf.o exec_elf_loader.o \
                   exec_script.o exec_subr.o kern_exec.o init_process.o \
                   rootfs_image.o \
                   init_sysent.o kern_tables.o stubs.o kern_machdep.o \
                   kern_mman.o kern_prot.o \
                   kern_prot2.o kern_proc.o kern_fork.o \
                   kern_exit.o kern_resource.o kern_sig.o kern_sig2.o \
                   kern_sysctl.o kern_sysv_shm.o sys_process.o \
                   init_main.o kern_synch.o kern_clock.o subr_prf.o \
                   subr_msgbuf.o subr_rmap.o \
                   tty.o tty_subr.o vm_sched.o vm_swp.o scheduler.o vers.o \
                   copyio.o vm_machdep.o \
                   config.o ioconf.o swapunix.o sysctl.o \
                   vm_param.o \
                   vm_phys.o vm_page.o vm_phys_board.o pmap.o \
                   $(VM_CORE_OBJS) $(INET_OBJS) $(I386_RUNTIME_OBJS) \
                   vmspace_bootstrap.o boot_main.o

$(KERNEL_OBJS): $(I386_SRC)/include/machine/machparam.h \
	    $(I386_SRC)/include/machine/layout.h
$(KERNEL_OBJS) setup.o: | .deps

.deps:
	mkdir -p $@

-include $(KERNEL_OBJS:%.o=.deps/%.d) .deps/setup.d

.DEFAULT_GOAL := all

.PHONY: all kernel bios-image toolchain-check rootfs-smoke ide-smoke \
	fat-mount-smoke \
	ide-pio-smoke ide-dma-smoke ide-auto-smoke \
	ramdisk-smoke \
	bios-image-smoke ide-absent-smoke bios-ide-absent-smoke \
	usb-mass-storage-smoke bios-usb-mass-storage-smoke \
	usb-mass-storage-ide-absent-smoke \
	bios-usb-mass-storage-ide-absent-smoke \
	ohci-keyboard-smoke bios-ohci-keyboard-smoke \
	ohci-mouse-smoke bios-ohci-mouse-smoke \
	uhci-keyboard-smoke bios-uhci-keyboard-smoke \
	uhci-mouse-smoke bios-uhci-mouse-smoke \
	ps2-input-smoke ide-dma-ps2-input-smoke bios-ps2-input-smoke \
	uhci-mass-storage-smoke bios-uhci-mass-storage-smoke \
	uhci-mass-storage-ide-absent-smoke \
	bios-uhci-mass-storage-ide-absent-smoke \
	usb-combined-smoke bios-usb-combined-smoke \
	run run-serial debug boot-smoke init-halt-smoke bios-boot-smoke \
	boot-smoke-matrix clean

.PHONY: FORCE
FORCE:

all: toolchain-check kernel

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

fpu.o: $(I386_COMMON_SRC)/fpu.c $(I386_SRC)/include/fpu.h \
	    $(I386_SRC)/include/interrupt.h \
	    $(TOPSRC)/sys/include/user.h \
	    | $(I386_GEN_INCLUDE)/sys $(I386_GEN_INCLUDE)/machine
	$(CC) $(I686_CFLAGS) $(I686_CPPFLAGS) $(I386_VM_INCLUDES) \
	    -c $< -o $@

$(FSUTIL): $(wildcard $(TOPSRC)/tools/fsutil/*.c) \
	    $(TOPSRC)/tools/fsutil/bsdfs.h \
	    $(TOPSRC)/tools/fsutil/manifest.h
	$(MAKE) -C $(TOPSRC)/tools/fsutil OBJDIR=$(FSUTIL_DIR) \
	    CC="$(HOSTCC)" all

$(I386_USER_LDSCRIPT): $(I386_USER_LDSCRIPT_SRC) \
	    $(I386_SRC)/include/machine/layout.h \
	    $(TOPSRC)/sys/vm/vm_constants.h
	$(CC) -E -P -x c -I$(TOPSRC)/sys -I$(I386_SRC) $< -o $@

$(I386_KERNEL_LDSCRIPT): $(I386_KERNEL_LDSCRIPT_SRC) \
	    $(I386_SRC)/include/machine/layout.h
	$(CC) -E -P -x c -I$(I386_SRC) $< -o $@

$(I386_ROOTFS_BASE_STAMP): $(I386_ROOTFS_COMMON_FILES) \
	    $(I386_ROOTFS_COMMON_HEADERS) \
	    $(I386_ROOTFS_MACHINE_HEADERS) \
	    $(I386_INSTALL_USER_HEADERS) $(I386_TERMCAP) $(I386_MAGIC_DB) \
	    $(I386_ROOTFS_MAKEFILES) \
	    $(shell find $(I386_ROOTFS_BOARD_DIR) -type f 2>/dev/null)
	rm -rf $(I386_ROOTFS_BASE)
	mkdir -p $(I386_ROOTFS_BASE)/bin \
	    $(I386_ROOTFS_BASE)/dev \
	    $(I386_ROOTFS_BASE)/etc \
	    $(I386_ROOTFS_BASE)/etc/rc0.d \
	    $(I386_ROOTFS_BASE)/etc/rc6.d \
	    $(I386_ROOTFS_BASE)/lib \
	    $(I386_ROOTFS_BASE)/libexec \
	    $(I386_ROOTFS_BASE)/mnt \
	    $(I386_ROOTFS_BASE)/root \
	    $(I386_ROOTFS_BASE)/sbin \
	    $(I386_ROOTFS_BASE)/usr/include \
	    $(I386_ROOTFS_BASE)/usr/bin \
	    $(I386_ROOTFS_BASE)/usr/sbin \
	    $(I386_ROOTFS_BASE)/usr/libexec \
	    $(I386_ROOTFS_BASE)/usr/share/man/cat1 \
	    $(I386_ROOTFS_BASE)/usr/share/man/cat3 \
	    $(I386_ROOTFS_BASE)/usr/share/man/cat5 \
	    $(I386_ROOTFS_BASE)/usr/share/man/cat8 \
	    $(I386_ROOTFS_BASE)/usr/share/misc \
	    $(I386_ROOTFS_BASE)/var/config \
	    $(I386_ROOTFS_BASE)/var/log \
	    $(I386_ROOTFS_BASE)/var/run \
	    $(I386_ROOTFS_BASE)/var/tmp
	cp -p $(TOPSRC)/sys/mips/rootfs/etc/group \
	    $(TOPSRC)/sys/mips/rootfs/etc/hosts \
	    $(TOPSRC)/sys/mips/rootfs/etc/inittab \
	    $(TOPSRC)/sys/mips/rootfs/etc/inetd.conf \
	    $(TOPSRC)/sys/mips/rootfs/etc/passwd \
	    $(TOPSRC)/sys/mips/rootfs/etc/profile \
	    $(TOPSRC)/sys/mips/rootfs/etc/rc \
	    $(TOPSRC)/sys/mips/rootfs/etc/services \
	    $(I386_ROOTFS_BASE)/etc/
	cp -p $(TOPSRC)/sys/mips/rootfs/etc/rc0.d/S99halt \
	    $(I386_ROOTFS_BASE)/etc/rc0.d/S99halt
	cp -p $(TOPSRC)/sys/mips/rootfs/etc/rc6.d/S99reboot \
	    $(I386_ROOTFS_BASE)/etc/rc6.d/S99reboot
	cp -p $(TOPSRC)/sys/mips/rootfs/root/.profile \
	    $(I386_ROOTFS_BASE)/root/.profile
	cp -pR $(I386_ROOTFS_BOARD_DIR)/. $(I386_ROOTFS_BASE)/
	cp -p $(I386_TERMCAP) $(I386_ROOTFS_BASE)/etc/termcap
	cp -p $(I386_MAGIC_DB) \
	    $(I386_ROOTFS_BASE)/usr/share/misc/magic.mgc
	$(I386_INSTALL_USER_HEADERS) $(TOPSRC) \
	    $(I386_ROOTFS_BASE)/usr/include i386 \
	    $(I386_SRC)/include/machine yes ""
	rm -f $(I386_ROOTFS_BASE)/etc/resolv.conf
	ln -s ../var/run/resolv.conf $(I386_ROOTFS_BASE)/etc/resolv.conf
	ln -s var/tmp $(I386_ROOTFS_BASE)/tmp
	touch $@

$(I386_USERLAND_STAMP): $(I386_ROOTFS_BASE_STAMP) \
	    $(I386_USER_LDSCRIPT) $(I386_USERLAND_SRCS) \
	    $(TOPSRC)/target.mk $(TOPSRC)/target-i386.mk \
	    $(I386_MAKEWHATIS_SED) $(I386_MAN_SRCS) \
	    $(I386_ROOTFS_MAKEFILES)
	rm -rf $(I386_USERLAND_BUILD)
	mkdir -p $(I386_USERLAND_SRC)
	cp -pR $(TOPSRC)/src/. $(I386_USERLAND_SRC)/
	ln -s $(abspath $(TOPSRC))/.git $(I386_USERLAND_BUILD)/.git
	ln -s $(abspath $(TOPSRC))/include $(I386_USERLAND_BUILD)/include
	ln -s $(abspath $(TOPSRC))/lib $(I386_USERLAND_BUILD)/lib
	ln -s $(abspath $(TOPSRC))/sys $(I386_USERLAND_BUILD)/sys
	ln -s $(abspath $(TOPSRC))/tools $(I386_USERLAND_BUILD)/tools
	ln -s $(abspath $(TOPSRC))/target.mk $(I386_USERLAND_BUILD)/target.mk
	ln -s $(abspath $(TOPSRC))/target-i386.mk \
	    $(I386_USERLAND_BUILD)/target-i386.mk
	mkdir -p $(I386_USERLAND_DESTDIR)/bin \
	    $(I386_USERLAND_DESTDIR)/etc \
	    $(I386_USERLAND_DESTDIR)/lib \
	    $(I386_USERLAND_DESTDIR)/libexec \
	    $(I386_USERLAND_DESTDIR)/sbin \
	    $(I386_USERLAND_DESTDIR)/share/man/cat1 \
	    $(I386_USERLAND_DESTDIR)/share/man/cat3 \
	    $(I386_USERLAND_DESTDIR)/share/man/cat5 \
	    $(I386_USERLAND_DESTDIR)/share/man/cat8 \
	    $(I386_USERLAND_DESTDIR)/share/misc \
	    $(I386_USERLAND_DESTDIR)/usr/bin \
	    $(I386_USERLAND_DESTDIR)/usr/libexec \
	    $(I386_USERLAND_DESTDIR)/usr/sbin \
	    $(I386_USERLAND_DESTDIR)/usr/share/man/cat1 \
	    $(I386_USERLAND_DESTDIR)/usr/share/man/cat3 \
	    $(I386_USERLAND_DESTDIR)/usr/share/man/cat5 \
	    $(I386_USERLAND_DESTDIR)/usr/share/man/cat8
	$(I386_SRC_MAKE) clean
	$(I386_SRC_MAKE) all
	$(I386_AWK_MAKE) clean
	$(I386_AWK_MAKE) awk
	$(I386_SRC_MAKE) install
	$(I386_AWK_MAKE) install
	rm -rf $(I386_ROOTFS_STAGE)
	mkdir -p $(I386_ROOTFS_STAGE)
	cp -pR $(I386_ROOTFS_BASE)/. $(I386_ROOTFS_STAGE)/
	cp -pR $(I386_USERLAND_DESTDIR)/. $(I386_ROOTFS_STAGE)/
	mkdir -p $(I386_ROOTFS_STAGE)/usr/lib/ldscripts
	cp -p $(I386_USERLAND_SRC)/crt0.o \
	    $(I386_ROOTFS_STAGE)/usr/lib/crt0.o
	cp -p $(I386_USERLAND_SRC)/libc.a \
	    $(I386_ROOTFS_STAGE)/usr/lib/libc.a
	cp -p $(I386_USERLAND_SRC)/libm.a \
	    $(I386_ROOTFS_STAGE)/usr/lib/libm.a
	cp -p $(I386_USER_LDSCRIPT) \
	    $(I386_ROOTFS_STAGE)/usr/lib/ldscripts/elf32-i386.ld
	$(RANLIB) $(I386_ROOTFS_STAGE)/usr/lib/libc.a
	$(RANLIB) $(I386_ROOTFS_STAGE)/usr/lib/libm.a
	for bin in $(REBSD_ROOTFS_USR_BIN_FILES); do \
	    if [ -e $(I386_ROOTFS_STAGE)/bin/$$bin ]; then \
	        mv -f $(I386_ROOTFS_STAGE)/bin/$$bin \
	            $(I386_ROOTFS_STAGE)/usr/bin/$$bin; \
	    fi; \
	done
	for file in $(REBSD_ROOTFS_USR_LIBEXEC_FILES); do \
	    if [ -e $(I386_ROOTFS_STAGE)/libexec/$$file ]; then \
	        mv -f $(I386_ROOTFS_STAGE)/libexec/$$file \
	            $(I386_ROOTFS_STAGE)/usr/libexec/$$file; \
	    fi; \
	done
	if [ -d $(I386_ROOTFS_STAGE)/share ]; then \
	    cp -pR $(I386_ROOTFS_STAGE)/share/. \
	        $(I386_ROOTFS_STAGE)/usr/share/; \
	    rm -rf $(I386_ROOTFS_STAGE)/share; \
	fi
	for page in $(REBSD_ROOTFS_CAT1_PAGES); do \
	    GROFF_NO_SGR=1 $(MANROFF) $(TOPSRC)/src/man/man1/$$page.1 \
	        > $(I386_ROOTFS_STAGE)/usr/share/man/cat1/$$page.0; \
	done
	for spec in $(REBSD_ROOTFS_CMD_CAT1_SOURCES); do \
	    dir=$${spec%:*}; page=$${spec#*:}; \
	    GROFF_NO_SGR=1 $(MANROFF) $(TOPSRC)/src/cmd/$$dir/$$page.1 \
	        > $(I386_ROOTFS_STAGE)/usr/share/man/cat1/$$page.0; \
	done
	for alias in $(REBSD_ROOTFS_CAT1_ALIASES); do \
	    dst=$${alias%:*}; src=$${alias#*:}; \
	    cp $(I386_ROOTFS_STAGE)/usr/share/man/cat1/$$src.0 \
	        $(I386_ROOTFS_STAGE)/usr/share/man/cat1/$$dst.0; \
	done
	for page in $(REBSD_ROOTFS_CAT5_PAGES); do \
	    GROFF_NO_SGR=1 $(DOCROFF) $(TOPSRC)/src/man/man5/$$page.5 \
	        > $(I386_ROOTFS_STAGE)/usr/share/man/cat5/$$page.0; \
	done
	for page in $(REBSD_ROOTFS_CAT8_PAGES); do \
	    GROFF_NO_SGR=1 $(MANROFF) $(TOPSRC)/src/man/man8/$$page.8 \
	        > $(I386_ROOTFS_STAGE)/usr/share/man/cat8/$$page.0; \
	done
	for alias in $(REBSD_ROOTFS_CAT8_ALIASES); do \
	    dst=$${alias%:*}; src=$${alias#*:}; \
	    cp $(I386_ROOTFS_STAGE)/usr/share/man/cat8/$$src.0 \
	        $(I386_ROOTFS_STAGE)/usr/share/man/cat8/$$dst.0; \
	done
	rm -f $(I386_ROOTFS_STAGE)/usr/share/man/whatis.tmp
	touch $(I386_ROOTFS_STAGE)/usr/share/man/whatis.tmp
	for file in `find $(I386_ROOTFS_STAGE)/usr/share/man/cat* \
	    -type f -name '*.0' -print`; do \
	    sed -n -f $(I386_MAKEWHATIS_SED) $$file \
	        >> $(I386_ROOTFS_STAGE)/usr/share/man/whatis.tmp; \
	done
	sort -u $(I386_ROOTFS_STAGE)/usr/share/man/whatis.tmp \
	    > $(I386_ROOTFS_STAGE)/usr/share/man/whatis
	rm -f $(I386_ROOTFS_STAGE)/usr/share/man/whatis.tmp
	touch $@

$(I386_ROOTFS_STAGE)/.userland: $(I386_USERLAND_STAMP)
	test -x $(I386_ROOTFS_STAGE)/sbin/init
	test -L $(I386_ROOTFS_STAGE)/etc/init
	test -L $(I386_ROOTFS_STAGE)/etc/telinit
	for bin in $(REBSD_ROOTFS_USR_SBIN_FILES); do \
	    test -x $(I386_ROOTFS_STAGE)/usr/sbin/$$bin; \
	    test ! -e $(I386_ROOTFS_STAGE)/sbin/$$bin; \
	done
	test -x $(I386_ROOTFS_STAGE)/bin/sh
	test -x $(I386_ROOTFS_STAGE)/libexec/getty
	test -x $(I386_ROOTFS_STAGE)/bin/login
	test -x $(I386_ROOTFS_STAGE)/bin/ls
	touch $@

$(I386_ROOTFS_MANIFEST): $(I386_ROOTFS_STAGE)/.userland \
	    $(I386_ROOTFS_MANIFEST_TOOL) $(I386_ROOTFS_DEVICES) \
	    $(I386_ROOTFS_MAKEFILES)
	$(PYTHON) $(I386_ROOTFS_MANIFEST_TOOL) \
	    --stage $(I386_ROOTFS_STAGE) --output $@ \
	    --devices $(I386_ROOTFS_DEVICES)

rootfs.img: $(FSUTIL) $(I386_ROOTFS_MANIFEST) \
	    $(I386_ROOTFS_STAGE)/.userland
	rm -f $@
	SOURCE_DATE_EPOCH=$(I386_ROOTFS_EPOCH) $(FSUTIL) --new \
	    --endian=little --size=$(I386_ROOTFS_KBYTES) \
	    --manifest=$(I386_ROOTFS_MANIFEST) \
	    $@ $(I386_ROOTFS_STAGE)
	$(FSUTIL) --check $@

rootfs_image.o: rootfs.img
	$(OBJCOPY) -I binary -O elf32-i386 -B i386 \
	    --rename-section \
	    .data=.romdisk,alloc,load,readonly,data,contents \
	    $< $@

rootfs-smoke: rootfs.img
	rm -f rootfs.check.img
	SOURCE_DATE_EPOCH=$(I386_ROOTFS_EPOCH) $(FSUTIL) --new \
	    --endian=little --size=$(I386_ROOTFS_KBYTES) \
	    --manifest=$(I386_ROOTFS_MANIFEST) \
	    rootfs.check.img $(I386_ROOTFS_STAGE)
	$(FSUTIL) --check rootfs.check.img
	cmp rootfs.img rootfs.check.img
	rm -f rootfs.check.img

$(I386_HOST_MKFS_FAT): $(TOPSRC)/src/cmd/mkfs.fat/main.c
	$(HOSTCC) -DMKFS_FAT_HOST -std=c89 -Wall -Wextra -Werror \
	    $< -o $@

$(I386_FAT_SMOKE_IMAGE): $(I386_HOST_MKFS_FAT) $(I386_FAT_SMOKE_TOOL)
	rm -f $@
	$(PYTHON) $(I386_FAT_SMOKE_TOOL) \
	    --mkfs ./$(I386_HOST_MKFS_FAT) --output $@

$(I386_FAT32_SMOKE_IMAGE): $(I386_HOST_MKFS_FAT) $(I386_FAT_SMOKE_TOOL)
	rm -f $@
	$(PYTHON) $(I386_FAT_SMOKE_TOOL) \
	    --mkfs ./$(I386_HOST_MKFS_FAT) --output $@ --fat 32 \
	    --declared-extra-sectors 3576

$(I386_SWAP_SMOKE_IMAGE): $(I386_SWAP_SMOKE_TOOL)
	rm -f $@
	$(PYTHON) $(I386_SWAP_SMOKE_TOOL) --output $@

early_console.o: $(I386_PC_SRC)/early_console.c \
	    $(I386_SRC)/include/boot.h $(I386_SRC)/include/interrupt.h \
	    $(I386_SRC)/include/io.h \
	    $(I386_SRC)/include/machine/console.h \
	    $(TOPSRC)/sys/include/reboot.h $(TOPSRC)/sys/include/tty.h \
	    $(TOPSRC)/sys/include/types.h \
	    | $(I386_GEN_INCLUDE)/sys $(I386_GEN_INCLUDE)/machine
	$(CC) $(I686_CFLAGS) $(I686_CPPFLAGS) $(I386_VM_INCLUDES) \
	    -c $< -o $@

vtconsole.o: $(I386_CONSOLE_SRC)/vtconsole.c \
	    $(I386_CONSOLE_SRC)/vtconsole.h
	$(CC) $(I686_CFLAGS) $(I686_CPPFLAGS) $(I386_VM_INCLUDES) \
	    -c $< -o $@

interrupt.o: $(I386_PC_SRC)/interrupt.c \
	    $(I386_SRC)/include/boot.h $(I386_SRC)/include/fpu.h \
	    $(I386_SRC)/include/interrupt.h \
	    $(I386_SRC)/include/syscall.h \
	    $(I386_SRC)/include/trap.h \
	    $(I386_SRC)/include/vmspace_bootstrap.h \
	    $(I386_SRC)/include/machine/machparam.h
	$(CC) $(I686_CFLAGS) $(I686_CPPFLAGS) -I$(I386_SRC)/include \
	    -c $< -o $@

tss.o: $(I386_PC_SRC)/tss.c $(I386_SRC)/include/boot.h \
	    $(I386_SRC)/include/interrupt.h $(I386_SRC)/include/tss.h
	$(CC) $(I686_CFLAGS) $(I686_CPPFLAGS) -I$(I386_SRC)/include \
	    -c $< -o $@

syscall.o: $(I386_COMMON_SRC)/syscall.c \
	    $(I386_SRC)/include/interrupt.h $(I386_SRC)/include/syscall.h \
	    $(TOPSRC)/sys/include/systm.h $(TOPSRC)/sys/include/user.h \
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

user_return.o: $(I386_COMMON_SRC)/user_return.c \
	    $(I386_SRC)/include/fpu.h $(I386_SRC)/include/interrupt.h \
	    $(TOPSRC)/sys/include/signalvar.h \
	    $(TOPSRC)/sys/include/systm.h $(TOPSRC)/sys/include/user.h \
	    | $(I386_GEN_INCLUDE)/sys $(I386_GEN_INCLUDE)/machine
	$(CC) $(I686_CFLAGS) $(I686_CPPFLAGS) $(I386_VM_INCLUDES) \
	    -c $< -o $@

trap.o: $(I386_COMMON_SRC)/trap.c \
	    $(I386_SRC)/include/interrupt.h $(I386_SRC)/include/trap.h \
	    $(TOPSRC)/sys/include/signal.h $(TOPSRC)/sys/include/user.h \
	    | $(I386_GEN_INCLUDE)/sys $(I386_GEN_INCLUDE)/machine
	$(CC) $(I686_CFLAGS) $(I686_CPPFLAGS) $(I386_VM_INCLUDES) \
	    -c $< -o $@

pic.o: $(I386_PC_SRC)/pic.c $(I386_SRC)/include/interrupt.h \
	    $(I386_SRC)/include/io.h
	$(CC) $(I686_CFLAGS) $(I686_CPPFLAGS) -I$(I386_SRC)/include \
	    -c $< -o $@

pit.o: $(I386_PC_SRC)/pit.c $(I386_SRC)/include/interrupt.h \
	    $(I386_SRC)/include/io.h \
	    $(I386_SRC)/include/machine/machparam.h \
	    $(TOPSRC)/sys/include/param.h $(TOPSRC)/sys/include/proc.h \
	    $(TOPSRC)/sys/include/resource.h $(TOPSRC)/sys/include/time.h \
	    | $(I386_GEN_INCLUDE)/sys $(I386_GEN_INCLUDE)/machine
	$(CC) $(I686_CFLAGS) $(I686_CPPFLAGS) $(I386_VM_INCLUDES) \
	    -c $< -o $@

pci_core.o: $(I386_PCI_SRC)/pci.c $(I386_PCI_SRC)/pci.h \
	    $(TOPSRC)/sys/include/hw_inventory.h \
	    $(TOPSRC)/sys/include/hw_inventory_provider.h \
	    | $(I386_GEN_INCLUDE)/sys $(I386_GEN_INCLUDE)/machine
	$(CC) $(I686_CFLAGS) $(I686_CPPFLAGS) $(I386_VM_INCLUDES) \
	    -c $< -o $@

pciide.o: $(I386_PCI_SRC)/pciide.c $(I386_PCI_SRC)/pciide.h \
	    $(I386_PCI_SRC)/pci.h $(I386_DISK_SRC)/disk.h \
	    $(TOPSRC)/sys/include/dma.h $(TOPSRC)/sys/include/errno.h \
	    | $(I386_GEN_INCLUDE)/sys $(I386_GEN_INCLUDE)/machine
	$(CC) $(I686_CFLAGS) $(I686_CPPFLAGS) $(I386_VM_INCLUDES) \
	    -c $< -o $@

i386_pci.o: $(I386_PC_SRC)/pci.c $(I386_SRC)/include/boot.h \
	    $(I386_SRC)/include/io.h $(I386_SRC)/include/pci.h \
	    $(I386_PCI_SRC)/pci.h $(TOPSRC)/sys/vm/pmap.h \
	    | $(I386_GEN_INCLUDE)/sys $(I386_GEN_INCLUDE)/machine
	$(CC) $(I686_CFLAGS) $(I686_CPPFLAGS) $(I386_VM_INCLUDES) \
	    -c $< -o $@

rtl8169_hw.o: $(I386_PCI_SRC)/rtl8169.c $(I386_PCI_SRC)/rtl8169.h \
	    $(I386_PCI_SRC)/pci.h $(TOPSRC)/sys/include/dma.h \
	    | $(I386_GEN_INCLUDE)/sys $(I386_GEN_INCLUDE)/machine
	$(CC) $(I686_CFLAGS) $(I686_CPPFLAGS) $(I386_VM_INCLUDES) \
	    -c $< -o $@

if_re.o: $(I386_PCI_SRC)/if_re.c $(I386_PCI_SRC)/rtl8169.h \
	    $(I386_PCI_SRC)/pci.h $(I386_NETINET_SRC)/if_ether.h \
	    | $(I386_GEN_INCLUDE)/sys $(I386_GEN_INCLUDE)/machine
	$(CC) $(I686_INET_CFLAGS) $(I686_CPPFLAGS) $(I386_VM_INCLUDES) \
	    $(I386_INET_INCLUDES) -c $< -o $@

subr_dma.o: $(TOPSRC)/sys/kernel/subr_dma.c \
	    $(TOPSRC)/sys/include/dma.h $(TOPSRC)/sys/include/errno.h \
	    | $(I386_GEN_INCLUDE)/sys $(I386_GEN_INCLUDE)/machine
	$(CC) $(I686_CFLAGS) $(I686_CPPFLAGS) $(I386_VM_INCLUDES) \
	    -c $< -o $@

cons.o: $(TOPSRC)/sys/kernel/cons.c \
	    $(TOPSRC)/sys/include/conf.h $(TOPSRC)/sys/include/errno.h \
	    $(TOPSRC)/sys/include/ioctl.h $(TOPSRC)/sys/include/tty.h \
	    $(TOPSRC)/sys/include/uio.h \
	    $(I386_SRC)/include/machine/console.h \
	    | $(I386_GEN_INCLUDE)/sys $(I386_GEN_INCLUDE)/machine
	$(CC) $(I686_CFLAGS) $(I686_CPPFLAGS) $(I386_VM_INCLUDES) \
	    -c $< -o $@

usb_subr.o usb_core.o usb_service.o usb_task.o uhub.o ukbd_decode.o ukbd.o \
umsmap.o ums.o ehci.o ohci.o uhci.o umass_bbb.o umass.o: %.o: \
	    $(I386_USB_SRC)/%.c \
	    | $(I386_GEN_INCLUDE)/sys $(I386_GEN_INCLUDE)/machine
	$(CC) $(I686_CFLAGS) $(I686_CPPFLAGS) $(I386_VM_INCLUDES) \
	    -c $< -o $@

sys_socket.o uipc_domain.o uipc_mbuf.o uipc_proto.o uipc_socket.o \
uipc_socket2.o uipc_syscalls.o: %.o: $(TOPSRC)/sys/kernel/%.c \
	    | $(I386_GEN_INCLUDE)/sys $(I386_GEN_INCLUDE)/machine
	$(CC) $(I686_INET_CFLAGS) $(I686_CPPFLAGS) $(I386_VM_INCLUDES) \
	    $(I386_INET_INCLUDES) -c $< -o $@

af.o if.o if_loop.o raw_cb.o raw_usrreq.o route.o in_cksum.o net_glue.o \
netinit.o: %.o: $(I386_NET_SRC)/%.c \
	    | $(I386_GEN_INCLUDE)/sys $(I386_GEN_INCLUDE)/machine
	$(CC) $(I686_INET_CFLAGS) $(I686_CPPFLAGS) $(I386_VM_INCLUDES) \
	    $(I386_INET_INCLUDES) -c $< -o $@

if_ether.o in.o in_pcb.o in_proto.o ip_icmp.o ip_input.o ip_output.o raw_ip.o \
tcp_debug.o tcp_input.o tcp_output.o tcp_subr.o tcp_timer.o tcp_usrreq.o \
udp_usrreq.o: %.o: $(I386_NETINET_SRC)/%.c \
	    | $(I386_GEN_INCLUDE)/sys $(I386_GEN_INCLUDE)/machine
	$(CC) $(I686_INET_CFLAGS) $(I686_CPPFLAGS) $(I386_VM_INCLUDES) \
	    $(I386_INET_INCLUDES) -c $< -o $@

kbdmap.o ps2.o mouse.o: %.o: $(I386_INPUT_SRC)/%.c \
	    | $(I386_GEN_INCLUDE)/sys $(I386_GEN_INCLUDE)/machine
	$(CC) $(I686_CFLAGS) $(I686_CPPFLAGS) $(I386_VM_INCLUDES) \
	    -c $< -o $@

$(I386_RUNTIME_OBJS): %.o: $(I386_RUNTIME_SRC)/%.c \
	    $(I386_RUNTIME_SRC)/int_lib.h $(I386_RUNTIME_SRC)/int_types.h \
	    $(I386_RUNTIME_SRC)/int_util.h $(I386_RUNTIME_SRC)/int_div_impl.inc \
	    | $(I386_GEN_INCLUDE)/sys $(I386_GEN_INCLUDE)/machine
	$(CC) $(I686_CFLAGS) $(I686_CPPFLAGS) $(I386_VM_INCLUDES) \
	    -I$(I386_RUNTIME_SRC) -c $< -o $@

i8042.o: $(I386_PC_SRC)/i8042.c \
	    $(I386_SRC)/include/i8042.h \
	    $(I386_SRC)/include/interrupt.h $(I386_SRC)/include/io.h \
	    $(I386_INPUT_SRC)/kbd.h $(I386_INPUT_SRC)/mousevar.h \
	    $(I386_INPUT_SRC)/ps2.h \
	    | $(I386_GEN_INCLUDE)/sys $(I386_GEN_INCLUDE)/machine
	$(CC) $(I686_CFLAGS) $(I686_CPPFLAGS) $(I386_VM_INCLUDES) \
	    -c $< -o $@

i386_dma.o: $(I386_PC_SRC)/dma.c \
	    $(TOPSRC)/sys/include/dma.h $(TOPSRC)/sys/include/errno.h \
	    | $(I386_GEN_INCLUDE)/sys $(I386_GEN_INCLUDE)/machine
	$(CC) $(I686_CFLAGS) $(I686_CPPFLAGS) $(I386_VM_INCLUDES) \
	    -c $< -o $@

usb_pci.o: $(I386_PC_SRC)/usb_pci.c \
	    $(I386_SRC)/include/boot.h \
	    $(I386_SRC)/include/interrupt.h \
	    $(I386_SRC)/include/pci.h \
	    $(I386_PCI_SRC)/pci.h \
	    $(I386_SRC)/include/usb_pci.h \
	    $(I386_USB_SRC)/ehcivar.h $(I386_USB_SRC)/ohcivar.h \
	    $(I386_USB_SRC)/uhcivar.h \
	    $(I386_USB_SRC)/uhub.h $(I386_USB_SRC)/ukbd.h \
	    $(I386_USB_SRC)/umass.h $(I386_USB_SRC)/ums.h \
	    $(I386_USB_SRC)/usbvar.h \
	    $(TOPSRC)/sys/vm/pmap.h \
	    | $(I386_GEN_INCLUDE)/sys $(I386_GEN_INCLUDE)/machine
	$(CC) $(I686_CFLAGS) $(I686_CPPFLAGS) $(I386_VM_INCLUDES) \
	    -c $< -o $@

ide.o: $(I386_PC_SRC)/ide.c $(I386_SRC)/include/boot.h \
	    $(I386_SRC)/include/ide.h $(I386_SRC)/include/interrupt.h \
	    $(I386_SRC)/include/pci.h $(I386_PCI_SRC)/pciide.h \
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
	    -DDISK_WRITE_CACHE_SLOTS=1u \
	    -DDISK_WRITE_BACK_MAX_SECTORS=32u \
	    -c $< -o $@

fat_subr.o: $(I386_FAT_SRC)/fat_subr.c $(I386_FAT_SRC)/fat.h \
	    | $(I386_GEN_INCLUDE)/sys $(I386_GEN_INCLUDE)/machine
	$(CC) $(I686_CFLAGS) $(I686_CPPFLAGS) $(I386_VM_INCLUDES) \
	    -c $< -o $@

fat_vfs.o: $(I386_FAT_SRC)/fat_vfs.c $(I386_FAT_SRC)/fat.h \
	    | $(I386_GEN_INCLUDE)/sys $(I386_GEN_INCLUDE)/machine
	$(CC) $(I686_CFLAGS) $(I686_CPPFLAGS) $(I386_VM_INCLUDES) \
	    -c $< -o $@

romdisk.o: $(I386_DISK_SRC)/romdisk.c $(I386_DISK_SRC)/romdisk.h \
	    $(TOPSRC)/sys/include/buf.h $(TOPSRC)/sys/include/disk.h \
	    $(TOPSRC)/sys/include/errno.h $(TOPSRC)/sys/include/systm.h \
	    $(TOPSRC)/sys/include/types.h \
	    | $(I386_GEN_INCLUDE)/sys $(I386_GEN_INCLUDE)/machine
	$(CC) $(I686_CFLAGS) $(I686_CPPFLAGS) $(I386_VM_INCLUDES) \
	    -c $< -o $@

ramdisk.o: $(I386_DISK_SRC)/ramdisk.c $(I386_DISK_SRC)/ramdisk.h \
	    $(I386_DISK_SRC)/ramcomp.h \
	    $(TOPSRC)/sys/include/buf.h $(TOPSRC)/sys/include/disk.h \
	    $(TOPSRC)/sys/include/errno.h $(TOPSRC)/sys/include/systm.h \
	    $(TOPSRC)/sys/include/types.h \
	    | $(I386_GEN_INCLUDE)/sys $(I386_GEN_INCLUDE)/machine
	$(CC) $(I686_CFLAGS) $(I686_CPPFLAGS) $(I386_VM_INCLUDES) \
	    -c $< -o $@

ramcomp.o: $(I386_DISK_SRC)/ramcomp.c $(I386_DISK_SRC)/ramcomp.h \
	    $(TOPSRC)/sys/include/errno.h $(TOPSRC)/sys/include/types.h \
	    | $(I386_GEN_INCLUDE)/sys $(I386_GEN_INCLUDE)/machine
	$(CC) $(I686_CFLAGS) $(I686_CPPFLAGS) $(I386_VM_INCLUDES) \
	    -c $< -o $@

i386_romdisk.o: $(I386_PC_SRC)/romdisk.c \
	    $(I386_SRC)/include/romdisk.h $(I386_DISK_SRC)/romdisk.h \
	    | $(I386_GEN_INCLUDE)/sys $(I386_GEN_INCLUDE)/machine
	$(CC) $(I686_CFLAGS) $(I686_CPPFLAGS) $(I386_VM_INCLUDES) \
	    -c $< -o $@

i386_ramdisk.o: $(I386_PC_SRC)/ramdisk.c \
	    $(I386_SRC)/include/ramdisk.h $(I386_DISK_SRC)/ramdisk.h \
	    $(TOPSRC)/sys/include/buf.h \
	    | $(I386_GEN_INCLUDE)/sys $(I386_GEN_INCLUDE)/machine
	$(CC) $(I686_CFLAGS) $(I686_CPPFLAGS) $(I386_VM_INCLUDES) \
	    -c $< -o $@

devsw.o: $(I386_PC_SRC)/devsw.c $(I386_DISK_SRC)/disk.h \
	    $(I386_SRC)/include/ramdisk.h $(I386_SRC)/include/romdisk.h \
	    $(TOPSRC)/sys/include/buf.h $(TOPSRC)/sys/include/conf.h \
	    $(TOPSRC)/sys/include/errno.h $(TOPSRC)/sys/include/inode.h \
	    $(TOPSRC)/sys/include/systm.h $(TOPSRC)/sys/include/tty.h \
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

clock_subr.o: $(TOPSRC)/sys/kernel/clock_subr.c \
	    $(TOPSRC)/sys/include/todr.h \
	    | $(I386_GEN_INCLUDE)/sys $(I386_GEN_INCLUDE)/machine
	$(CC) $(I686_CFLAGS) $(I686_CPPFLAGS) $(I386_VM_INCLUDES) \
	    -c $< -o $@

todr.o: $(TOPSRC)/sys/kernel/todr.c $(TOPSRC)/sys/include/todr.h \
	    | $(I386_GEN_INCLUDE)/sys $(I386_GEN_INCLUDE)/machine
	$(CC) $(I686_CFLAGS) $(I686_CPPFLAGS) $(I386_VM_INCLUDES) \
	    -c $< -o $@

mc146818.o: $(I386_RTC_SRC)/mc146818.c \
	    $(I386_RTC_SRC)/mc146818.h $(TOPSRC)/sys/include/todr.h \
	    | $(I386_GEN_INCLUDE)/sys $(I386_GEN_INCLUDE)/machine
	$(CC) $(I686_CFLAGS) $(I686_CPPFLAGS) $(I386_VM_INCLUDES) \
	    -c $< -o $@

i386_rtc.o: $(I386_PC_SRC)/rtc.c $(I386_RTC_SRC)/mc146818.h \
	    $(I386_SRC)/include/io.h $(TOPSRC)/sys/include/todr.h \
	    | $(I386_GEN_INCLUDE)/sys $(I386_GEN_INCLUDE)/machine
	$(CC) $(I686_CFLAGS) $(I686_CPPFLAGS) $(I386_VM_INCLUDES) \
	    -c $< -o $@

nullzero.o: $(TOPSRC)/sys/kernel/nullzero.c \
	    $(TOPSRC)/sys/include/memdev.h $(TOPSRC)/sys/include/uio.h \
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

sys_fifo.o: $(TOPSRC)/sys/kernel/sys_fifo.c \
	    | $(I386_GEN_INCLUDE)/sys $(I386_GEN_INCLUDE)/machine
	$(CC) $(I686_CFLAGS) $(I686_CPPFLAGS) $(I386_VM_INCLUDES) \
	    -c $< -o $@

ufs_syscalls.o: $(TOPSRC)/sys/kernel/ufs_syscalls.c \
	    | $(I386_GEN_INCLUDE)/sys $(I386_GEN_INCLUDE)/machine
	$(CC) $(I686_CFLAGS) $(I686_CPPFLAGS) $(I386_VM_INCLUDES) \
	    -c $< -o $@

ufs_syscalls2.o: $(TOPSRC)/sys/kernel/ufs_syscalls2.c $(I386_SRC)/Makefile \
	    | $(I386_GEN_INCLUDE)/sys $(I386_GEN_INCLUDE)/machine
	$(CC) $(I686_CFLAGS) $(I686_CPPFLAGS) $(I386_VM_INCLUDES) \
	    -c $< -o $@

vfs_vnops.o: $(TOPSRC)/sys/kernel/vfs_vnops.c \
	    | $(I386_GEN_INCLUDE)/sys $(I386_GEN_INCLUDE)/machine
	$(CC) $(I686_CFLAGS) $(I686_CPPFLAGS) $(I386_VM_INCLUDES) \
	    -c $< -o $@

disk_bootstrap.o: $(I386_PC_SRC)/disk_bootstrap.c \
	    $(I386_SRC)/include/disk_bootstrap.h \
	    $(I386_SRC)/include/ide.h $(I386_DISK_SRC)/disk.h \
	    $(I386_PCI_SRC)/pciide.h \
	    $(TOPSRC)/sys/include/systm.h \
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

init_main.o: $(TOPSRC)/sys/kernel/init_main.c \
	    | $(I386_GEN_INCLUDE)/sys $(I386_GEN_INCLUDE)/machine
	$(CC) $(I686_CFLAGS) $(I686_CPPFLAGS) $(I386_VM_INCLUDES) \
	    -c $< -o $@

exec_aout.o exec_conf.o exec_elf.o exec_script.o kern_exec.o \
kern_tables.o kern_machdep.o kern_mman.o kern_prot2.o kern_sysctl.o \
kern_sysv_shm.o: %.o: \
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

vm_swp.o: $(TOPSRC)/sys/kernel/vm_swp.c \
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

subr_msgbuf.o: $(TOPSRC)/sys/kernel/subr_msgbuf.c \
	    $(TOPSRC)/sys/include/msgbuf.h \
	    | $(I386_GEN_INCLUDE)/sys $(I386_GEN_INCLUDE)/machine
	$(CC) $(I686_CFLAGS) $(I686_CPPFLAGS) $(I386_VM_INCLUDES) \
	    -c $< -o $@

subr_rmap.o: $(TOPSRC)/sys/kernel/subr_rmap.c \
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

config.o: $(I386_PC_SRC)/config.c \
	    $(I386_SRC)/include/disk_bootstrap.h \
	    $(I386_SRC)/include/ide.h $(I386_SRC)/include/pci.h \
	    $(I386_SRC)/include/ramdisk.h \
	    $(I386_SRC)/include/usb_pci.h \
	    $(TOPSRC)/sys/include/proc.h $(TOPSRC)/sys/include/time.h \
	    $(TOPSRC)/sys/include/resource.h $(TOPSRC)/sys/include/kernel.h \
	    | $(I386_GEN_INCLUDE)/sys $(I386_GEN_INCLUDE)/machine
	$(CC) $(I686_CFLAGS) $(I686_CPPFLAGS) $(I386_VM_INCLUDES) \
	    -c $< -o $@

$(KCONFIG): $(KCONFIG_SRCS)
	$(MAKE) -C $(KCONFIG_DIR)

i386-kconfig.stamp: $(I386_PC_SRC)/Config \
	    $(I386_SRC)/devices.kconf $(KCONFIG)
	rm -f ioconf.c swapunix.c
	$(KCONFIG) -i $(I386_PC_SRC)/Config
	test -f ioconf.c
	test -f swapunix.c
	touch $@

ioconf.c swapunix.c: i386-kconfig.stamp

ioconf.o: ioconf.c i386-kconfig.stamp \
	    | $(I386_GEN_INCLUDE)/sys $(I386_GEN_INCLUDE)/machine
	$(CC) $(I686_CFLAGS) $(I686_CPPFLAGS) $(I386_VM_INCLUDES) \
	    -c $< -o $@

swapunix.o: swapunix.c i386-kconfig.stamp \
	    | $(I386_GEN_INCLUDE)/sys $(I386_GEN_INCLUDE)/machine
	$(CC) $(I686_CFLAGS) $(I686_CPPFLAGS) $(I386_VM_INCLUDES) \
	    -c $< -o $@

sysctl.o: $(I386_PC_SRC)/sysctl.c \
	    $(I386_SRC)/include/machine/cpu.h \
	    $(TOPSRC)/sys/include/sysctl.h $(TOPSRC)/sys/include/systm.h \
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
	    $(I386_SRC)/include/boot.h $(I386_SRC)/include/fpu.h \
	    $(I386_SRC)/include/interrupt.h \
	    $(I386_SRC)/include/memory.h $(I386_SRC)/include/paging.h \
	    $(I386_SRC)/include/syscall.h \
	    $(I386_SRC)/include/tss.h \
	    $(I386_SRC)/include/machine/console.h \
	    $(I386_SRC)/include/machine/machparam.h \
	    $(TOPSRC)/sys/include/param.h \
	    | $(I386_GEN_INCLUDE)/sys $(I386_GEN_INCLUDE)/machine
	$(CC) $(I686_CFLAGS) $(I686_CPPFLAGS) $(I386_VM_INCLUDES) \
	    -c $< -o $@

rebsd-i686.elf: $(KERNEL_OBJS) $(I386_KERNEL_LDSCRIPT)
	$(LD) $(I686_LDFLAGS) --gc-sections -T $(I386_KERNEL_LDSCRIPT) \
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

ide-smoke: rebsd-i686.bzimg rootfs.img
	$(PYTHON) $(I386_TOOLS_SRC)/qemu-boot-smoke.py \
	    --qemu $(QEMU_I386) --machine $(QEMU_MACHINE) \
	    --cpu $(QEMU_CPU) --memory $(QEMU_MEMORY) --kernel $< \
	    --disk rootfs.img

fat-mount-smoke: rebsd-i686.bzimg $(I386_FAT_SMOKE_IMAGE) \
	    $(I386_FAT32_SMOKE_IMAGE)
	$(PYTHON) $(I386_TOOLS_SRC)/qemu-boot-smoke.py \
	    --qemu $(QEMU_I386) --machine $(QEMU_MACHINE) \
	    --cpu $(QEMU_CPU) --memory $(QEMU_MEMORY) --kernel $< \
	    --disk $(I386_FAT_SMOKE_IMAGE) --fat-mount-smoke --timeout 20
	$(PYTHON) $(I386_TOOLS_SRC)/qemu-boot-smoke.py \
	    --qemu $(QEMU_I386) --machine $(QEMU_MACHINE) \
	    --cpu $(QEMU_CPU) --memory $(QEMU_MEMORY) --kernel $< \
	    --disk $(I386_FAT32_SMOKE_IMAGE) --fat-mount-smoke \
	    --expect-fat-limited --fat-repair-smoke --timeout 30

ide-auto-smoke: rebsd-i686.bzimg rootfs.img
	$(PYTHON) $(I386_TOOLS_SRC)/qemu-boot-smoke.py \
	    --qemu $(QEMU_I386) --machine $(QEMU_MACHINE) \
	    --cpu $(QEMU_CPU) --memory $(QEMU_MEMORY) --kernel $< \
	    --disk rootfs.img --ata-mode auto

ide-pio-smoke: rebsd-i686.bzimg rootfs.img
	$(PYTHON) $(I386_TOOLS_SRC)/qemu-boot-smoke.py \
	    --qemu $(QEMU_I386) --machine $(QEMU_MACHINE) \
	    --cpu $(QEMU_CPU) --memory $(QEMU_MEMORY) --kernel $< \
	    --disk rootfs.img --ata-mode pio --ide-write-smoke --timeout 30

ide-dma-smoke: rebsd-i686.bzimg rootfs.img
	$(PYTHON) $(I386_TOOLS_SRC)/qemu-boot-smoke.py \
	    --qemu $(QEMU_I386) --machine $(QEMU_MACHINE) \
	    --cpu $(QEMU_CPU) --memory $(QEMU_MEMORY) --kernel $< \
	    --disk rootfs.img --ata-mode dma --ide-write-smoke --timeout 30

swap-smoke: rebsd-i686.bzimg $(I386_SWAP_SMOKE_IMAGE)
	$(PYTHON) $(I386_TOOLS_SRC)/qemu-boot-smoke.py \
	    --qemu $(QEMU_I386) --machine $(QEMU_MACHINE) \
	    --cpu $(QEMU_CPU) --memory $(QEMU_MEMORY) --kernel $< \
	    --disk $(I386_SWAP_SMOKE_IMAGE) --swap-smoke --timeout 30

ramdisk-smoke: rebsd-i686.bzimg
	$(PYTHON) $(I386_TOOLS_SRC)/qemu-boot-smoke.py \
	    --qemu $(QEMU_I386) --machine $(QEMU_MACHINE) \
	    --cpu $(QEMU_CPU) --memory $(QEMU_MEMORY) --kernel $< \
	    --ramdisk-smoke --timeout 30

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

init-halt-smoke: rebsd-i686.bzimg
	$(PYTHON) $(I386_TOOLS_SRC)/qemu-boot-smoke.py \
	    --qemu $(QEMU_I386) --machine $(QEMU_MACHINE) \
	    --cpu $(QEMU_CPU) --memory $(QEMU_MEMORY) --kernel $< \
	    --halt-smoke --timeout 20

bios-boot-smoke: rebsd-i686-bios-floppy.img rootfs.img
	$(PYTHON) $(I386_TOOLS_SRC)/qemu-boot-smoke.py \
	    --qemu $(QEMU_I386) --machine $(QEMU_MACHINE) \
	    --cpu $(QEMU_CPU) --memory $(QEMU_MEMORY) \
	    --bios-image rebsd-i686-bios-floppy.img \
	    --disk rootfs.img

usb-mass-storage-smoke: rebsd-i686.bzimg rootfs.img
	$(PYTHON) $(I386_TOOLS_SRC)/qemu-boot-smoke.py \
	    --qemu $(QEMU_I386) --machine $(QEMU_MACHINE) \
	    --cpu $(QEMU_CPU) --memory $(QEMU_MEMORY) --kernel $< \
	    --disk rootfs.img --usb-disk rootfs.img --timeout 20

bios-usb-mass-storage-smoke: rebsd-i686-bios-floppy.img rootfs.img
	$(PYTHON) $(I386_TOOLS_SRC)/qemu-boot-smoke.py \
	    --qemu $(QEMU_I386) --machine $(QEMU_MACHINE) \
	    --cpu $(QEMU_CPU) --memory $(QEMU_MEMORY) \
	    --bios-image rebsd-i686-bios-floppy.img \
	    --disk rootfs.img --usb-disk rootfs.img --timeout 20

usb-mass-storage-ide-absent-smoke: rebsd-i686.bzimg rootfs.img
	$(PYTHON) $(I386_TOOLS_SRC)/qemu-boot-smoke.py \
	    --qemu $(QEMU_I386) --machine $(QEMU_MACHINE) \
	    --cpu $(QEMU_CPU) --memory $(QEMU_MEMORY) --kernel $< \
	    --usb-disk rootfs.img --expect-no-disk --timeout 20

bios-usb-mass-storage-ide-absent-smoke: \
	    rebsd-i686-bios-floppy.img rootfs.img
	$(PYTHON) $(I386_TOOLS_SRC)/qemu-boot-smoke.py \
	    --qemu $(QEMU_I386) --machine $(QEMU_MACHINE) \
	    --cpu $(QEMU_CPU) --memory $(QEMU_MEMORY) \
	    --bios-image rebsd-i686-bios-floppy.img \
	    --usb-disk rootfs.img --expect-no-disk --timeout 20

ohci-keyboard-smoke: rebsd-i686.bzimg rootfs.img
	$(PYTHON) $(I386_TOOLS_SRC)/qemu-boot-smoke.py \
	    --qemu $(QEMU_I386) --machine $(QEMU_MACHINE) \
	    --cpu $(QEMU_CPU) --memory $(QEMU_MEMORY) --kernel $< \
	    --disk rootfs.img --ohci-keyboard --timeout 20

bios-ohci-keyboard-smoke: rebsd-i686-bios-floppy.img rootfs.img
	$(PYTHON) $(I386_TOOLS_SRC)/qemu-boot-smoke.py \
	    --qemu $(QEMU_I386) --machine $(QEMU_MACHINE) \
	    --cpu $(QEMU_CPU) --memory $(QEMU_MEMORY) \
	    --bios-image rebsd-i686-bios-floppy.img \
	    --disk rootfs.img --ohci-keyboard --timeout 20

ohci-mouse-smoke: rebsd-i686.bzimg rootfs.img
	$(PYTHON) $(I386_TOOLS_SRC)/qemu-boot-smoke.py \
	    --qemu $(QEMU_I386) --machine $(QEMU_MACHINE) \
	    --cpu $(QEMU_CPU) --memory $(QEMU_MEMORY) --kernel $< \
	    --disk rootfs.img --ohci-mouse --timeout 20

bios-ohci-mouse-smoke: rebsd-i686-bios-floppy.img rootfs.img
	$(PYTHON) $(I386_TOOLS_SRC)/qemu-boot-smoke.py \
	    --qemu $(QEMU_I386) --machine $(QEMU_MACHINE) \
	    --cpu $(QEMU_CPU) --memory $(QEMU_MEMORY) \
	    --bios-image rebsd-i686-bios-floppy.img \
	    --disk rootfs.img --ohci-mouse --timeout 20

uhci-keyboard-smoke: rebsd-i686.bzimg rootfs.img
	$(PYTHON) $(I386_TOOLS_SRC)/qemu-boot-smoke.py \
	    --qemu $(QEMU_I386) --machine $(QEMU_MACHINE) \
	    --cpu $(QEMU_CPU) --memory $(QEMU_MEMORY) --kernel $< \
	    --disk rootfs.img --uhci-keyboard --timeout 20

bios-uhci-keyboard-smoke: rebsd-i686-bios-floppy.img rootfs.img
	$(PYTHON) $(I386_TOOLS_SRC)/qemu-boot-smoke.py \
	    --qemu $(QEMU_I386) --machine $(QEMU_MACHINE) \
	    --cpu $(QEMU_CPU) --memory $(QEMU_MEMORY) \
	    --bios-image rebsd-i686-bios-floppy.img \
	    --disk rootfs.img --uhci-keyboard --timeout 20

uhci-mouse-smoke: rebsd-i686.bzimg rootfs.img
	$(PYTHON) $(I386_TOOLS_SRC)/qemu-boot-smoke.py \
	    --qemu $(QEMU_I386) --machine $(QEMU_MACHINE) \
	    --cpu $(QEMU_CPU) --memory $(QEMU_MEMORY) --kernel $< \
	    --disk rootfs.img --uhci-mouse --timeout 20

bios-uhci-mouse-smoke: rebsd-i686-bios-floppy.img rootfs.img
	$(PYTHON) $(I386_TOOLS_SRC)/qemu-boot-smoke.py \
	    --qemu $(QEMU_I386) --machine $(QEMU_MACHINE) \
	    --cpu $(QEMU_CPU) --memory $(QEMU_MEMORY) \
	    --bios-image rebsd-i686-bios-floppy.img \
	    --disk rootfs.img --uhci-mouse --timeout 20

ps2-input-smoke: rebsd-i686.bzimg rootfs.img
	$(PYTHON) $(I386_TOOLS_SRC)/qemu-boot-smoke.py \
	    --qemu $(QEMU_I386) --machine $(QEMU_MACHINE) \
	    --cpu $(QEMU_CPU) --memory $(QEMU_MEMORY) --kernel $< \
	    --disk rootfs.img --ps2-keyboard --ps2-mouse --timeout 20

ide-dma-ps2-input-smoke: rebsd-i686.bzimg rootfs.img
	$(PYTHON) $(I386_TOOLS_SRC)/qemu-boot-smoke.py \
	    --qemu $(QEMU_I386) --machine $(QEMU_MACHINE) \
	    --cpu $(QEMU_CPU) --memory $(QEMU_MEMORY) --kernel $< \
	    --disk rootfs.img --ata-mode dma \
	    --ps2-keyboard --ps2-mouse --timeout 20

bios-ps2-input-smoke: rebsd-i686-bios-floppy.img rootfs.img
	$(PYTHON) $(I386_TOOLS_SRC)/qemu-boot-smoke.py \
	    --qemu $(QEMU_I386) --machine $(QEMU_MACHINE) \
	    --cpu $(QEMU_CPU) --memory $(QEMU_MEMORY) \
	    --bios-image rebsd-i686-bios-floppy.img \
	    --disk rootfs.img --ps2-keyboard --ps2-mouse --timeout 20

uhci-mass-storage-smoke: rebsd-i686.bzimg rootfs.img
	$(PYTHON) $(I386_TOOLS_SRC)/qemu-boot-smoke.py \
	    --qemu $(QEMU_I386) --machine $(QEMU_MACHINE) \
	    --cpu $(QEMU_CPU) --memory $(QEMU_MEMORY) --kernel $< \
	    --disk rootfs.img --uhci-disk rootfs.img --timeout 20

bios-uhci-mass-storage-smoke: rebsd-i686-bios-floppy.img rootfs.img
	$(PYTHON) $(I386_TOOLS_SRC)/qemu-boot-smoke.py \
	    --qemu $(QEMU_I386) --machine $(QEMU_MACHINE) \
	    --cpu $(QEMU_CPU) --memory $(QEMU_MEMORY) \
	    --bios-image rebsd-i686-bios-floppy.img \
	    --disk rootfs.img --uhci-disk rootfs.img --timeout 20

uhci-mass-storage-ide-absent-smoke: rebsd-i686.bzimg rootfs.img
	$(PYTHON) $(I386_TOOLS_SRC)/qemu-boot-smoke.py \
	    --qemu $(QEMU_I386) --machine $(QEMU_MACHINE) \
	    --cpu $(QEMU_CPU) --memory $(QEMU_MEMORY) --kernel $< \
	    --uhci-disk rootfs.img --expect-no-disk --timeout 20

bios-uhci-mass-storage-ide-absent-smoke: \
	    rebsd-i686-bios-floppy.img rootfs.img
	$(PYTHON) $(I386_TOOLS_SRC)/qemu-boot-smoke.py \
	    --qemu $(QEMU_I386) --machine $(QEMU_MACHINE) \
	    --cpu $(QEMU_CPU) --memory $(QEMU_MEMORY) \
	    --bios-image rebsd-i686-bios-floppy.img \
	    --uhci-disk rootfs.img --expect-no-disk --timeout 20

usb-combined-smoke: rebsd-i686.bzimg rootfs.img
	$(PYTHON) $(I386_TOOLS_SRC)/qemu-boot-smoke.py \
	    --qemu $(QEMU_I386) --machine $(QEMU_MACHINE) \
	    --cpu $(QEMU_CPU) --memory $(QEMU_MEMORY) --kernel $< \
	    --disk rootfs.img --usb-disk rootfs.img --ohci-keyboard \
	    --timeout 20

bios-usb-combined-smoke: rebsd-i686-bios-floppy.img rootfs.img
	$(PYTHON) $(I386_TOOLS_SRC)/qemu-boot-smoke.py \
	    --qemu $(QEMU_I386) --machine $(QEMU_MACHINE) \
	    --cpu $(QEMU_CPU) --memory $(QEMU_MEMORY) \
	    --bios-image rebsd-i686-bios-floppy.img \
	    --disk rootfs.img --usb-disk rootfs.img --ohci-keyboard \
	    --timeout 20

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

boot-smoke-matrix: rebsd-i686.bzimg
	@for memory in 32M 64M 128M 256M 768M 1024M; do \
	    echo "boot-smoke-matrix: memory=$$memory"; \
	    $(PYTHON) $(I386_TOOLS_SRC)/qemu-boot-smoke.py \
	        --qemu $(QEMU_I386) --machine $(QEMU_MACHINE) \
	        --cpu $(QEMU_CPU) --memory $$memory --kernel $< || exit 1; \
	done

clean:
	rm -f setup.o setup.elf setup.bin setup.map \
	    vers.c \
	    $(I386_FAT_SMOKE_IMAGE) $(I386_FAT32_SMOKE_IMAGE) \
	    $(I386_SWAP_SMOKE_IMAGE) \
	    $(I386_HOST_MKFS_FAT) \
	    rootfs.img rootfs.check.img rootfs_image.o \
	    $(I386_ROOTFS_MANIFEST) $(I386_USER_LDSCRIPT) \
	    $(I386_KERNEL_LDSCRIPT) \
	    $(I386_ROOTFS_BASE_STAMP) \
	    $(I386_USERLAND_STAMP) \
	    rebsd-i686-bios-floppy.img \
	    rebsd-i686-bios-floppy.check.img \
	    $(KERNEL_OBJS) rebsd-i686.elf rebsd-i686.bin \
	    rebsd-i686.bzimg rebsd-i686.map
	rm -rf $(I386_ROOTFS_STAGE)
	rm -rf $(I386_ROOTFS_BASE)
	rm -rf $(I386_USERLAND_BUILD)
	rm -rf .deps
	rm -f $(I386_GEN_INCLUDE)/sys $(I386_GEN_INCLUDE)/machine
	rmdir $(I386_GEN_INCLUDE) 2>/dev/null || true

endif
