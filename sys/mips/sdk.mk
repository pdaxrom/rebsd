#
# Standalone ReBSD/MIPS PCC cross SDK builder.
#
# Examples:
#   make -C sys/mips -f sdk.mk cross-pcc-sdk CPU=vr4300 FLOAT=soft
#   make -C sys/mips -f sdk.mk cross-pcc-sdk \
#       MIPS_SDK_CPU=mips32r2 MIPS_SDK_FLOAT=hard MIPS_SDK_PREFIX=/tmp/cross-pcc
#

MIPS_SDK_MAKEFILE := $(lastword $(MAKEFILE_LIST))
MIPS_SDK_DIR := $(dir $(abspath $(MIPS_SDK_MAKEFILE)))
TOPSRC ?= $(abspath $(MIPS_SDK_DIR)/../..)

MIPS_SDK_CPU ?= $(if $(CPU),$(CPU),vr4300)
MIPS_SDK_FLOAT ?= $(if $(FLOAT),$(FLOAT),hard)
MIPS_SDK_ENDIAN ?= $(if $(ENDIAN),$(ENDIAN),big)
MIPS_SDK_CPUS = vr4300 mips32r2
MIPS_SDK_FLOATS = hard soft
MIPS_SDK_ENDIANS = big little

ifeq ($(filter $(MIPS_SDK_CPU),$(MIPS_SDK_CPUS)),)
$(error Unsupported MIPS_SDK_CPU=$(MIPS_SDK_CPU); expected one of $(MIPS_SDK_CPUS))
endif
ifeq ($(filter $(MIPS_SDK_FLOAT),$(MIPS_SDK_FLOATS)),)
$(error Unsupported MIPS_SDK_FLOAT=$(MIPS_SDK_FLOAT); expected one of $(MIPS_SDK_FLOATS))
endif
ifeq ($(filter $(MIPS_SDK_ENDIAN),$(MIPS_SDK_ENDIANS)),)
$(error Unsupported MIPS_SDK_ENDIAN=$(MIPS_SDK_ENDIAN); expected one of $(MIPS_SDK_ENDIANS))
endif
ifneq ($(MIPS_SDK_ENDIAN),big)
$(error MIPS_SDK_ENDIAN=$(MIPS_SDK_ENDIAN) is reserved for the future mipsel port; current mips-rebsd PCC is big-endian only)
endif

MIPS_SDK_ABI = $(MIPS_SDK_ENDIAN).$(MIPS_SDK_CPU).$(MIPS_SDK_FLOAT)
MIPS_SDK_BUILD ?= /private/tmp/rebsd-cross-pcc-build-$(MIPS_SDK_ABI)
MIPS_SDK_PREFIX ?= $(TOPSRC)/cross-pcc

MIPS_ROOTFS_COMPILER = pcc
MIPS_ROOTFS_CPU = $(MIPS_SDK_CPU)
MIPS_ROOTFS_FLOAT = $(MIPS_SDK_FLOAT)
MIPS_ROOTFS_ENDIAN = $(MIPS_SDK_ENDIAN)
MIPS_ROOTFS_TARGET_PLATFORM = mips
MIPS_ROOTFS_STAGE = $(MIPS_SDK_BUILD)/sysroot
MIPS_ROOTFS_BUILD_MANIFEST = $(MIPS_SDK_BUILD)/rootfs.generated.manifest
MIPS_ROOTFS_IMAGE_MANIFEST = $(MIPS_ROOTFS_BUILD_MANIFEST)
MIPS_ROOTFS_USER_LDSCRIPT = $(MIPS_SDK_BUILD)/mips-user.ld
MIPS_ROOTFS_USER_LDSCRIPT_SRC = $(TOPSRC)/sys/mips/user/user.ld.S
MIPS_ROOTFS_USERLAND_STAMP_PREFIX = $(MIPS_SDK_BUILD)/mips-userland

MIPS_NATIVE_TOOLS = $(MIPS_SDK_BUILD)/host-tools
MIPS_NATIVE_DIR = $(MIPS_SDK_BUILD)/runtime.$(MIPS_SDK_ABI)
MIPS_NATIVE_TREE = $(MIPS_NATIVE_DIR)/tree
MIPS_NATIVE_PCC_BUILD = $(MIPS_SDK_BUILD)/target-pcc-build.$(MIPS_SDK_ABI)
MIPS_NATIVE_PCC_DIR = $(MIPS_SDK_BUILD)/target-pcc.$(MIPS_SDK_ABI)

MIPS_PCC_PROVIDER = cross
MIPS_PCC_HOST_BUILD = $(MIPS_SDK_BUILD)/host-pcc-build.$(MIPS_SDK_ABI)
MIPS_PCC_HOST_PREFIX = $(abspath $(MIPS_SDK_PREFIX))

include $(TOPSRC)/sys/mips/rootfs.mk

.PHONY: cross-pcc-sdk cross-pcc-sdk-tools cross-pcc-sdk-runtime \
        clean-cross-pcc-sdk

cross-pcc-sdk: cross-pcc-sdk-runtime
	@echo "cross-pcc SDK: $(MIPS_PCC_HOST_PREFIX)"
	@echo "cpu=$(MIPS_SDK_CPU) float=$(MIPS_SDK_FLOAT) endian=$(MIPS_SDK_ENDIAN)"

cross-pcc-sdk-tools: $(MIPS_HOST_PCC)
	@test -x $(MIPS_CROSS_PCC_AOUT)

cross-pcc-sdk-runtime: $(MIPS_NATIVE_STAMP)
	@test -f $(MIPS_CROSS_PCC_LIB)/libpcc.a
	@test -f $(MIPS_CROSS_PCC_SOFTFLOAT_LIB)/libpcc.a

clean-cross-pcc-sdk:
	rm -rf $(MIPS_SDK_BUILD)
