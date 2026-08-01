# Integer compiler-runtime helpers needed by freestanding MIPS kernels.
#
# GCC and PCC both emit calls to these routines for 64-bit division and
# remainder on 32-bit MIPS.  Userland obtains them from its compiler runtime,
# while the kernel is linked directly with ld and must carry soft-float-safe
# copies.  The implementation is shared by every MIPS board and compiler.

MIPS_KERNEL_RUNTIME_DIR = $(TOPSRC)/src/dev/pcc/pcc-libs/libpcc
MIPS_KERNEL_RUNTIME_NAMES = divdi3 moddi3 qdivrem udivdi3 umoddi3
MIPS_KERNEL_RUNTIME_OBJS = $(addsuffix .o,$(MIPS_KERNEL_RUNTIME_NAMES))
MIPS_KERNEL_RUNTIME_SRCS = $(addprefix $(MIPS_KERNEL_RUNTIME_DIR)/,$(addsuffix .c,$(MIPS_KERNEL_RUNTIME_NAMES))) \
                          $(MIPS_KERNEL_RUNTIME_DIR)/quad.h

MIPS_KERNEL_RUNTIME_ENDIAN_CPP_big = -DTARGET_BIG_ENDIAN=1
MIPS_KERNEL_RUNTIME_ENDIAN_CPP_little = -DTARGET_LITTLE_ENDIAN=1

ifeq ($(filter $(MIPS_ROOTFS_ENDIAN),big little),)
$(error MIPS kernel runtime requires MIPS_ROOTFS_ENDIAN=big or little)
endif

$(MIPS_KERNEL_RUNTIME_OBJS): CFLAGS += -Dos_rebsd \
    $(MIPS_KERNEL_RUNTIME_ENDIAN_CPP_$(MIPS_ROOTFS_ENDIAN))

$(MIPS_KERNEL_RUNTIME_OBJS): %.o: $(MIPS_KERNEL_RUNTIME_DIR)/%.c \
    $(MIPS_KERNEL_RUNTIME_DIR)/quad.h $(KERNEL_COMPILER_DEPS) | machine sys .deps
	${COMPILE_C}
