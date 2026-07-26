ifeq ($(REBSD_OBJ_BUILD),)
# Each source-tree goal dispatches a separate recursive make into the same
# board object directory.  Serialize those dispatches even when the caller
# uses -j; parallelism remains enabled inside each isolated object build.
.NOTPARALLEL:
ifneq ($(filter rootfs-board-matrix,$(MAKECMDGOALS)),)
.PHONY: rootfs-board-matrix
rootfs-board-matrix:
	python3 $(abspath ../..)/sys/mips/tools/rootfs-board-matrix.py \
	    --top $(abspath ../..) \
	    --out $(if $(ROOTFS_MATRIX_OUT),$(abspath $(ROOTFS_MATRIX_OUT)),/private/tmp/rebsd-rootfs-matrix) \
	    --jobs $(if $(ROOTFS_MATRIX_JOBS),$(ROOTFS_MATRIX_JOBS),4)
else
REBSD_TOPSRC_REL = ../..
include ../../mk/obj-dispatch.mk
endif
else

TOPSRC          = ../..
BOARD           ?= malta
BOARD_DIR       = $(BOARD)
BOARD_SOURCE_DIR = $(TOPSRC)/sys/mips/$(BOARD)
KCONFIG_DIR     = $(TOPSRC)/tools/kconfig
KCONFIG         = $(OBJTOP)/tools/kconfig/kconfig
KCONFIG_SRCS    = $(KCONFIG_DIR)/Makefile $(KCONFIG_DIR)/config.h \
                  $(KCONFIG_DIR)/config.y $(KCONFIG_DIR)/lang.l \
                  $(KCONFIG_DIR)/main.c $(KCONFIG_DIR)/mkioconf.c \
                  $(KCONFIG_DIR)/mkmakefile.c $(KCONFIG_DIR)/mkswapconf.c
BOARD_KCONFIG_SRCS = $(TOPSRC)/sys/mips/Makefile.kconf \
                     $(TOPSRC)/sys/mips/files.kconf \
                     $(TOPSRC)/sys/mips/devices.kconf \
                     $(wildcard $(BOARD_SOURCE_DIR)/Makefile.kconf \
                     $(BOARD_SOURCE_DIR)/files.kconf \
                     $(BOARD_SOURCE_DIR)/files.* \
                     $(BOARD_SOURCE_DIR)/devices.kconf \
                     $(BOARD_SOURCE_DIR)/devices.*)

.PHONY: all reconfig clean clean-all unix kernel kernel-objects rootfs.img run run-early run-net \
	    run-vr4300 kernel.z64 preflight.z64 \
	    pcc-debug.z64 pcc-debug-stage pcc-debug-image \
	    pcc-hw-test-stage pcc-hw-test-image \
	    cross-pcc-sdk cross-pcc-sdk-tools clean-cross-pcc-sdk \
        smoke-as-vr4300 matrix-as-vr4300 smoke-aout-toolchain \
        smoke-host-portablecc \
        pcc-regress-compile pcc-regress-stage pcc-regress-runtime \
        pcc-regress-runtime-vr4300 pcc-regress-summary \
        linpack-smoke-build linpack-smoke-stage linpack-smoke-runtime \
        linpack-smoke-runtime-vr4300 native-pcc-smoke-runtime \
        native-pcc-smoke-runtime-vr4300 native-pcc-regress-stage \
        native-pcc-regress-runtime native-pcc-regress-runtime-vr4300 \
        pcc-perf-baseline pcc-smoke-all-runtime \
        pcc-smoke-all-runtime-vr4300 vm-smoke-runtime vm-stress-runtime \
        vm-pressure-runtime vm-pressure-smoke-build \
        vm-pressure-smoke-stage vm-pressure-smoke-runtime \
	    vm-diagnostics-pcc-runtime \
	    vm-io-smoke-runtime vm-ne2k-smoke-runtime \
	    rootfs-contract-check rootfs-boot-smoke-runtime rootfs-board-matrix \
	    rootfs-patch rootfs-repack-kernel \
	    rootfs-patch-kernel

all: $(BOARD_DIR)/Makefile
	$(MAKE) -C $(BOARD_DIR) all

unix kernel kernel-objects rootfs.img kernel.z64 preflight.z64: $(BOARD_DIR)/Makefile
	$(MAKE) -C $(BOARD_DIR) $@

pcc-debug.z64 pcc-debug-stage pcc-debug-image \
pcc-hw-test-stage pcc-hw-test-image: $(BOARD_DIR)/Makefile
	$(MAKE) -C $(BOARD_DIR) $@

run run-early run-net run-vr4300: $(BOARD_DIR)/Makefile
	$(MAKE) -C $(BOARD_DIR) $@

smoke-as-vr4300 matrix-as-vr4300 smoke-aout-toolchain smoke-host-portablecc pcc-regress-compile pcc-regress-stage pcc-regress-runtime pcc-regress-runtime-vr4300 pcc-regress-summary linpack-smoke-build linpack-smoke-stage linpack-smoke-runtime linpack-smoke-runtime-vr4300 native-pcc-smoke-runtime native-pcc-smoke-runtime-vr4300 native-pcc-regress-stage native-pcc-regress-runtime native-pcc-regress-runtime-vr4300 pcc-perf-baseline pcc-smoke-all-runtime pcc-smoke-all-runtime-vr4300 vm-smoke-runtime vm-stress-runtime vm-pressure-runtime vm-pressure-smoke-build vm-pressure-smoke-stage vm-pressure-smoke-runtime vm-diagnostics-pcc-runtime vm-io-smoke-runtime vm-ne2k-smoke-runtime rootfs-contract-check rootfs-boot-smoke-runtime rootfs-patch rootfs-repack-kernel rootfs-patch-kernel: $(BOARD_DIR)/Makefile
	$(MAKE) -C $(BOARD_DIR) $@

rootfs-board-matrix:
	python3 $(TOPSRC)/sys/mips/tools/rootfs-board-matrix.py \
	    --top $(abspath $(TOPSRC)) \
	    --out $(if $(ROOTFS_MATRIX_OUT),$(abspath $(ROOTFS_MATRIX_OUT)),/private/tmp/rebsd-rootfs-matrix) \
	    --jobs $(if $(ROOTFS_MATRIX_JOBS),$(ROOTFS_MATRIX_JOBS),4)

cross-pcc-sdk cross-pcc-sdk-tools clean-cross-pcc-sdk:
	$(MAKE) -f sdk.mk $@

reconfig: $(KCONFIG)
	mkdir -p $(BOARD_DIR)
	cd $(BOARD_DIR) && $(KCONFIG) $(TOPSRC)/sys/mips/$(BOARD)/Config

clean:
	if [ -f $(BOARD_DIR)/Makefile ]; then $(MAKE) -C $(BOARD_DIR) clean; fi
	rm -rf $(BOARD_DIR)/*.o $(BOARD_DIR)/*.elf $(BOARD_DIR)/*.bin \
	    $(BOARD_DIR)/*.img $(BOARD_DIR)/*.dis $(BOARD_DIR)/*.nm \
	    $(BOARD_DIR)/a.out $(BOARD_DIR)/.deps $(BOARD_DIR)/machine \
	    $(BOARD_DIR)/sys

clean-all:
	if [ -f $(BOARD_DIR)/Makefile ]; then $(MAKE) -C $(BOARD_DIR) clean-all; fi
	rm -f $(BOARD_DIR)/Makefile $(BOARD_DIR)/ioconf.c $(BOARD_DIR)/swap*.c
	rm -rf $(BOARD_DIR)/*.o $(BOARD_DIR)/*.elf $(BOARD_DIR)/*.bin \
	    $(BOARD_DIR)/*.img $(BOARD_DIR)/*.dis $(BOARD_DIR)/*.nm \
	    $(BOARD_DIR)/a.out $(BOARD_DIR)/.deps $(BOARD_DIR)/machine \
	    $(BOARD_DIR)/sys $(BOARD_DIR)/vers.c

$(BOARD_DIR)/Makefile: $(BOARD_SOURCE_DIR)/Config $(BOARD_KCONFIG_SRCS) $(KCONFIG)
	mkdir -p $(BOARD_DIR)
	cd $(BOARD_DIR) && $(KCONFIG) $(TOPSRC)/sys/mips/$(BOARD)/Config

$(KCONFIG): $(KCONFIG_SRCS)
	$(MAKE) -C $(KCONFIG_DIR)

endif
