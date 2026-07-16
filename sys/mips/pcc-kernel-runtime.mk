# Integer compiler-runtime helpers needed by freestanding PCC kernels.
#
# PCC emits calls to these routines for 64-bit division and remainder on
# 32-bit MIPS.  Userland obtains them from libpcc.a, while the kernel is
# linked directly with ld and therefore must carry its own soft-float-safe
# copies.

PCC_KERNEL_RUNTIME_DIR = $(TOPSRC)/src/dev/pcc/pcc-libs/libpcc
PCC_KERNEL_RUNTIME_NAMES = divdi3 moddi3 qdivrem udivdi3 umoddi3
PCC_KERNEL_RUNTIME_OBJS = $(addsuffix .o,$(PCC_KERNEL_RUNTIME_NAMES))
PCC_KERNEL_RUNTIME_SRCS = $(addprefix $(PCC_KERNEL_RUNTIME_DIR)/,$(addsuffix .c,$(PCC_KERNEL_RUNTIME_NAMES))) \
                          $(PCC_KERNEL_RUNTIME_DIR)/quad.h

$(PCC_KERNEL_RUNTIME_OBJS): CFLAGS += -Dos_rebsd

$(PCC_KERNEL_RUNTIME_OBJS): %.o: $(PCC_KERNEL_RUNTIME_DIR)/%.c \
    $(PCC_KERNEL_RUNTIME_DIR)/quad.h $(KERNEL_COMPILER_DEPS) | machine sys .deps
	${COMPILE_C}
