ifeq ($(REBSD_OBJ_BUILD),)
REBSD_TOPSRC_REL = ../..
include ../../mk/obj-dispatch.mk
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
BOARD_KCONFIG_SRCS = $(wildcard $(BOARD_SOURCE_DIR)/Makefile.kconf \
                     $(BOARD_SOURCE_DIR)/files.kconf $(BOARD_SOURCE_DIR)/devices.kconf)
ifeq ($(BOARD_KCONFIG_SRCS),)
BOARD_KCONFIG_SRCS = $(TOPSRC)/sys/mips/Makefile.kconf \
                     $(TOPSRC)/sys/mips/files.kconf \
                     $(TOPSRC)/sys/mips/devices.kconf
endif

.PHONY: all reconfig clean clean-all unix kernel rootfs.img run run-early run-net \
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
        pcc-smoke-all-runtime-vr4300

all: $(BOARD_DIR)/Makefile
	$(MAKE) -C $(BOARD_DIR) all

unix kernel rootfs.img kernel.z64 preflight.z64: $(BOARD_DIR)/Makefile
	$(MAKE) -C $(BOARD_DIR) $@

pcc-debug.z64 pcc-debug-stage pcc-debug-image \
pcc-hw-test-stage pcc-hw-test-image: $(BOARD_DIR)/Makefile
	$(MAKE) -C $(BOARD_DIR) $@

run run-early run-net run-vr4300: $(BOARD_DIR)/Makefile
	$(MAKE) -C $(BOARD_DIR) $@

smoke-as-vr4300 matrix-as-vr4300 smoke-aout-toolchain smoke-host-portablecc pcc-regress-compile pcc-regress-stage pcc-regress-runtime pcc-regress-runtime-vr4300 pcc-regress-summary linpack-smoke-build linpack-smoke-stage linpack-smoke-runtime linpack-smoke-runtime-vr4300 native-pcc-smoke-runtime native-pcc-smoke-runtime-vr4300 native-pcc-regress-stage native-pcc-regress-runtime native-pcc-regress-runtime-vr4300 pcc-perf-baseline pcc-smoke-all-runtime pcc-smoke-all-runtime-vr4300: $(BOARD_DIR)/Makefile
	$(MAKE) -C $(BOARD_DIR) $@

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
