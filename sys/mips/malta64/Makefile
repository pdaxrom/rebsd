PARAM = -DMALTA64
PARAM += -DMIPS3
PARAM += -DMALTA_UART_ENABLED
PARAM += -DMALTA_ENABLED
PARAM += -DPTY_ENABLED
PARAM += -DPTY_NUNITS=4
PARAM += -DETHER_ENABLED
PARAM += -DETHER_NUNITS=2
PARAM += -DMALTA_NE_ENABLED
PARAM += -DUSBNET_ENABLED
PARAM += -DINET_ENABLED
PARAM += -DUNIXDOMAIN_ENABLED
PARAM += -DCONS_MINOR=0
PARAM += -DCONS_MAJOR=0
#
# Makefile template for shared MIPS ports.
#
MALTA_PORT64 = $(filter -DMALTA64,$(PARAM))
MALTA_CPU_TUNE ?= $(if $(MALTA_PORT64),r4000,24kf)
MIPS_ARCH ?= $(if $(MALTA_PORT64),-EB -mips3 -mabi=32 -mtune=$(MALTA_CPU_TUNE),-EB -mips32 -mabi=32 -mtune=$(MALTA_CPU_TUNE))
MALTA_QEMU_RAM ?= 32M
MALTA_QEMU ?= $(if $(MALTA_PORT64),qemu-system-mips64,qemu-system-mips)
MALTA_QEMU_CPU ?= $(if $(MALTA_PORT64),R4000,)
MALTA_QEMU_CPU_OPT = $(if $(MALTA_QEMU_CPU),-cpu $(MALTA_QEMU_CPU),)
MALTA_QEMU_CPU_ARG = $(if $(MALTA_QEMU_CPU),--cpu $(MALTA_QEMU_CPU),)
MALTA_VR4300_QEMU ?= qemu-system-mips64
MALTA_VR4300_CPU ?= R4000

include ../../../target-mips.mk

TOPSRC          = ../../..
S               = ../..
BOARD_SRC       = ../malta
MALTA_HW_MODEL ?= $(if $(MALTA_PORT64),MIPS Malta R4000,MIPS Malta 24Kf)
MALTA_VERSION_NAME ?= $(if $(MALTA_PORT64),Malta64,Malta)
MALTA_KERNEL_LD_SCRIPT ?= ../malta/malta.ld
MALTA_RUNTIME_KERNEL ?= $(if $(MALTA_PORT64),malta64.elf,unix.elf)
MALTA_EARLY_TARGET ?= $(if $(MALTA_PORT64),malta64-early.elf,malta-early.elf)
MALTA64_STAGE_ARCH ?= -EB -mips3 -mabi=64 -mtune=r4000
MALTA64_STAGE_CC = $(MIPS_PREFIX)gcc $(MALTA64_STAGE_ARCH) $(MIPS_CODE)
MALTA64_STAGE_ASFLAGS = -x assembler-with-cpp -c -I. -I$(TOPSRC)/include \
                        $(PARAM) -DKERNEL -DMIPS -Wa,-I.
MALTA64_STAGE_LD ?= ld.lld
MALTA64_STAGE_LDFLAGS ?= -m elf64btsmip -nostdlib
MALTA64_LLVM_BINDIR ?= /opt/homebrew/opt/llvm/bin
MALTA64_STAGE_SIZE ?= $(MALTA64_LLVM_BINDIR)/llvm-size
MALTA64_STAGE_OBJDUMP ?= $(MALTA64_LLVM_BINDIR)/llvm-objdump
MIPS_ROOTFS_KBYTES ?= 16384
MIPS_ROOTFS_SRC ?= $(TOPSRC)/sys/mips/n64/rootfs.stage
MIPS_ROOTFS_COMPILER ?= $(if $(N64_USERLAND_COMPILER),$(N64_USERLAND_COMPILER),gcc)
MIPS_ROOTFS_COMPILERS = gcc pcc
ifneq ($(N64_USERLAND_COMPILER),)
ifneq ($(MIPS_ROOTFS_COMPILER),$(N64_USERLAND_COMPILER))
$(error MIPS_ROOTFS_COMPILER=$(MIPS_ROOTFS_COMPILER) conflicts with N64_USERLAND_COMPILER=$(N64_USERLAND_COMPILER))
endif
endif
ifeq ($(filter $(MIPS_ROOTFS_COMPILER),$(MIPS_ROOTFS_COMPILERS)),)
$(error Unsupported MIPS_ROOTFS_COMPILER=$(MIPS_ROOTFS_COMPILER); expected one of $(MIPS_ROOTFS_COMPILERS))
endif
MIPS_KERNEL_COMPILER ?= gcc
MIPS_KERNEL_COMPILERS = gcc pcc
ifeq ($(filter $(MIPS_KERNEL_COMPILER),$(MIPS_KERNEL_COMPILERS)),)
$(error Unsupported MIPS_KERNEL_COMPILER=$(MIPS_KERNEL_COMPILER); expected one of $(MIPS_KERNEL_COMPILERS))
endif
MIPS_ROOTFS_SRC_STAMP ?= $(MIPS_ROOTFS_SRC)/.userland.$(MIPS_ROOTFS_COMPILER)
MIPS_ROOTFS_OVERLAY ?= $(TOPSRC)/sys/mips/malta/rootfs
MIPS_ROOTFS_COMMON_MANIFEST ?= $(TOPSRC)/sys/mips/rootfs.manifest
MIPS_ROOTFS_BOARD_MANIFEST ?= $(TOPSRC)/sys/mips/malta/rootfs.manifest
MIPS_ROOTFS_BUILD_MANIFEST = rootfs.generated.manifest
MIPS_ROOTFS_IMAGE_MANIFEST ?= $(MIPS_ROOTFS_BUILD_MANIFEST)
MIPS_ROOTFS_STAGE = rootfs.stage
MIPS_ROOTFS_STAMP = $(MIPS_ROOTFS_STAGE)/.stamp
MIPS_ROOTFS_COMMON_FILES = $(shell find $(TOPSRC)/sys/mips/rootfs \
                           -type f -o -type l 2>/dev/null)
MIPS_ROOTFS_OVERLAY_FILES = $(shell find $(MIPS_ROOTFS_OVERLAY) -type f -o -type l 2>/dev/null)
MIPS_COMMON_MACHINE_HEADERS = console cpu devmajors elf_machdep float fpu io \
                              layout limits machparam ramswap romdisk
MIPS_TRACE       ?= 0
FSUTIL          = $(TOPSRC)/tools/fsutil/fsutil
PCC_HOST_SMOKE_SCRIPT = $(TOPSRC)/sys/mips/n64/native/smoke-host-portablecc.sh
PCC_HOST_BUILD ?= /private/tmp/rebsd-pcc-build
PCC_HOST_PREFIX ?= /private/tmp/rebsd-pcc-install
PCC_HOST_AS     = $(TOPSRC)/sys/mips/n64/n64-native-tools/as
PCC_HOST_LD     = $(TOPSRC)/sys/mips/n64/n64-native-tools/ld
PCC_HOST_AR     = $(TOPSRC)/sys/mips/n64/n64-native-tools/ar
PCC_HOST_PCC    = $(PCC_HOST_PREFIX)/bin/mips-rebsd-pcc
PCC_HOST_SRCS   = $(PCC_HOST_SMOKE_SCRIPT) \
                  $(shell find $(TOPSRC)/src/dev/pcc/pcc -type f \
                  \( -name '*.[chly]' -o -name '*.h' -o -name Makefile.in \
                  -o -name configure -o -name config.sub \
                  -o -name config.guess \) 2>/dev/null)
PCC_REGRESS_SCRIPT = $(TOPSRC)/sys/mips/tools/pcc-regress.py
PCC_REGRESS_OUT ?= /private/tmp/rebsd-pcc-tests
PCC_REGRESS_MANIFEST ?= /private/tmp/rebsd-pcc-tests.manifest
PCC_REGRESS_LOG ?= /private/tmp/rebsd-pcc-runtime.log
NATIVE_PCC_REGRESS_MANIFEST ?= /private/tmp/rebsd-native-pcc-tests.manifest
NATIVE_PCC_REGRESS_LOG ?= /private/tmp/rebsd-native-pcc-tests.log
NATIVE_PCC_REGRESS_TIMEOUT ?= 1200
NATIVE_PCC_REGRESS_SILENCE_TIMEOUT ?= 300
LINPACK_SMOKE_SCRIPT = $(TOPSRC)/sys/mips/tools/linpack-smoke.py
LINPACK_SMOKE_SRC = $(TOPSRC)/sys/mips/rootfs/root/linpack.c
LINPACK_SMOKE_OUT ?= /private/tmp/rebsd-linpack-smoke
LINPACK_SMOKE_MANIFEST ?= /private/tmp/rebsd-linpack-smoke.manifest
LINPACK_SMOKE_LOG ?= /private/tmp/rebsd-linpack-smoke.log
LINPACK_SMOKE_ARRAY_SIZE ?= 120
LINPACK_SMOKE_MIN_SECONDS ?= 1
MIPS_LINPACK_ROOTFS_STAMP = $(MIPS_ROOTFS_STAGE)/.linpack-smoke
MIPS_ROOTFS_EXTRA_STAMPS =
ifeq ($(MIPS_ROOTFS_COMPILER),pcc)
MIPS_ROOTFS_EXTRA_STAMPS += $(MIPS_LINPACK_ROOTFS_STAMP)
endif
MALTA_RUN_COMMAND_SCRIPT = $(TOPSRC)/sys/mips/tools/malta-run-command.py
NATIVE_PCC_SMOKE_LOG ?= /private/tmp/rebsd-native-pcc-smoke.log

CPPFLAGS        = -I. -I$(TOPSRC)/include ${PARAM} -DKERNEL -DMIPS \
                  -DROMFS_ENABLED \
                  -DHW_MACHINE_NAME=\"mips\" \
                  -DHW_MODEL_NAME='"$(MALTA_HW_MODEL)"'
ifneq ($(filter -DINET_ENABLED,$(PARAM)),)
CPPFLAGS        += -I$(TOPSRC)/include/sys -I$(TOPSRC)/sys \
                  -I$(TOPSRC)/sys/net \
                  -I$(TOPSRC)/sys/netinet -DINET -DSUPERVISOR
INET_CWARN      = -Wno-comment -Wno-endif-labels -Wno-implicit-int \
                  -Wno-implicit-function-declaration -Wno-parentheses \
                  -Wno-return-mismatch -Wno-return-type
endif
ifneq ($(filter -DUNIXDOMAIN_ENABLED,$(PARAM)),)
CPPFLAGS        += -DUNIXDOMAIN
endif
ifeq ($(MIPS_TRACE),1)
CPPFLAGS        += -DMIPS_TRACE
endif
CFLAGS          = -O2 -Wall -Werror -Wno-array-bounds -nostdinc \
                  $(CPPFLAGS) $(INET_CWARN) -MT $@ -MP -MD -MF .deps/$*.dep
VERS_CFLAGS     = -O2 -Wall -Werror -Wno-array-bounds -nostdinc $(CPPFLAGS)
ASFLAGS         = -x assembler-with-cpp -c $(CPPFLAGS) -MT $@ -MP -MD \
                  -MF .deps/$*.dep
FPU_ASFLAGS     = $(ASFLAGS)

KERNEL_CC       = ${CC}
KERNEL_AS       = ${CC} -x assembler -c
KERNEL_COMPILER_DEPS =
COMPILE_C       = ${KERNEL_CC} ${CFLAGS} -c $<
COMPILE_VERS_C  = ${KERNEL_CC} ${VERS_CFLAGS} -MT vers.o -MP -MD \
                  -MF .deps/vers.dep -c vers.c -o vers.o
ifeq ($(MIPS_KERNEL_COMPILER),pcc)
KERNEL_CC       = $(PCC_HOST_PCC)
KERNEL_COMPILER_DEPS = $(PCC_HOST_PCC)
COMPILE_C       = ${KERNEL_CC} ${CFLAGS} -S -o .deps/$(@:.o=.s) $< && \
                  if grep -nE '^[[:space:]]*((add|sub|mul|div|sqrt|abs|mov|neg|cvt|trunc|round|floor|ceil|c\.[[:alnum:]_]+)\.(s|d|w|l)|(l|s)\.(s|d)|(lwc1|ldc1|swc1|sdc1|mfc1|mtc1|dmfc1|dmtc1|cfc1|ctc1|bc1[[:alnum:]_]*))[[:space:]]' .deps/$(@:.o=.s); then \
                      echo "error: PCC kernel C emitted COP1/FPU instructions in .deps/$(@:.o=.s)" >&2; exit 1; \
                  fi && \
                  ${KERNEL_AS} .deps/$(@:.o=.s) -o $@
COMPILE_VERS_C  = ${KERNEL_CC} ${VERS_CFLAGS} -MT vers.o -MP -MD \
                  -MF .deps/vers.dep -S -o .deps/vers.s vers.c && \
                  if grep -nE '^[[:space:]]*((add|sub|mul|div|sqrt|abs|mov|neg|cvt|trunc|round|floor|ceil|c\.[[:alnum:]_]+)\.(s|d|w|l)|(l|s)\.(s|d)|(lwc1|ldc1|swc1|sdc1|mfc1|mtc1|dmfc1|dmtc1|cfc1|ctc1|bc1[[:alnum:]_]*))[[:space:]]' .deps/vers.s; then \
                      echo "error: PCC kernel C emitted COP1/FPU instructions in .deps/vers.s" >&2; exit 1; \
                  fi && \
                  ${KERNEL_AS} .deps/vers.s -o vers.o
endif
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
	sys_socket.o uipc_domain.o uipc_mbuf.o uipc_proto.o \
	uipc_usrreq.o uipc_socket.o uipc_socket2.o uipc_syscalls.o af.o \
	if.o if_loop.o if_ether.o raw_cb.o raw_usrreq.o route.o in.o \
	in_pcb.o in_proto.o ip_icmp.o ip_input.o ip_output.o raw_ip.o \
	tcp_debug.o tcp_input.o tcp_output.o tcp_subr.o tcp_timer.o \
	tcp_usrreq.o udp_usrreq.o startup.o exception_entry.o \
	exception.o fpu.o cons.o in_cksum.o net_glue.o netinit.o \
	signal.o sysctl.o libkern.o stubs.o uart.o devsw.o clock.o \
	machdep.o romdisk.o romfs_core.o romfs_vfs.o cartflash.o \
	ramswap.o config.o if_ne.o if_usbn.o if_usbn_fake.o

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
	$S/kernel/vm_swp.c $S/kernel/sys_socket.c \
	$S/kernel/uipc_domain.c $S/kernel/uipc_mbuf.c \
	$S/kernel/uipc_proto.c $S/kernel/uipc_usrreq.c \
	$S/kernel/uipc_socket.c $S/kernel/uipc_socket2.c \
	$S/kernel/uipc_syscalls.c $S/net/af.c $S/net/if.c \
	$S/net/if_loop.c $S/netinet/if_ether.c $S/net/raw_cb.c \
	$S/net/raw_usrreq.c $S/net/route.c $S/netinet/in.c \
	$S/netinet/in_pcb.c $S/netinet/in_proto.c $S/netinet/ip_icmp.c \
	$S/netinet/ip_input.c $S/netinet/ip_output.c $S/netinet/raw_ip.c \
	$S/netinet/tcp_debug.c $S/netinet/tcp_input.c \
	$S/netinet/tcp_output.c $S/netinet/tcp_subr.c \
	$S/netinet/tcp_timer.c $S/netinet/tcp_usrreq.c \
	$S/netinet/udp_usrreq.c $S/mips/common/exception.c \
	$S/mips/common/cons.c $S/mips/common/in_cksum.c \
	$S/mips/common/net_glue.c $S/mips/common/netinit.c \
	$S/mips/common/signal.c $S/mips/common/sysctl.c \
	$S/mips/common/libkern.c $S/mips/common/stubs.c \
	$S/mips/malta/uart.c $S/mips/malta/devsw.c $S/mips/malta/clock.c \
	$S/mips/malta/machdep.c $S/mips/malta/romdisk.c \
	$S/mips/common/romfs_core.c $S/mips/common/romfs_vfs.c \
	$S/mips/malta/cartflash.c $S/mips/malta/ramswap.c \
	$S/mips/malta/config.c $S/mips/malta/if_ne.c \
	$S/mips/common/if_usbn.c $S/mips/malta/if_usbn_fake.c swapunix.c

SYSTEM_OBJ      = ${OBJS} ioconf.o rootfs.o
SYSTEM_DEP      = Makefile ioconf.c $(MALTA_KERNEL_LD_SCRIPT) \
                  $(KERNEL_COMPILER_DEPS)
SYSTEM_LD_HEAD  = printf '%s\n' 'const char version[] = "ReBSD for $(MALTA_VERSION_NAME): local build\n";' > vers.c; \
                  ${COMPILE_VERS_C}; \
                  rm -f $@
SYSTEM_LD       = ${LD} -nostdlib -T $(MALTA_KERNEL_LD_SCRIPT) ${SYSTEM_OBJ} vers.o -o $@
SYSTEM_LD_TAIL  = ${SIZE} $@; \
                  ${OBJDUMP} -d -S $@ > $(basename $@).dis; \
                  ${NM} -n $@ > $(basename $@).nm

all: unix $(MALTA_EARLY_TARGET) $(if $(MALTA_PORT64),malta64.elf,)

kernel: unix $(if $(MALTA_PORT64),malta64.elf,)

malta-early.o: ../malta/early_uart.S | .deps
	$(CC) $(ASFLAGS) $< -o $@

malta-early.elf: malta-early.o ../malta/malta.ld
	$(LD) -nostdlib -T ../malta/malta.ld malta-early.o -o $@
	$(SIZE) $@
	$(OBJDUMP) -d -S $@ > malta-early.dis

unix.bin: unix.elf
	$(OBJCOPY) -O binary unix.elf unix.bin

malta64-stage0.o: ../malta64/stage0.S | .deps
	$(MALTA64_STAGE_CC) $(MALTA64_STAGE_ASFLAGS) $< -o $@

malta64-stage0-kernel.o: ../malta64/stage0_kernel_blob.S unix.bin | .deps
	$(MALTA64_STAGE_CC) $(MALTA64_STAGE_ASFLAGS) $< -o $@

malta64.elf: malta64-stage0.o malta64-stage0-kernel.o ../malta64/stage0_linker.ld
	$(MALTA64_STAGE_LD) $(MALTA64_STAGE_LDFLAGS) \
	    -T ../malta64/stage0_linker.ld \
	    malta64-stage0.o malta64-stage0-kernel.o -o $@
	$(MALTA64_STAGE_SIZE) $@
	$(MALTA64_STAGE_OBJDUMP) -d -S $@ > malta64.dis

malta64-early.o: ../malta64/early_uart64.S | .deps
	$(MALTA64_STAGE_CC) $(MALTA64_STAGE_ASFLAGS) $< -o $@

malta64-early.elf: malta64-early.o ../malta64/stage0_linker.ld
	$(MALTA64_STAGE_LD) $(MALTA64_STAGE_LDFLAGS) \
	    -T ../malta64/stage0_linker.ld malta64-early.o -o $@
	$(MALTA64_STAGE_SIZE) $@
	$(MALTA64_STAGE_OBJDUMP) -d -S $@ > malta64-early.dis

run-early: $(MALTA_EARLY_TARGET)
	$(MALTA_QEMU) -M malta $(MALTA_QEMU_CPU_OPT) -m $(MALTA_QEMU_RAM) \
	    -nographic -serial mon:stdio -no-reboot -kernel $(MALTA_EARLY_TARGET)

run: $(MALTA_RUNTIME_KERNEL)
	$(MALTA_QEMU) -M malta $(MALTA_QEMU_CPU_OPT) -m $(MALTA_QEMU_RAM) \
	    -nographic -serial mon:stdio -no-reboot -kernel $(MALTA_RUNTIME_KERNEL)

run-vr4300: unix.elf
	$(MALTA_VR4300_QEMU) -M malta -cpu $(MALTA_VR4300_CPU) \
	    -m $(MALTA_QEMU_RAM) -nographic -serial mon:stdio \
	    -no-reboot -kernel unix.elf

run-net: $(MALTA_RUNTIME_KERNEL)
	$(MALTA_QEMU) -M malta $(MALTA_QEMU_CPU_OPT) -m $(MALTA_QEMU_RAM) \
	    -nographic -serial mon:stdio -no-reboot -kernel $(MALTA_RUNTIME_KERNEL) \
	    -netdev user,id=net0,guestfwd=tcp:10.0.2.100:2323-cmd:/bin/cat \
	    -device ne2k_isa,netdev=net0,iobase=0x300,irq=9,mac=52:54:00:12:34:56

.PHONY: smoke-host-portablecc pcc-regress-compile pcc-regress-stage \
        pcc-regress-runtime pcc-regress-runtime-vr4300 \
        pcc-regress-summary linpack-smoke-build \
        linpack-smoke-stage linpack-smoke-runtime \
        linpack-smoke-runtime-vr4300 native-pcc-smoke-runtime \
        native-pcc-smoke-runtime-vr4300 native-pcc-regress-stage \
        native-pcc-regress-runtime native-pcc-regress-runtime-vr4300 \
        rootfs-patch rootfs-repack-kernel rootfs-patch-kernel FORCE
$(PCC_HOST_PCC): $(MIPS_ROOTFS_STAMP) $(PCC_HOST_SRCS) $(PCC_HOST_AS) \
    $(PCC_HOST_LD)
	sh $(PCC_HOST_SMOKE_SCRIPT) $(abspath $(TOPSRC)) \
	    $(abspath $(PCC_HOST_BUILD)) $(abspath $(PCC_HOST_PREFIX)) \
	    $(abspath $(MIPS_ROOTFS_STAGE)/usr/include) \
	    $(abspath $(MIPS_ROOTFS_STAGE)/usr/lib) \
	    $(abspath $(PCC_HOST_AS)) $(abspath $(PCC_HOST_LD))

smoke-host-portablecc: $(PCC_HOST_PCC)
	@echo "smoke-host-portablecc: ok"

pcc-regress-compile: smoke-host-portablecc $(PCC_REGRESS_SCRIPT)
	python3 $(PCC_REGRESS_SCRIPT) compile \
	    --pcc $(abspath $(PCC_HOST_PCC)) \
	    --rootfs $(abspath $(MIPS_ROOTFS_STAGE)) \
	    --out $(PCC_REGRESS_OUT) \
	    --ar $(abspath $(PCC_HOST_AR))

pcc-regress-stage: pcc-regress-compile $(MIPS_ROOTFS_BUILD_MANIFEST)
	python3 $(PCC_REGRESS_SCRIPT) stage \
	    --out $(PCC_REGRESS_OUT) \
	    --rootfs $(abspath $(MIPS_ROOTFS_STAGE)) \
	    --manifest $(abspath $(MIPS_ROOTFS_BUILD_MANIFEST)) \
	    --manifest-out $(PCC_REGRESS_MANIFEST)
	cp $(PCC_REGRESS_MANIFEST) $(MIPS_ROOTFS_BUILD_MANIFEST)

pcc-regress-runtime: pcc-regress-stage
	$(MAKE) $(MALTA_RUNTIME_KERNEL) \
	    MIPS_ROOTFS_IMAGE_MANIFEST=$(abspath $(PCC_REGRESS_MANIFEST))
	python3 $(PCC_REGRESS_SCRIPT) run \
	    --malta-dir $(abspath .) \
	    --kernel $(MALTA_RUNTIME_KERNEL) \
	    --log $(PCC_REGRESS_LOG) \
	    --qemu $(MALTA_QEMU) $(MALTA_QEMU_CPU_ARG) \
	    --ram $(MALTA_QEMU_RAM)

pcc-regress-runtime-vr4300:
	$(MAKE) pcc-regress-runtime \
	    MALTA_QEMU=$(MALTA_VR4300_QEMU) \
	    MALTA_QEMU_CPU=$(MALTA_VR4300_CPU) \
	    PCC_REGRESS_LOG=/private/tmp/rebsd-pcc-runtime-vr4300.log

pcc-regress-summary: $(PCC_REGRESS_SCRIPT)
	python3 $(PCC_REGRESS_SCRIPT) summarize \
	    --out $(PCC_REGRESS_OUT) \
	    --log $(PCC_REGRESS_LOG)

linpack-smoke-build: smoke-host-portablecc $(LINPACK_SMOKE_SCRIPT) \
    $(LINPACK_SMOKE_SRC)
	python3 $(LINPACK_SMOKE_SCRIPT) build \
	    --source $(abspath $(LINPACK_SMOKE_SRC)) \
	    --out $(LINPACK_SMOKE_OUT) \
	    --top $(abspath $(TOPSRC)) \
	    --rootfs $(abspath $(MIPS_ROOTFS_STAGE)) \
	    --gcc-wrapper $(abspath $(TOPSRC)/sys/mips/n64/native/n64-aout-cc.sh) \
	    --gcc-prefix $(MIPS_PREFIX) \
	    --as $(abspath $(PCC_HOST_AS)) \
	    --ld $(abspath $(PCC_HOST_LD)) \
	    --pcc $(abspath $(PCC_HOST_PCC))

$(MIPS_LINPACK_ROOTFS_STAMP): $(MIPS_ROOTFS_STAMP) linpack-smoke-build
	cp -p $(LINPACK_SMOKE_OUT)/linpack-gcc \
	    $(MIPS_ROOTFS_STAGE)/root/linpack-gcc
	cp -p $(LINPACK_SMOKE_OUT)/linpack-pcc \
	    $(MIPS_ROOTFS_STAGE)/root/linpack-pcc
	chmod 0775 $(MIPS_ROOTFS_STAGE)/root/linpack-gcc \
	    $(MIPS_ROOTFS_STAGE)/root/linpack-pcc
	touch $@

linpack-smoke-stage: linpack-smoke-build $(MIPS_ROOTFS_BUILD_MANIFEST)
	python3 $(LINPACK_SMOKE_SCRIPT) stage \
	    --out $(LINPACK_SMOKE_OUT) \
	    --rootfs $(abspath $(MIPS_ROOTFS_STAGE)) \
	    --manifest $(abspath $(MIPS_ROOTFS_BUILD_MANIFEST)) \
	    --manifest-out $(LINPACK_SMOKE_MANIFEST)
	cp $(LINPACK_SMOKE_MANIFEST) $(MIPS_ROOTFS_BUILD_MANIFEST)

linpack-smoke-runtime: linpack-smoke-stage
	$(MAKE) $(MALTA_RUNTIME_KERNEL) \
	    MIPS_ROOTFS_IMAGE_MANIFEST=$(abspath $(LINPACK_SMOKE_MANIFEST))
	python3 $(LINPACK_SMOKE_SCRIPT) run \
	    --malta-dir $(abspath .) \
	    --kernel $(MALTA_RUNTIME_KERNEL) \
	    --log $(LINPACK_SMOKE_LOG) \
	    --qemu $(MALTA_QEMU) $(MALTA_QEMU_CPU_ARG) \
	    --ram $(MALTA_QEMU_RAM) \
	    --array-size $(LINPACK_SMOKE_ARRAY_SIZE) \
	    --min-seconds $(LINPACK_SMOKE_MIN_SECONDS)

linpack-smoke-runtime-vr4300:
	$(MAKE) linpack-smoke-runtime \
	    MALTA_QEMU=$(MALTA_VR4300_QEMU) \
	    MALTA_QEMU_CPU=$(MALTA_VR4300_CPU) \
	    LINPACK_SMOKE_LOG=/private/tmp/rebsd-linpack-smoke-vr4300.log

native-pcc-smoke-runtime: $(MIPS_ROOTFS_STAMP) $(MALTA_RUN_COMMAND_SCRIPT)
	$(MAKE) $(MALTA_RUNTIME_KERNEL)
	python3 $(MALTA_RUN_COMMAND_SCRIPT) \
	    --malta-dir $(abspath .) \
	    --kernel $(MALTA_RUNTIME_KERNEL) \
	    --log $(NATIVE_PCC_SMOKE_LOG) \
	    --qemu $(MALTA_QEMU) $(MALTA_QEMU_CPU_ARG) \
	    --ram $(MALTA_QEMU_RAM) \
	    --command '/root/native-pcc-smoke.sh && /root/libc-abi-smoke.sh && /root/build-workload-smoke.sh; echo NATIVE_PCC_SMOKE_RC:$$?' \
	    --expect "NATIVE_PCC_SMOKE_RC:0"

native-pcc-smoke-runtime-vr4300:
	$(MAKE) native-pcc-smoke-runtime \
	    MALTA_QEMU=$(MALTA_VR4300_QEMU) \
	    MALTA_QEMU_CPU=$(MALTA_VR4300_CPU) \
	    NATIVE_PCC_SMOKE_LOG=/private/tmp/rebsd-native-pcc-smoke-vr4300.log

native-pcc-regress-stage: $(MIPS_ROOTFS_BUILD_MANIFEST) $(PCC_REGRESS_SCRIPT)
	python3 $(PCC_REGRESS_SCRIPT) native-stage \
	    --rootfs $(abspath $(MIPS_ROOTFS_STAGE)) \
	    --manifest $(abspath $(MIPS_ROOTFS_BUILD_MANIFEST)) \
	    --manifest-out $(NATIVE_PCC_REGRESS_MANIFEST)
	cp $(NATIVE_PCC_REGRESS_MANIFEST) $(MIPS_ROOTFS_BUILD_MANIFEST)

native-pcc-regress-runtime: native-pcc-regress-stage $(MALTA_RUN_COMMAND_SCRIPT)
	$(MAKE) $(MALTA_RUNTIME_KERNEL) \
	    MIPS_ROOTFS_IMAGE_MANIFEST=$(abspath $(NATIVE_PCC_REGRESS_MANIFEST))
	python3 $(MALTA_RUN_COMMAND_SCRIPT) \
	    --malta-dir $(abspath .) \
	    --kernel $(MALTA_RUNTIME_KERNEL) \
	    --log $(NATIVE_PCC_REGRESS_LOG) \
	    --qemu $(MALTA_QEMU) $(MALTA_QEMU_CPU_ARG) \
	    --ram $(MALTA_QEMU_RAM) \
	    --timeout $(NATIVE_PCC_REGRESS_TIMEOUT) \
	    --silence-timeout $(NATIVE_PCC_REGRESS_SILENCE_TIMEOUT) \
	    --command '/root/run-native-pcc-regress.sh; echo NATIVE_PCC_REGRESS_RC:$$?' \
	    --expect "NATIVE_PCC_REGRESS_RC:0"

native-pcc-regress-runtime-vr4300:
	$(MAKE) native-pcc-regress-runtime \
	    MALTA_QEMU=$(MALTA_VR4300_QEMU) \
	    MALTA_QEMU_CPU=$(MALTA_VR4300_CPU) \
	    NATIVE_PCC_REGRESS_LOG=/private/tmp/rebsd-native-pcc-tests-vr4300.log

FORCE:

$(MIPS_ROOTFS_SRC_STAMP): FORCE
	$(MAKE) -C $(TOPSRC)/sys/mips/n64 \
	    N64_USERLAND_COMPILER=$(MIPS_ROOTFS_COMPILER) \
	    rootfs.stage/.userland.$(MIPS_ROOTFS_COMPILER)

$(MIPS_ROOTFS_STAMP): $(MIPS_ROOTFS_SRC_STAMP) $(MIPS_ROOTFS_OVERLAY_FILES)
	rm -rf $(MIPS_ROOTFS_STAGE)
	mkdir -p $(MIPS_ROOTFS_STAGE)
	cp -pR $(MIPS_ROOTFS_SRC)/. $(MIPS_ROOTFS_STAGE)/
	cp -pR $(MIPS_ROOTFS_OVERLAY)/. $(MIPS_ROOTFS_STAGE)/
	rm -rf $(MIPS_ROOTFS_STAGE)/usr/include/machine
	mkdir -p $(MIPS_ROOTFS_STAGE)/usr/include/machine
	for header in $(MIPS_COMMON_MACHINE_HEADERS); do \
	    cp -p $(TOPSRC)/sys/mips/$$header.h \
	        $(MIPS_ROOTFS_STAGE)/usr/include/machine/$$header.h; \
	done
	cp -p $(TOPSRC)/sys/mips/include/machine/types.h \
	    $(MIPS_ROOTFS_STAGE)/usr/include/machine/types.h
	touch $@

$(MIPS_ROOTFS_BUILD_MANIFEST): $(MIPS_ROOTFS_COMMON_MANIFEST) $(MIPS_ROOTFS_BOARD_MANIFEST) $(MIPS_ROOTFS_STAMP) $(MIPS_ROOTFS_EXTRA_STAMPS)
	cp $(MIPS_ROOTFS_COMMON_MANIFEST) $@
	find $(MIPS_ROOTFS_STAGE)/usr/include -type d | sort | while read path; do \
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
	cat $(MIPS_ROOTFS_BOARD_MANIFEST) >> $@
	if [ "$(MIPS_ROOTFS_COMPILER)" = "pcc" ]; then \
	    printf '\nfile /root/linpack-gcc\nmode 0775\n' >> $@; \
	    printf '\nfile /root/linpack-pcc\nmode 0775\n' >> $@; \
	fi

rootfs.img: $(FSUTIL) $(MIPS_ROOTFS_STAMP) $(MIPS_ROOTFS_IMAGE_MANIFEST)
	rm -f $@
	$(FSUTIL) --new --endian=big --size=$(MIPS_ROOTFS_KBYTES) \
	    --manifest=$(MIPS_ROOTFS_IMAGE_MANIFEST) $@ $(MIPS_ROOTFS_STAGE)
	$(FSUTIL) --check $@

rootfs.o: rootfs.img
	$(LD) -r -b binary -o $@ rootfs.img
	$(OBJCOPY) --rename-section .data=.romdisk,alloc,load,readonly,data,contents $@

MIPS_ROOTFS_PATCH_DIR ?= /private/tmp/rebsd-rootfs-patch
MIPS_ROOTFS_PATCH_FILES ?=
MIPS_ROOTFS_PATCH_MANIFEST ?= rootfs.patch.manifest
MIPS_ROOTFS_PATCH_SCRIPT = $(TOPSRC)/sys/mips/tools/rootfs-patch.py

rootfs-patch: $(FSUTIL) $(MIPS_ROOTFS_PATCH_SCRIPT)
	@if [ -z "$(MIPS_ROOTFS_PATCH_FILES)" ]; then \
	    echo "Set MIPS_ROOTFS_PATCH_FILES to paths under MIPS_ROOTFS_PATCH_DIR"; \
	    exit 2; \
	fi
	@if [ ! -d $(MIPS_ROOTFS_STAGE) ] || [ ! -f $(MIPS_ROOTFS_IMAGE_MANIFEST) ]; then \
	    echo "rootfs stage/manifest is missing; build the rootfs once before using rootfs-patch"; \
	    exit 2; \
	fi
	python3 $(MIPS_ROOTFS_PATCH_SCRIPT) \
	    --rootfs $(abspath $(MIPS_ROOTFS_STAGE)) \
	    --manifest $(abspath $(MIPS_ROOTFS_IMAGE_MANIFEST)) \
	    --manifest-out $(abspath $(MIPS_ROOTFS_PATCH_MANIFEST)) \
	    --patch-dir $(abspath $(MIPS_ROOTFS_PATCH_DIR)) \
	    $(MIPS_ROOTFS_PATCH_FILES)
	rm -f rootfs.img
	$(FSUTIL) --new --endian=big --size=$(MIPS_ROOTFS_KBYTES) \
	    --manifest=$(MIPS_ROOTFS_PATCH_MANIFEST) rootfs.img $(MIPS_ROOTFS_STAGE)
	$(FSUTIL) --check rootfs.img

rootfs-repack-kernel:
	@if [ ! -f rootfs.img ]; then \
	    echo "rootfs.img is missing; build the rootfs once before using rootfs-repack-kernel"; \
	    exit 2; \
	fi
	$(LD) -r -b binary -o rootfs.o rootfs.img
	$(OBJCOPY) --rename-section .data=.romdisk,alloc,load,readonly,data,contents rootfs.o
	printf '%s\n' 'const char version[] = "ReBSD for $(MALTA_VERSION_NAME): local build\n";' > vers.c
	${CC} ${VERS_CFLAGS} -MT vers.o -MP -MD -MF .deps/vers.dep -c vers.c -o vers.o
	rm -f unix.elf
	${LD} -nostdlib -T $(MALTA_KERNEL_LD_SCRIPT) ${SYSTEM_OBJ} vers.o -o unix.elf swapunix.o
	${SIZE} unix.elf
	${OBJDUMP} -d -S unix.elf > unix.dis
	${NM} -n unix.elf > unix.nm
ifneq ($(MALTA_PORT64),)
	$(OBJCOPY) -O binary unix.elf unix.bin
	$(MALTA64_STAGE_CC) $(MALTA64_STAGE_ASFLAGS) \
	    ../malta64/stage0_kernel_blob.S -o malta64-stage0-kernel.o
	$(MALTA64_STAGE_LD) $(MALTA64_STAGE_LDFLAGS) \
	    -T ../malta64/stage0_linker.ld \
	    malta64-stage0.o malta64-stage0-kernel.o -o malta64.elf
	$(MALTA64_STAGE_SIZE) malta64.elf
	$(MALTA64_STAGE_OBJDUMP) -d -S malta64.elf > malta64.dis
endif

rootfs-patch-kernel: rootfs-patch
	$(MAKE) rootfs-repack-kernel

ioconf.o: ioconf.c ${SYSTEM_DEP} | machine sys .deps
	${COMPILE_C}

${SYSTEM_OBJ} swapunix.o: | machine sys .deps

$(FSUTIL):
	$(MAKE) -C $(TOPSRC)/tools/fsutil

reconfig ioconf.c swapunix.c: Config ../../../tools/kconfig/kconfig | machine sys
	../../../tools/kconfig/kconfig Config

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

sys_socket.o: $S/kernel/sys_socket.c ${SYSTEM_DEP}
	${COMPILE_C}

uipc_domain.o: $S/kernel/uipc_domain.c ${SYSTEM_DEP}
	${COMPILE_C}

uipc_mbuf.o: $S/kernel/uipc_mbuf.c ${SYSTEM_DEP}
	${COMPILE_C}

uipc_proto.o: $S/kernel/uipc_proto.c ${SYSTEM_DEP}
	${COMPILE_C}

uipc_usrreq.o: $S/kernel/uipc_usrreq.c ${SYSTEM_DEP}
	${COMPILE_C}

uipc_socket.o: $S/kernel/uipc_socket.c ${SYSTEM_DEP}
	${COMPILE_C}

uipc_socket2.o: $S/kernel/uipc_socket2.c ${SYSTEM_DEP}
	${COMPILE_C}

uipc_syscalls.o: $S/kernel/uipc_syscalls.c ${SYSTEM_DEP}
	${COMPILE_C}

af.o: $S/net/af.c ${SYSTEM_DEP}
	${COMPILE_C}

if.o: $S/net/if.c ${SYSTEM_DEP}
	${COMPILE_C}

if_loop.o: $S/net/if_loop.c ${SYSTEM_DEP}
	${COMPILE_C}

if_ether.o: $S/netinet/if_ether.c ${SYSTEM_DEP}
	${COMPILE_C}

raw_cb.o: $S/net/raw_cb.c ${SYSTEM_DEP}
	${COMPILE_C}

raw_usrreq.o: $S/net/raw_usrreq.c ${SYSTEM_DEP}
	${COMPILE_C}

route.o: $S/net/route.c ${SYSTEM_DEP}
	${COMPILE_C}

in.o: $S/netinet/in.c ${SYSTEM_DEP}
	${COMPILE_C}

in_pcb.o: $S/netinet/in_pcb.c ${SYSTEM_DEP}
	${COMPILE_C}

in_proto.o: $S/netinet/in_proto.c ${SYSTEM_DEP}
	${COMPILE_C}

ip_icmp.o: $S/netinet/ip_icmp.c ${SYSTEM_DEP}
	${COMPILE_C}

ip_input.o: $S/netinet/ip_input.c ${SYSTEM_DEP}
	${COMPILE_C}

ip_output.o: $S/netinet/ip_output.c ${SYSTEM_DEP}
	${COMPILE_C}

raw_ip.o: $S/netinet/raw_ip.c ${SYSTEM_DEP}
	${COMPILE_C}

tcp_debug.o: $S/netinet/tcp_debug.c ${SYSTEM_DEP}
	${COMPILE_C}

tcp_input.o: $S/netinet/tcp_input.c ${SYSTEM_DEP}
	${COMPILE_C}

tcp_output.o: $S/netinet/tcp_output.c ${SYSTEM_DEP}
	${COMPILE_C}

tcp_subr.o: $S/netinet/tcp_subr.c ${SYSTEM_DEP}
	${COMPILE_C}

tcp_timer.o: $S/netinet/tcp_timer.c ${SYSTEM_DEP}
	${COMPILE_C}

tcp_usrreq.o: $S/netinet/tcp_usrreq.c ${SYSTEM_DEP}
	${COMPILE_C}

udp_usrreq.o: $S/netinet/udp_usrreq.c ${SYSTEM_DEP}
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

in_cksum.o: $S/mips/common/in_cksum.c ${SYSTEM_DEP}
	${COMPILE_C}

net_glue.o: $S/mips/common/net_glue.c ${SYSTEM_DEP}
	${COMPILE_C}

netinit.o: $S/mips/common/netinit.c ${SYSTEM_DEP}
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

if_ne.o: $S/mips/malta/if_ne.c ${SYSTEM_DEP}
	${COMPILE_C}

if_usbn.o: $S/mips/common/if_usbn.c ${SYSTEM_DEP}
	${COMPILE_C}

if_usbn_fake.o: $S/mips/malta/if_usbn_fake.c ${SYSTEM_DEP}
	${COMPILE_C}


-include .deps/*.dep
