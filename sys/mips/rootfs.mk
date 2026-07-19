#
# Shared MIPS rootfs, userland, and PCC runtime rules.
#
# Board makefiles must set TOPSRC before including this file.  They may
# override MIPS_ROOTFS_BOARD_DIR, MIPS_ROOTFS_BOARD_MANIFEST,
# MIPS_ROOTFS_BOARD_CMD_SUBDIRS, and MIPS_ROOTFS_USER_LDSCRIPT_SRC.
#

MIPS_ROOTFS_MAKEFILE := $(lastword $(MAKEFILE_LIST))
MIPS_ROOTFS_COMPILER ?= gcc
MIPS_ROOTFS_COMPILERS = gcc pcc
ifeq ($(filter $(MIPS_ROOTFS_COMPILER),$(MIPS_ROOTFS_COMPILERS)),)
$(error Unsupported MIPS_ROOTFS_COMPILER=$(MIPS_ROOTFS_COMPILER); expected one of $(MIPS_ROOTFS_COMPILERS))
endif

MIPS_ROOTFS_CPU ?= mips32r2
MIPS_ROOTFS_CPUS = vr4300 mips32r2
ifeq ($(filter $(MIPS_ROOTFS_CPU),$(MIPS_ROOTFS_CPUS)),)
$(error Unsupported MIPS_ROOTFS_CPU=$(MIPS_ROOTFS_CPU); expected one of $(MIPS_ROOTFS_CPUS))
endif

MIPS_ROOTFS_FLOAT ?= hard
MIPS_ROOTFS_FLOATS = hard soft
ifeq ($(filter $(MIPS_ROOTFS_FLOAT),$(MIPS_ROOTFS_FLOATS)),)
$(error Unsupported MIPS_ROOTFS_FLOAT=$(MIPS_ROOTFS_FLOAT); expected one of $(MIPS_ROOTFS_FLOATS))
endif

MIPS_ROOTFS_ENDIAN ?= big
MIPS_ROOTFS_ENDIANS = big little
ifeq ($(filter $(MIPS_ROOTFS_ENDIAN),$(MIPS_ROOTFS_ENDIANS)),)
$(error Unsupported MIPS_ROOTFS_ENDIAN=$(MIPS_ROOTFS_ENDIAN); expected one of $(MIPS_ROOTFS_ENDIANS))
endif
MIPS_ROOTFS_EXEC_FORMAT ?= elf
MIPS_ROOTFS_EXEC_FORMATS = aout elf
ifeq ($(filter $(MIPS_ROOTFS_EXEC_FORMAT),$(MIPS_ROOTFS_EXEC_FORMATS)),)
$(error Unsupported MIPS_ROOTFS_EXEC_FORMAT=$(MIPS_ROOTFS_EXEC_FORMAT); expected one of $(MIPS_ROOTFS_EXEC_FORMATS))
endif
MIPS_ROOTFS_NATIVE_PCC ?= 1
MIPS_ROOTFS_ABI_VARIANT ?=
MIPS_ROOTFS_ABI = $(MIPS_ROOTFS_ENDIAN).$(MIPS_ROOTFS_CPU).$(MIPS_ROOTFS_FLOAT).$(MIPS_ROOTFS_EXEC_FORMAT)$(if $(MIPS_ROOTFS_ABI_VARIANT),.$(MIPS_ROOTFS_ABI_VARIANT),)
OBJTOP ?= $(TOPSRC)
TOPOBJ ?= $(OBJTOP)
MIPS_BUILD_TOOL_DIR ?= $(OBJTOP)/tools
MIPS_BUILD_SRC_DIR ?= $(OBJTOP)/src
MIPS_BUILD_TEST_DIR ?= $(TOPOBJ)/tests
MIPS_BUILD_TOOLCHAIN_DIR ?= $(TOPOBJ)/toolchain
MIPS_ROOTFS_ENDIAN_FLAG_big = -EB
MIPS_ROOTFS_ENDIAN_FLAG_little = -EL
MIPS_ROOTFS_ENDIAN_FLAG = $(MIPS_ROOTFS_ENDIAN_FLAG_$(MIPS_ROOTFS_ENDIAN))
MIPS_ROOTFS_ENDIAN_CPP_big = -DTARGET_BIG_ENDIAN
MIPS_ROOTFS_ENDIAN_CPP_little = -DTARGET_LITTLE_ENDIAN
MIPS_ROOTFS_ENDIAN_CPP = $(MIPS_ROOTFS_ENDIAN_CPP_$(MIPS_ROOTFS_ENDIAN))
MIPS_ROOTFS_ENDIAN_CPP1_big = -DTARGET_BIG_ENDIAN=1
MIPS_ROOTFS_ENDIAN_CPP1_little = -DTARGET_LITTLE_ENDIAN=1
MIPS_ROOTFS_ENDIAN_CPP1 = $(MIPS_ROOTFS_ENDIAN_CPP1_$(MIPS_ROOTFS_ENDIAN))
MIPS_ROOTFS_TOOLCHAIN_CPP_aout =
MIPS_ROOTFS_TOOLCHAIN_CPP_elf = -DREBSD_TOOLCHAIN_ELF_DEFAULT
MIPS_ROOTFS_TOOLCHAIN_CPP ?= $(MIPS_ROOTFS_TOOLCHAIN_CPP_$(MIPS_ROOTFS_EXEC_FORMAT))
MIPS_ROOTFS_EXTRA_CPPFLAGS ?=
MIPS_ROOTFS_LD_EMULATION_big = elf32ebmip
MIPS_ROOTFS_LD_EMULATION_little = elf32elmip
MIPS_ROOTFS_LD_EMULATION = $(MIPS_ROOTFS_LD_EMULATION_$(MIPS_ROOTFS_ENDIAN))
MIPS_ROOTFS_FLOAT_FLAG_hard = -mhard-float
MIPS_ROOTFS_FLOAT_FLAG_soft = -msoft-float
MIPS_ROOTFS_FLOAT_FLAG = $(MIPS_ROOTFS_FLOAT_FLAG_$(MIPS_ROOTFS_FLOAT))
MIPS_ROOTFS_ARCH_vr4300 = $(MIPS_ROOTFS_ENDIAN_FLAG) -march=vr4300 \
                         -mtune=vr4300 -mips3 -mabi=32 \
                         $(MIPS_ROOTFS_FLOAT_FLAG)
MIPS_ROOTFS_ARCH_mips32r2 = $(MIPS_ROOTFS_ENDIAN_FLAG) \
                           -march=mips32r2 -mips32r2 -mtune=24kf \
                           -mabi=32 $(MIPS_ROOTFS_FLOAT_FLAG)
MIPS_ROOTFS_AS_CPU_vr4300 = -march=vr4300
MIPS_ROOTFS_AS_CPU_mips32r2 = -march=mips32r2
MIPS_ROOTFS_ARCH = $(MIPS_ROOTFS_ARCH_$(MIPS_ROOTFS_CPU))
MIPS_ROOTFS_AS_CPU = $(MIPS_ROOTFS_AS_CPU_$(MIPS_ROOTFS_CPU))
MIPS_ROOTFS_CODE ?= $(if $(N64_CODE),$(N64_CODE),$(MIPS_CODE))
MIPS_ROOTFS_GCC_PREFIX ?= $(if $(N64_PREFIX),$(N64_PREFIX),$(MIPS_PREFIX))

MIPS_ROOTFS_KBYTES ?= $(if $(filter 1,$(MIPS_ROOTFS_NATIVE_PCC)),32768,$(if $(filter pcc,$(MIPS_ROOTFS_COMPILER)),32768,16384))
MIPS_ROOTFS_COMMON_DIR ?= $(TOPSRC)/sys/mips/rootfs
MIPS_ROOTFS_BOARD_DIR ?=
MIPS_ROOTFS_MANIFEST ?= $(TOPSRC)/sys/mips/rootfs.manifest
MIPS_ROOTFS_BOARD_MANIFEST ?=
MIPS_ROOTFS_BUILD_MANIFEST ?= rootfs.generated.manifest
MIPS_ROOTFS_IMAGE_MANIFEST ?= $(MIPS_ROOTFS_BUILD_MANIFEST)
MIPS_ROOTFS_IMAGE_DEPS ?= $(MIPS_ROOTFS_BUILD_MANIFEST) $(MIPS_ROOTFS_USER_STAMP)
MIPS_ROOTFS_IMAGE_STAGE ?= $(MIPS_ROOTFS_STAGE)
MIPS_ROOTFS_IMAGE_KBYTES ?= $(MIPS_ROOTFS_KBYTES)
MIPS_ROOTFS_STAGE ?= rootfs.stage
MIPS_ROOTFS_STAMP = $(MIPS_ROOTFS_USER_STAMP)
MIPS_ROOTFS_USR_BIN = $(MIPS_ROOTFS_STAGE)/usr/bin
MIPS_ROOTFS_USR_LIB = $(MIPS_ROOTFS_STAGE)/usr/lib
MIPS_ROOTFS_USR_LIBEXEC = $(MIPS_ROOTFS_STAGE)/usr/libexec
MIPS_ROOTFS_USR_INCLUDE = $(MIPS_ROOTFS_STAGE)/usr/include
MIPS_ROOTFS_USR_SHARE = $(MIPS_ROOTFS_STAGE)/usr/share
MIPS_ROOTFS_BASE_STAMP = $(MIPS_ROOTFS_STAGE)/.base
MIPS_ROOTFS_USER_STAMP = $(MIPS_ROOTFS_STAGE)/.userland.$(MIPS_ROOTFS_COMPILER).$(MIPS_ROOTFS_ABI)
MIPS_ROOTFS_USER_COMPAT_STAMP = $(MIPS_ROOTFS_STAGE)/.userland
MIPS_ROOTFS_WHATIS = $(MIPS_ROOTFS_USR_SHARE)/man/whatis
MIPS_PCC_HOST_INCLUDE ?= $(MIPS_ROOTFS_USR_INCLUDE)
MIPS_PCC_HOST_INCLUDE_STAMP ?= $(MIPS_ROOTFS_BASE_STAMP)
MIPS_ROOTFS_TARGET_PLATFORM ?= mips
MIPS_ROOTFS_USER_LDSCRIPT ?= mips-user.ld
MIPS_ROOTFS_USER_LDSCRIPT_SRC ?= $(TOPSRC)/sys/mips/user/user.ld.S
MIPS_ROOTFS_LDSCRIPTS_DIR ?= ldscripts
MIPS_ROOTFS_INSTALLED_LDSCRIPT_big = $(MIPS_ROOTFS_LDSCRIPTS_DIR)/elf32-bigmips.ld
MIPS_ROOTFS_INSTALLED_LDSCRIPT_little = $(MIPS_ROOTFS_LDSCRIPTS_DIR)/elf32-littlemips.ld
MIPS_ROOTFS_INSTALLED_LDSCRIPT ?= $(MIPS_ROOTFS_INSTALLED_LDSCRIPT_$(MIPS_ROOTFS_ENDIAN))
MIPS_ROOTFS_INSTALLED_LDSCRIPT_PATH = /usr/lib/$(MIPS_ROOTFS_INSTALLED_LDSCRIPT)
MIPS_ROOTFS_ELF2AOUT ?= $(MIPS_BUILD_TOOL_DIR)/elf2aout/elf2aout
MIPS_ROOTFS_ELF2AOUT_SRCS = $(TOPSRC)/tools/elf2aout/Makefile \
                            $(TOPSRC)/tools/elf2aout/elf2aout.c
MIPS_ROOTFS_EXEC_FORMAT_DEPS_aout = $(MIPS_ROOTFS_ELF2AOUT)
MIPS_ROOTFS_EXEC_FORMAT_DEPS_elf =
MIPS_ROOTFS_EXEC_FORMAT_DEPS = $(MIPS_ROOTFS_EXEC_FORMAT_DEPS_$(MIPS_ROOTFS_EXEC_FORMAT))
MIPS_ROOTFS_USERLAND_STAMP_PREFIX ?= mips-userland
MIPS_ROOTFS_USERLAND_STAMP = $(MIPS_ROOTFS_USERLAND_STAMP_PREFIX).$(MIPS_ROOTFS_COMPILER).$(MIPS_ROOTFS_ABI).stamp
MIPS_ROOTFS_CLEAN_ARTIFACTS = $(MIPS_ROOTFS_STAGE) \
                              $(MIPS_ROOTFS_BUILD_MANIFEST) \
                              rootfs.*.manifest rootfs.img rootfs.o \
                              $(MIPS_ROOTFS_USER_LDSCRIPT) \
                              $(MIPS_ROOTFS_USERLAND_STAMP_PREFIX)*.stamp \
                              mips-userland*.stamp n64-userland*.stamp \
                              $(MIPS_NATIVE_TOOLS) $(MIPS_NATIVE_DIR) \
                              $(MIPS_NATIVE_PCC_BUILD) $(MIPS_NATIVE_PCC_DIR) \
                              mips-native-runtime* mips-native-tools \
                              mips-native-pcc-build* mips-native-pcc* \
                              n64-native-runtime* n64-native-tools \
                              n64-native-pcc-build* n64-native-pcc* \
                              n64-linpack-gcc-runtime*

MIPS_ROOTFS_FILES = $(shell find $(MIPS_ROOTFS_COMMON_DIR) \
                   $(MIPS_ROOTFS_BOARD_DIR) -type f 2>/dev/null)
MIPS_ROOTFS_DIRS = $(shell find $(MIPS_ROOTFS_COMMON_DIR) \
                  $(MIPS_ROOTFS_BOARD_DIR) -type d 2>/dev/null)
MIPS_NET_INCLUDE_SRCS = $(shell find $(TOPSRC)/sys/net $(TOPSRC)/sys/netinet \
                   -maxdepth 1 -type f -name '*.h' 2>/dev/null)
MIPS_NETINET_USER_HEADERS = $(filter-out $(TOPSRC)/sys/netinet/in.h,$(wildcard $(TOPSRC)/sys/netinet/*.h))
MIPS_INCLUDE_SRCS = $(shell find $(TOPSRC)/include $(TOPSRC)/sys/include \
                   $(TOPSRC)/sys/mips/include $(TOPSRC)/sys/mips/n64/include \
                   -type f -name '*.h' \
                   2>/dev/null) \
                   $(wildcard $(TOPSRC)/sys/mips/*.h \
                   $(TOPSRC)/sys/mips/n64/*.h) \
                   $(MIPS_NET_INCLUDE_SRCS)
MIPS_INCLUDE_LINKS = $(shell find $(TOPSRC)/include -maxdepth 1 -type l \
                    2>/dev/null)
MIPS_COMMON_MACHINE_HEADERS ?= console cpu devmajors elf_machdep float fpu io \
                               layout limits machparam ramswap romdisk
MIPS_BOARD_MACHINE_HEADER_DIR ?=
MIPS_BOARD_INCLUDE_DIR ?=

MIPS_UTILITY_SMOKE_SRCS = $(TOPSRC)/src/cmd/basename.c \
                         $(TOPSRC)/src/cmd/sum.c \
                         $(TOPSRC)/src/cmd/size.c \
                         $(TOPSRC)/src/cmd/aoutio.c \
                         $(TOPSRC)/src/cmd/aoutio.h \
                         $(TOPSRC)/src/cmd/elf32_mips.h
MIPS_TERMCAP = $(TOPSRC)/src/libtermlib/termcap/termcap.small
MIPS_MAKEWHATIS_SED = $(TOPSRC)/src/man/makewhatis.sed
MIPS_AWK_SRCS = $(shell find $(TOPSRC)/src/cmd/awk -type f \( -name '*.[chly]' -o -name Makefile -o -name tokenscript \) 2>/dev/null)
MIPS_BISON ?= $(if $(wildcard /opt/homebrew/opt/bison/bin/bison),/opt/homebrew/opt/bison/bin/bison,bison)
MIPS_YACC ?= byacc
MIPS_SRC_LIBS ?= libc libm libutil libtermlib libcurses libvmf libreadline libtcl
MIPS_SRC_SUBDIRS ?= cmd
MIPS_CMD_NONE = __mips_none__
MIPS_BOARD_CMD_SUBDIRS ?=
MIPS_BOARD_USR_BIN_FILES ?=
MIPS_CMD_SUBDIRS ?= basic calendar chown chroot compress date2 deco dhclient diff emg env \
                  fdisk find fold forth fsck fsck.fat fstat getty gpt hostname id ifconfig inetd init \
                  aout ar as ld login ls make man md5 med mkfs mkfs.fat mknod \
                  mkpasswd mount more netstat nm pdc picoc ping printf pstat ptytest \
                  ranlib reboot renice retroforth route sed setty \
                  sh shutdown sl smux stty sysctl tcl telnet \
                  telnetd test wget umount uname xargs
MIPS_CMD_SUBDIRS += $(MIPS_BOARD_CMD_SUBDIRS)
MIPS_CMD_STDS ?= basename cal cat cb chgrp chmod cmp col comm cp dd diskspeed \
               du echo ed fgrep file free grep head hostid iostat join kill last ln \
               mesg mkdir mv nice od off64-smoke-gcc pagesize pr printenv ps pwd rev rm rmail \
               rmdir size sleep sort split strace sum sync tail tar tee time touch vmstat \
               top tr tsort tty uniq uptime vm-pressure-smoke w wc whereis who
MIPS_CMD_NSTDS ?= egrep expr
MIPS_CMD_OPERATORS ?= df
MIPS_CMD_SCRIPTS ?= false nohup true
MIPS_CMD_EXTRA_SUBDIRS ?= deco ptytest tcl $(MIPS_BOARD_CMD_SUBDIRS)
MIPS_STB_DIR ?=
MIPS_STB_SRCS = $(wildcard $(MIPS_STB_DIR)/stb_image.h)
MIPS_USR_BIN_FILES ?= aout apropos ar as awk basename basic cal calendar cb \
                    chgrp cmp col comm compress deco diff diskspeed du ed \
                    egrep emg env fgrep file find fold forth free grep groups head \
                    hostid id iostat join last ld man matrix-as-vr4300 \
                    matrix-as-vr4300.sh make md5 med mesg more nice nm nohup \
                    od off64-smoke-gcc pagesize pdc picoc pr printf printenv ps ptytest \
                    ranlib renice renumber retroforth rev rmail \
                    setty size sl smux smoke-as-vr4300 smoke-as-vr4300.sh \
                    sort split strace strip sum sysctl tail tar tcl tee telnet time \
                    top touch tsort tty uncompress uniq uptime vm-pressure-smoke vmstat w wc \
                    wget whatis whereis who whoami xargs zcat \
                    $(MIPS_BOARD_USR_BIN_FILES)
MIPS_USR_LIBEXEC_FILES ?= bigram code
MIPS_ROOTFS_CAT1_PAGES ?= apropos awk basename cal cat cb chgrp chmod cmp col \
                        comm compress cp date dd df diff du echo ed expr \
                        false file find fold free grep head hostid iostat join kill last \
                        ln login ls make man mesg mkdir more mv nice od \
                        pagesize pr ps pcc printenv pwd rev rm rmail rmdir \
                        sed sh size sleep sort split strip sum tail tar tee \
                        time top touch tr true tsort tty uniq uptime vmstat w \
                        wc whatis who
MIPS_ROOTFS_CMD_CAT1_SOURCES ?= as:as emg:emg env:env nm:nm sl:sl wget:wget
MIPS_ROOTFS_BOARD_CMD_CAT1_SOURCES ?=
MIPS_ROOTFS_CAT1_ALIASES ?= egrep:grep fgrep:grep uncompress:compress \
                          zcat:compress nohup:nice cc:pcc cpp:pcc
MIPS_ROOTFS_CAT8_PAGES ?= fsck fstat getty sync
MIPS_ROOTFS_CAT8_ALIASES ?= fsck.ufs:fsck mkfs.ufs:mkfs fastboot:reboot halt:reboot poweroff:reboot \
                          bootloader:reboot
MIPS_USER_SRCS = $(TOPSRC)/target.mk $(TOPSRC)/target-mips.mk \
                $(TOPSRC)/target-n64.mk \
                $(TOPSRC)/src/Makefile $(TOPSRC)/src/cmd/Makefile \
                $(shell find $(TOPSRC)/src/cmd -maxdepth 3 -type f \( -name '*.[chSy]' -o -name '*.[0-9]' -o -name '*.sh' -o -name Makefile \) 2>/dev/null) \
                $(MIPS_STB_SRCS)
MIPS_LIBUTIL_SRCS = $(shell find $(TOPSRC)/src/libutil -type f \( -name '*.[chS]' -o -name Makefile \) 2>/dev/null)
MIPS_LIBTERMLIB_SRCS = $(shell find $(TOPSRC)/src/libtermlib -type f \( -name '*.[chS]' -o -name Makefile -o -name 'termcap.*' \) 2>/dev/null)
MIPS_LIBCURSES_SRCS = $(shell find $(TOPSRC)/src/libcurses -type f \( -name '*.[chS]' -o -name Makefile \) 2>/dev/null)
MIPS_LIBVMF_SRCS = $(shell find $(TOPSRC)/src/libvmf -type f \( -name '*.[chS]' -o -name Makefile -o -name '*.3' \) 2>/dev/null)
MIPS_LIBREADLINE_SRCS = $(shell find $(TOPSRC)/src/libreadline -type f \( -name '*.[chS]' -o -name Makefile \) 2>/dev/null)
MIPS_LIBTCL_SRCS = $(shell find $(TOPSRC)/src/libtcl -type f \( -name '*.[chS]' -o -name Makefile \) 2>/dev/null)
MIPS_LIBC_SRCS = $(shell find $(TOPSRC)/src/libc -type f \( -name '*.[chS]' -o -name '*.inc' -o -name Makefile \) 2>/dev/null)
MIPS_LIBM_SRCS = $(shell find $(TOPSRC)/src/libm -type f \( -name '*.[chS]' -o -name Makefile \) 2>/dev/null)

MIPS_NATIVE_DIR ?= mips-native-runtime.$(MIPS_ROOTFS_ABI)
MIPS_NATIVE_TREE = $(MIPS_NATIVE_DIR)/tree
MIPS_NATIVE_SOFTFLOAT_DIR = $(MIPS_NATIVE_DIR)/softfloat
MIPS_NATIVE_TOOLS ?= mips-native-tools
MIPS_NATIVE_HOST_INCLUDE = $(MIPS_NATIVE_TOOLS)/include
MIPS_NATIVE_HOST_INCLUDE_STAMP = $(MIPS_NATIVE_HOST_INCLUDE)/.stamp
MIPS_NATIVE_AS = $(MIPS_NATIVE_TOOLS)/as
MIPS_NATIVE_LD = $(MIPS_NATIVE_TOOLS)/ld
MIPS_NATIVE_AOUT = $(MIPS_NATIVE_TOOLS)/aout
MIPS_NATIVE_AOUT_AR = $(MIPS_NATIVE_TOOLS)/ar
MIPS_NATIVE_AOUT_RANLIB = $(MIPS_NATIVE_TOOLS)/ranlib
MIPS_NATIVE_AOUT_NM = $(MIPS_NATIVE_TOOLS)/nm
MIPS_NATIVE_AOUT_SIZE = $(MIPS_NATIVE_TOOLS)/size
MIPS_NATIVE_AOUT_STRIP = $(MIPS_NATIVE_TOOLS)/strip
MIPS_NATIVE_CRT0_SRC = $(TOPSRC)/lib/startup/crt0.s
MIPS_NATIVE_STAMP = $(MIPS_NATIVE_DIR)/.built
MIPS_NATIVE_LIBS = crt0.o libc.a libm.a libpcc.a
MIPS_NATIVE_SOFTFLOAT_LIBS = libpcc.a
MIPS_NATIVE_TARGET_FLAGS_vr4300 = $(MIPS_ROOTFS_ENDIAN_CPP) -DTARGET_VR4300 \
                                  -DTARGET_MIPS_STRICT_ALIGN64 \
                                  -DTARGET_MIPS_SH_ALLOC_GUARD \
                                  -DTARGET_NO_ABICALLS \
                                  $(MIPS_ROOTFS_TOOLCHAIN_CPP) \
                                  $(MIPS_ROOTFS_EXTRA_CPPFLAGS)
MIPS_NATIVE_TARGET_FLAGS_mips32r2 = $(MIPS_ROOTFS_ENDIAN_CPP) \
                                    -DTARGET_MIPS32R2 \
                                    -DTARGET_MIPS_SH_ALLOC_GUARD \
                                    -DTARGET_NO_ABICALLS \
                                    $(MIPS_ROOTFS_TOOLCHAIN_CPP) \
                                    $(MIPS_ROOTFS_EXTRA_CPPFLAGS)
MIPS_NATIVE_TARGET_FLAGS ?= $(MIPS_NATIVE_TARGET_FLAGS_$(MIPS_ROOTFS_CPU))
MIPS_NATIVE_MKHOSTINCLUDE = $(TOPSRC)/sys/mips/n64/native/mkhostinclude.sh
MIPS_NATIVE_CC_SCRIPT = $(TOPSRC)/sys/mips/n64/native/n64-aout-cc.sh
MIPS_NATIVE_ASWRAP_SCRIPT = $(TOPSRC)/sys/mips/n64/native/n64-aout-as.sh
MIPS_HOST_PORTABLECC_SCRIPT = $(TOPSRC)/sys/mips/n64/native/smoke-host-portablecc.sh
MIPS_NATIVE_AS_SMOKE_SCRIPT = $(TOPSRC)/sys/mips/n64/native/smoke-as-vr4300.sh
MIPS_NATIVE_AS_MATRIX_SCRIPT = $(TOPSRC)/sys/mips/n64/native/matrix-as-vr4300.sh
MIPS_NATIVE_AOUT_SMOKE_SCRIPT = $(TOPSRC)/sys/mips/n64/native/smoke-aout-toolchain.sh
MIPS_NATIVE_PCC_MAKEFILE = $(TOPSRC)/sys/mips/tools/Makefile.native-pcc
MIPS_NATIVE_PCC_CONFIG = $(TOPSRC)/sys/mips/tools/native-pcc-config.h
MIPS_REAL_MAKE ?= $(if $(REBSD_REAL_MAKE),$(REBSD_REAL_MAKE),$(MAKE))
MIPS_NATIVE_PCC_BUILD ?= mips-native-pcc-build.$(MIPS_ROOTFS_ABI)
MIPS_NATIVE_PCC_DIR ?= mips-native-pcc.$(MIPS_ROOTFS_ABI)
MIPS_NATIVE_PCC_STAMP = $(MIPS_NATIVE_PCC_DIR)/.built
ifeq ($(MIPS_ROOTFS_COMPILER),pcc)
ifneq ($(MIPS_ROOTFS_NATIVE_PCC),1)
$(error MIPS_ROOTFS_COMPILER=pcc requires MIPS_ROOTFS_NATIVE_PCC=1)
endif
endif
MIPS_ROOTFS_NATIVE_PCC_STAMPS =
ifeq ($(MIPS_ROOTFS_NATIVE_PCC),1)
MIPS_ROOTFS_NATIVE_PCC_STAMPS = $(MIPS_NATIVE_STAMP) $(MIPS_NATIVE_PCC_STAMP)
endif

MIPS_LIBPCC_DIR = $(TOPSRC)/src/dev/pcc/pcc-libs/libpcc
MIPS_LIBPCC_OBJS = cmpdi2.o divdi3.o fixdfdi.o fixsfdi.o fixunsdfdi.o \
                  fixunssfdi.o floatdidf.o floatdisf.o floatunsdidf.o \
                  isinf_sign.o moddi3.o muldi3.o negdi2.o qdivrem.o \
                  ucmpdi2.o udivdi3.o umoddi3.o cxmuldiv.o \
                  ashldi3.o ashrdi3.o lshrdi3.o \
                  _alloca.o unwind.o ssp.o signbit.o
MIPS_LIBPCC_RUNTIME_DIR = $(TOPSRC)/src/libc/runtime
MIPS_LIBPCC_RUNTIME_OBJS = adddf3.o addsf3.o comparedf2.o comparesf2.o \
                  divdf3.o divsf3.o extendsfdf2.o fixdfsi.o fixsfsi.o \
                  fixunssfsi.o fixunsdfsi.o floatunsdisf.o floatsidf.o \
                  floatsisf.o floatundidf.o floatunsisf.o fp_mode.o \
                  int_helpers.o muldf3.o mulsf3.o negdf2.o negsf2.o \
                  subdf3.o subsf3.o truncdfsf2.o floatunsidf.o
MIPS_LIBPCC_SRCS = $(addprefix $(MIPS_LIBPCC_DIR)/,$(MIPS_LIBPCC_OBJS:.o=.c)) \
                  $(MIPS_LIBPCC_DIR)/quad.h \
                  $(shell find $(MIPS_LIBPCC_DIR)/include -type f 2>/dev/null) \
                  $(addprefix $(MIPS_LIBPCC_RUNTIME_DIR)/,$(MIPS_LIBPCC_RUNTIME_OBJS:.o=.c)) \
                  $(shell find $(MIPS_LIBPCC_RUNTIME_DIR) -type f \( -name '*.h' -o -name '*.inc' \) 2>/dev/null)
MIPS_DEV_PCC_SRCS = $(MIPS_NATIVE_PCC_MAKEFILE) $(MIPS_NATIVE_PCC_CONFIG) \
                   $(shell find $(TOPSRC)/src/dev/pcc/pcc -type f \
                   \( -name '*.[chly]' -o -name '*.h' -o -name Makefile.in \
                   -o -name configure -o -name config.sub \
                   -o -name config.guess \) 2>/dev/null)
MIPS_NATIVE_TOOL_SRCS = $(MIPS_NATIVE_MKHOSTINCLUDE) $(MIPS_NATIVE_CC_SCRIPT) \
                       $(MIPS_NATIVE_ASWRAP_SCRIPT) \
                       $(MIPS_NATIVE_AS_SMOKE_SCRIPT) \
                       $(MIPS_NATIVE_AS_MATRIX_SCRIPT) \
                       $(MIPS_NATIVE_AOUT_SMOKE_SCRIPT) \
                       $(TOPSRC)/src/cmd/as/as.c \
                       $(TOPSRC)/src/cmd/ld/ld.c \
                       $(TOPSRC)/src/cmd/aout/aout.c \
                       $(TOPSRC)/src/cmd/aout/mips-dis.c \
                       $(TOPSRC)/src/cmd/aout/mips-opcode.h \
                       $(TOPSRC)/src/cmd/ar/append.c \
                       $(TOPSRC)/src/cmd/ar/ar.c \
                       $(TOPSRC)/src/cmd/ar/archive.c \
                       $(TOPSRC)/src/cmd/ar/contents.c \
                       $(TOPSRC)/src/cmd/ar/delete.c \
                       $(TOPSRC)/src/cmd/ar/extract.c \
                       $(TOPSRC)/src/cmd/ar/extern.h \
                       $(TOPSRC)/src/cmd/ar/misc.c \
                       $(TOPSRC)/src/cmd/ar/move.c \
                       $(TOPSRC)/src/cmd/ar/print.c \
                       $(TOPSRC)/src/cmd/ar/replace.c \
                       $(TOPSRC)/src/cmd/ar/strmode.c \
                       $(TOPSRC)/src/cmd/ranlib/ranlib.c \
                       $(TOPSRC)/src/cmd/nm/nm.c \
                       $(TOPSRC)/src/cmd/size.c \
                       $(TOPSRC)/src/cmd/strip.c \
                       $(TOPSRC)/src/cmd/elf32_mips.h \
                       $(TOPSRC)/src/cmd/aoutio.c \
                       $(TOPSRC)/src/cmd/aoutio.h \
                       $(TOPSRC)/include/a.out.h \
                       $(TOPSRC)/include/ar.h \
                       $(TOPSRC)/include/nlist.h \
                       $(TOPSRC)/include/ranlib.h \
                       $(TOPSRC)/include/sys/exec_aout.h
MIPS_NATIVE_RUNTIME_SRCS = $(MIPS_NATIVE_TOOL_SRCS) $(MIPS_INCLUDE_SRCS) \
                          $(MIPS_NATIVE_CRT0_SRC) $(MIPS_LIBC_SRCS) \
                          $(MIPS_LIBM_SRCS) $(MIPS_LIBPCC_SRCS) \
                          $(TOPSRC)/target.mk $(TOPSRC)/target-mips.mk \
                          $(TOPSRC)/target-n64.mk

MIPS_PCC_PROVIDER ?= cross
MIPS_PCC_PROVIDERS = cross system
ifeq ($(filter $(MIPS_PCC_PROVIDER),$(MIPS_PCC_PROVIDERS)),)
$(error Unsupported MIPS_PCC_PROVIDER=$(MIPS_PCC_PROVIDER); expected one of $(MIPS_PCC_PROVIDERS))
endif
MIPS_PCC_HOST_BUILD ?= $(MIPS_BUILD_TOOLCHAIN_DIR)/pcc-build.$(MIPS_ROOTFS_ABI)
MIPS_PCC_HOST_PREFIX ?= $(MIPS_BUILD_TOOLCHAIN_DIR)/pcc-install.$(MIPS_ROOTFS_ABI)
MIPS_CROSS_PCC_TRIPLE ?= $(if $(filter little,$(MIPS_ROOTFS_ENDIAN)),mipsel-rebsd,mips-rebsd)
MIPS_CROSS_PCC_TARGET = $(MIPS_PCC_HOST_PREFIX)/$(MIPS_CROSS_PCC_TRIPLE)
MIPS_CROSS_PCC_LIB = $(MIPS_CROSS_PCC_TARGET)/lib
MIPS_CROSS_PCC_LDSCRIPTS = $(MIPS_CROSS_PCC_LIB)/ldscripts
MIPS_CROSS_PCC_SOFTFLOAT_LIB = $(MIPS_CROSS_PCC_LIB)/softfloat
MIPS_HOST_PCC = $(MIPS_PCC_HOST_PREFIX)/bin/$(MIPS_CROSS_PCC_TRIPLE)-pcc
MIPS_HOST_CCOM = $(MIPS_PCC_HOST_PREFIX)/libexec/$(MIPS_CROSS_PCC_TRIPLE)-ccom
MIPS_PCC_TOOLCHAIN_STAMP = $(MIPS_PCC_HOST_PREFIX)/.rebsd-pcc-toolchain.stamp
MIPS_CROSS_PCC_AS = $(MIPS_PCC_HOST_PREFIX)/bin/$(MIPS_CROSS_PCC_TRIPLE)-as
MIPS_CROSS_PCC_LD = $(MIPS_PCC_HOST_PREFIX)/bin/$(MIPS_CROSS_PCC_TRIPLE)-ld
MIPS_CROSS_PCC_AOUT = $(MIPS_PCC_HOST_PREFIX)/bin/$(MIPS_CROSS_PCC_TRIPLE)-aout
MIPS_CROSS_PCC_AR = $(MIPS_PCC_HOST_PREFIX)/bin/$(MIPS_CROSS_PCC_TRIPLE)-ar
MIPS_CROSS_PCC_RANLIB = $(MIPS_PCC_HOST_PREFIX)/bin/$(MIPS_CROSS_PCC_TRIPLE)-ranlib
MIPS_CROSS_PCC_NM = $(MIPS_PCC_HOST_PREFIX)/bin/$(MIPS_CROSS_PCC_TRIPLE)-nm
MIPS_CROSS_PCC_SIZE = $(MIPS_PCC_HOST_PREFIX)/bin/$(MIPS_CROSS_PCC_TRIPLE)-size
MIPS_CROSS_PCC_STRIP = $(MIPS_PCC_HOST_PREFIX)/bin/$(MIPS_CROSS_PCC_TRIPLE)-strip
MIPS_PCC_SYSTEM_CC ?= cc
MIPS_PCC_SYSTEM_AS ?= as
MIPS_PCC_SYSTEM_LD ?= ld
MIPS_PCC_SYSTEM_AOUT ?= aout
MIPS_PCC_SYSTEM_AR ?= ar
MIPS_PCC_SYSTEM_RANLIB ?= ranlib
MIPS_PCC_SYSTEM_NM ?= nm
MIPS_PCC_SYSTEM_SIZE ?= size
MIPS_PCC_SYSTEM_STRIP ?= strip
ifeq ($(MIPS_PCC_PROVIDER),cross)
MIPS_PCC_CC = $(MIPS_HOST_PCC)
MIPS_PCC_AS = $(MIPS_CROSS_PCC_AS)
MIPS_PCC_LD = $(MIPS_CROSS_PCC_LD)
MIPS_PCC_AOUT = $(MIPS_CROSS_PCC_AOUT)
MIPS_PCC_AR = $(MIPS_CROSS_PCC_AR)
MIPS_PCC_RANLIB = $(MIPS_CROSS_PCC_RANLIB)
MIPS_PCC_NM = $(MIPS_CROSS_PCC_NM)
MIPS_PCC_SIZE = $(MIPS_CROSS_PCC_SIZE)
MIPS_PCC_STRIP = $(MIPS_CROSS_PCC_STRIP)
MIPS_PCC_PROVIDER_DEPS = $(MIPS_PCC_TOOLCHAIN_STAMP)
else ifeq ($(MIPS_PCC_PROVIDER),system)
MIPS_PCC_CC = $(MIPS_PCC_SYSTEM_CC)
MIPS_PCC_AS = $(MIPS_PCC_SYSTEM_AS)
MIPS_PCC_LD = $(MIPS_PCC_SYSTEM_LD)
MIPS_PCC_AOUT = $(MIPS_PCC_SYSTEM_AOUT)
MIPS_PCC_AR = $(MIPS_PCC_SYSTEM_AR)
MIPS_PCC_RANLIB = $(MIPS_PCC_SYSTEM_RANLIB)
MIPS_PCC_NM = $(MIPS_PCC_SYSTEM_NM)
MIPS_PCC_SIZE = $(MIPS_PCC_SYSTEM_SIZE)
MIPS_PCC_STRIP = $(MIPS_PCC_SYSTEM_STRIP)
MIPS_PCC_PROVIDER_DEPS =
endif
MIPS_PCC_RUNTIME_CC = $(MIPS_PCC_CC) -march=$(MIPS_ROOTFS_CPU) \
                     $(MIPS_ROOTFS_FLOAT_FLAG) \
                     -I$(abspath $(MIPS_NATIVE_TREE)/sys/mips/include) \
                     -I$(abspath $(MIPS_NATIVE_TREE)/sys/mips/n64/include) \
                     -I$(abspath $(MIPS_NATIVE_TREE)/include)
MIPS_PCC_RUNTIME_AS = $(MIPS_PCC_RUNTIME_CC) -x assembler-with-cpp -c
MIPS_PCC_RUNTIME_SOFT_CC = $(MIPS_PCC_CC) -march=$(MIPS_ROOTFS_CPU) \
                          -msoft-float \
                          -I$(abspath $(MIPS_NATIVE_TREE)/sys/mips/include) \
                          -I$(abspath $(MIPS_NATIVE_TREE)/sys/mips/n64/include) \
                          -I$(abspath $(MIPS_NATIVE_TREE)/include)

MIPS_ROOTFS_INCLUDES = -I$(abspath $(MIPS_ROOTFS_USR_INCLUDE)/machine) \
                       -I$(abspath $(MIPS_ROOTFS_USR_INCLUDE))

ifeq ($(MIPS_ROOTFS_COMPILER),gcc)
MIPS_USERLAND_CC = $(MIPS_ROOTFS_GCC_PREFIX)gcc $(MIPS_ROOTFS_ARCH) $(MIPS_ROOTFS_CODE) $(MIPS_ROOTFS_ENDIAN_CPP) $(MIPS_ROOTFS_TOOLCHAIN_CPP) $(MIPS_ROOTFS_EXTRA_CPPFLAGS) $(MIPS_ROOTFS_INCLUDES) \
                -Werror -Wno-unused-value -Wno-format-overflow \
                -Wno-attribute-alias -Wno-missing-attributes
MIPS_USERLAND_AS = $(MIPS_USERLAND_CC) -x assembler-with-cpp -c
MIPS_USERLAND_LD = $(MIPS_ROOTFS_GCC_PREFIX)ld -m $(MIPS_ROOTFS_LD_EMULATION)
MIPS_USERLAND_AR = $(MIPS_ROOTFS_GCC_PREFIX)ar
MIPS_USERLAND_RANLIB = $(MIPS_ROOTFS_GCC_PREFIX)ranlib
MIPS_USERLAND_NM = $(MIPS_ROOTFS_GCC_PREFIX)nm
MIPS_USERLAND_SIZE = $(MIPS_ROOTFS_GCC_PREFIX)size
MIPS_USERLAND_OBJDUMP = $(MIPS_ROOTFS_GCC_PREFIX)objdump
MIPS_USERLAND_ELF2AOUT_aout = $(abspath $(MIPS_ROOTFS_ELF2AOUT))
MIPS_USERLAND_ELF2AOUT_elf = cp
MIPS_USERLAND_ELF2AOUT = $(MIPS_USERLAND_ELF2AOUT_$(MIPS_ROOTFS_EXEC_FORMAT))
MIPS_USERLAND_LDFLAGS = --nmagic -T$(abspath $(MIPS_ROOTFS_USER_LDSCRIPT)) \
                       $(abspath $(MIPS_BUILD_SRC_DIR)/crt0.o) \
                       -L$(abspath $(MIPS_BUILD_SRC_DIR))
MIPS_USERLAND_CRT0_DEPS = $(MIPS_BUILD_SRC_DIR)/crt0.o
MIPS_USERLAND_EXTRA_DEPS = $(MIPS_ROOTFS_EXEC_FORMAT_DEPS)
else ifeq ($(MIPS_ROOTFS_COMPILER),pcc)
MIPS_USERLAND_CC = $(MIPS_PCC_CC) -march=$(MIPS_ROOTFS_CPU) \
                  $(MIPS_ROOTFS_FLOAT_FLAG) $(MIPS_ROOTFS_ENDIAN_CPP) \
                  $(MIPS_ROOTFS_TOOLCHAIN_CPP) \
                  $(MIPS_ROOTFS_EXTRA_CPPFLAGS) \
                  $(MIPS_ROOTFS_INCLUDES)
MIPS_USERLAND_AS = $(MIPS_USERLAND_CC) -x assembler-with-cpp -c
MIPS_USERLAND_LD = $(MIPS_PCC_CC) -march=$(MIPS_ROOTFS_CPU) \
                  $(MIPS_ROOTFS_FLOAT_FLAG)
MIPS_USERLAND_AR = $(MIPS_PCC_AR)
MIPS_USERLAND_RANLIB = $(MIPS_PCC_RANLIB)
MIPS_USERLAND_NM = $(MIPS_PCC_NM)
MIPS_USERLAND_SIZE = $(MIPS_PCC_SIZE)
MIPS_USERLAND_OBJDUMP = true
MIPS_USERLAND_ELF2AOUT = cp
MIPS_USERLAND_LDFLAGS = -nostartfiles \
                       -T$(abspath $(MIPS_ROOTFS_USER_LDSCRIPT)) \
                       $(abspath $(MIPS_NATIVE_DIR)/crt0.o) \
                       -L$(abspath $(MIPS_BUILD_SRC_DIR)) \
                       $(if $(filter soft,$(MIPS_ROOTFS_FLOAT)),-L$(abspath $(MIPS_NATIVE_SOFTFLOAT_DIR)),) \
                       -L$(abspath $(MIPS_NATIVE_DIR))
MIPS_USERLAND_CRT0_DEPS = $(MIPS_NATIVE_DIR)/crt0.o
MIPS_USERLAND_EXTRA_DEPS = $(MIPS_PCC_PROVIDER_DEPS) \
                          $(MIPS_NATIVE_DIR)/libpcc.a \
                          $(MIPS_NATIVE_SOFTFLOAT_DIR)/libpcc.a
else
$(error Unsupported MIPS_ROOTFS_COMPILER=$(MIPS_ROOTFS_COMPILER); expected one of $(MIPS_ROOTFS_COMPILERS))
endif

MIPS_SRC_MAKE = GROFF_NO_SGR=1 $(MAKE) -C $(TOPSRC)/src \
               TARGET_PLATFORM=$(MIPS_ROOTFS_TARGET_PLATFORM) \
               MIPS_ROOTFS_CPU=$(MIPS_ROOTFS_CPU) \
               MIPS_ROOTFS_ENDIAN=$(MIPS_ROOTFS_ENDIAN) \
               N64_USER_LDSCRIPT=$(abspath $(MIPS_ROOTFS_USER_LDSCRIPT)) \
               N64_STB_DIR="$(MIPS_STB_DIR)" \
               DESTDIR=$(abspath $(MIPS_ROOTFS_STAGE)) \
               CC="$(MIPS_USERLAND_CC)" \
               AS="$(MIPS_USERLAND_AS)" \
               LD="$(MIPS_USERLAND_LD)" \
               AR="$(MIPS_USERLAND_AR)" \
               RANLIB="$(MIPS_USERLAND_RANLIB)" \
               NM="$(MIPS_USERLAND_NM)" \
               SIZE="$(MIPS_USERLAND_SIZE)" \
               OBJDUMP="$(MIPS_USERLAND_OBJDUMP)" \
               ELF2AOUT="$(MIPS_USERLAND_ELF2AOUT)" \
               LDFLAGS="$(MIPS_USERLAND_LDFLAGS)" \
               LIBC_COMPILER_RUNTIME=$(if $(filter pcc,$(MIPS_ROOTFS_COMPILER)),libpcc,libc) \
               CC_STDINC=/usr/include \
               CC_LIBDIR=/usr/lib \
               CC_LIBEXECDIR=/usr/libexec \
               CC_CRT0FILE=/usr/lib/crt0.o \
               CC_CRT0FILE_PROFILE=/usr/lib/mcrt0.o \
               SRC_ONLY_LIBS="$(MIPS_SRC_LIBS)" \
               SRC_ONLY_SUBDIR="$(MIPS_SRC_SUBDIRS)" \
               CMD_ONLY_SUBDIR="$(MIPS_CMD_SUBDIRS)" \
               CMD_ONLY_STD="$(MIPS_CMD_STDS)" \
               CMD_ONLY_SCRIPT="$(MIPS_CMD_SCRIPTS)" \
               CMD_ONLY_NSTD="$(MIPS_CMD_NSTDS)" \
               CMD_ONLY_SETUID="$(MIPS_CMD_NONE)" \
               CMD_ONLY_OPERATOR="$(MIPS_CMD_OPERATORS)" \
               CMD_ONLY_KMEM="$(MIPS_CMD_NONE)" \
               CMD_ONLY_TTY="$(MIPS_CMD_NONE)" \
               CMD_EXTRA_SUBDIR="$(MIPS_CMD_EXTRA_SUBDIRS)" \
               SMUX_SUBDIRS=retro \
               CMD_BUILD_STRIP=strip
MIPS_AWK_MAKE = $(MAKE) -C $(TOPSRC)/src/cmd/awk \
               TARGET_PLATFORM=$(MIPS_ROOTFS_TARGET_PLATFORM) \
               MIPS_ROOTFS_CPU=$(MIPS_ROOTFS_CPU) \
               MIPS_ROOTFS_ENDIAN=$(MIPS_ROOTFS_ENDIAN) \
               N64_USER_LDSCRIPT=$(abspath $(MIPS_ROOTFS_USER_LDSCRIPT)) \
               DESTDIR=$(abspath $(MIPS_ROOTFS_STAGE)) \
               CC="$(MIPS_USERLAND_CC)" \
               AS="$(MIPS_USERLAND_AS)" \
               LD="$(MIPS_USERLAND_LD)" \
               AR="$(MIPS_USERLAND_AR)" \
               RANLIB="$(MIPS_USERLAND_RANLIB)" \
               NM="$(MIPS_USERLAND_NM)" \
               SIZE="$(MIPS_USERLAND_SIZE)" \
               OBJDUMP="$(MIPS_USERLAND_OBJDUMP)" \
               ELF2AOUT="$(MIPS_USERLAND_ELF2AOUT)" \
               LDFLAGS="$(MIPS_USERLAND_LDFLAGS)" \
               YACC="$(MIPS_YACC)"

MIPS_LINPACK_SMOKE_SCRIPT = $(TOPSRC)/sys/mips/tools/linpack-smoke.py
MIPS_LINPACK_SMOKE_SRC = $(TOPSRC)/sys/mips/rootfs/root/linpack.c
MIPS_LINPACK_SMOKE_OUT ?= $(MIPS_BUILD_TEST_DIR)/linpack-smoke
MIPS_LINPACK_SMOKE_MANIFEST ?= rootfs.linpack-smoke.manifest
MIPS_LINPACK_ROOTFS_STAMP = $(MIPS_ROOTFS_STAGE)/.linpack-smoke.$(MIPS_ROOTFS_ABI)
MIPS_ROOTFS_LINPACK_GCC ?= $(if $(filter gcc,$(MIPS_ROOTFS_COMPILER)),1,0)
MIPS_LINPACK_GCC_COMPILE ?= $(MIPS_ROOTFS_GCC_PREFIX)gcc \
    $(MIPS_ROOTFS_ARCH) $(MIPS_ROOTFS_CODE) $(MIPS_ROOTFS_TOOLCHAIN_CPP) \
    $(MIPS_ROOTFS_EXTRA_CPPFLAGS)
MIPS_LINPACK_GCC_LINK ?= $(MIPS_ROOTFS_GCC_PREFIX)ld \
    -m $(MIPS_ROOTFS_LD_EMULATION) --nmagic \
    -T$(abspath $(MIPS_ROOTFS_USER_LDSCRIPT)) \
    $(abspath $(MIPS_BUILD_SRC_DIR)/crt0.o)
MIPS_LINPACK_GCC_LIBDIR ?= $(abspath $(MIPS_BUILD_SRC_DIR))
MIPS_LINPACK_GCC_LINK_FORMAT ?= elf
MIPS_LINPACK_GCC_DEPS ?=
MIPS_LINPACK_PCC_LDSCRIPT_NAME = $(if $(filter little,$(MIPS_ROOTFS_ENDIAN)),elf32-littlemips.ld,elf32-bigmips.ld)
MIPS_LINPACK_PCC_LDSCRIPT = $(MIPS_ROOTFS_USR_LIB)/ldscripts/$(MIPS_LINPACK_PCC_LDSCRIPT_NAME)
MIPS_LINPACK_PCC_LINKER_SCRIPT = $(if $(wildcard $(MIPS_LINPACK_PCC_LDSCRIPT)),-T $(abspath $(MIPS_LINPACK_PCC_LDSCRIPT)),)
MIPS_COMPILER_BENCH_SRC = $(TOPSRC)/sys/mips/rootfs/root/mips-compiler-bench.c
MIPS_COMPILER_BENCH_OUT ?= $(MIPS_BUILD_TEST_DIR)/mips-compiler-bench
MIPS_COMPILER_BENCH_ROOTFS_STAMP = $(MIPS_ROOTFS_STAGE)/.mips-compiler-bench.$(MIPS_ROOTFS_ABI)
MIPS_VM_PROCESS_SMOKE_SRC = $(TOPSRC)/sys/mips/tools/vm-process-smoke.c
MIPS_VM_PROCESS_SMOKE_GPR64_SRC = $(TOPSRC)/sys/mips/tools/vm-process-smoke-gpr64.S
MIPS_VM_PROCESS_SMOKE_OUT ?= $(MIPS_BUILD_TEST_DIR)/vm-process-smoke.$(MIPS_ROOTFS_COMPILER).$(MIPS_ROOTFS_ABI)
MIPS_VM_PROCESS_SMOKE_GPR64_OBJ = $(MIPS_VM_PROCESS_SMOKE_OUT).gpr64.o
MIPS_VM_PROCESS_SMOKE_GPR64_SRCS = $(if $(filter vr4300,$(MIPS_ROOTFS_CPU)),$(MIPS_VM_PROCESS_SMOKE_GPR64_SRC),)
MIPS_VM_PROCESS_SMOKE_GPR64_OBJS = $(if $(filter vr4300,$(MIPS_ROOTFS_CPU)),$(MIPS_VM_PROCESS_SMOKE_GPR64_OBJ),)
MIPS_VM_PROCESS_SMOKE_ROOTFS_STAMP = $(MIPS_ROOTFS_STAGE)/.vm-process-smoke.$(MIPS_ROOTFS_COMPILER).$(MIPS_ROOTFS_ABI)
MIPS_NET_SMOKE_SRC = $(TOPSRC)/sys/mips/rootfs/root/net-smoke.c
MIPS_NET_SMOKE_OUT ?= $(MIPS_BUILD_TEST_DIR)/net-smoke.$(MIPS_ROOTFS_COMPILER).$(MIPS_ROOTFS_ABI)
MIPS_NET_SMOKE_ROOTFS_STAMP = $(MIPS_ROOTFS_STAGE)/.net-smoke.$(MIPS_ROOTFS_COMPILER).$(MIPS_ROOTFS_ABI)
MIPS_LIBC_ABI_SMOKE_SRC = $(TOPSRC)/sys/mips/rootfs/root/libc-abi-smoke.c
MIPS_LIBC_ABI_SMOKE_OUT ?= $(MIPS_BUILD_TEST_DIR)/libc-abi-smoke.$(MIPS_ROOTFS_COMPILER).$(MIPS_ROOTFS_ABI)
MIPS_LIBC_ABI_SMOKE_ROOTFS_STAMP = $(MIPS_ROOTFS_STAGE)/.libc-abi-smoke.$(MIPS_ROOTFS_COMPILER).$(MIPS_ROOTFS_ABI)
MIPS_ROOTFS_EXTRA_STAMPS ?=
MIPS_ROOTFS_EXTRA_STAMPS += $(MIPS_VM_PROCESS_SMOKE_ROOTFS_STAMP)
MIPS_ROOTFS_EXTRA_STAMPS += $(MIPS_NET_SMOKE_ROOTFS_STAMP)
MIPS_ROOTFS_EXTRA_STAMPS += $(MIPS_LIBC_ABI_SMOKE_ROOTFS_STAMP)
ifeq ($(MIPS_ROOTFS_NATIVE_PCC),1)
MIPS_ROOTFS_EXTRA_STAMPS += $(MIPS_LINPACK_ROOTFS_STAMP)
MIPS_ROOTFS_EXTRA_STAMPS += $(MIPS_COMPILER_BENCH_ROOTFS_STAMP)
endif

MIPS_ROOTFS_BOARD_MANIFEST_0775 ?=
MIPS_ROOTFS_BOARD_MANIFEST_0755 ?=
MIPS_ROOTFS_BOARD_MANIFEST_0664 ?=
MIPS_ROOTFS_BOARD_CAT1_PAGES ?=
MIPS_ROOTFS_EXTRA_USR_BIN_FILES ?= \
    $(MIPS_NATIVE_AS_SMOKE_SCRIPT):smoke-as-vr4300 \
    $(MIPS_NATIVE_AS_SMOKE_SCRIPT):smoke-as-vr4300.sh \
    $(MIPS_NATIVE_AS_MATRIX_SCRIPT):matrix-as-vr4300 \
    $(MIPS_NATIVE_AS_MATRIX_SCRIPT):matrix-as-vr4300.sh

$(MIPS_ROOTFS_USER_LDSCRIPT): $(MIPS_ROOTFS_USER_LDSCRIPT_SRC)
	$(CC) -E -P -undef -x c $(CPPFLAGS) $< -o $@

$(MIPS_ROOTFS_ELF2AOUT): $(MIPS_ROOTFS_ELF2AOUT_SRCS)
	$(MAKE) -C $(TOPSRC)/tools/elf2aout

$(MIPS_ROOTFS_BUILD_MANIFEST): $(MIPS_ROOTFS_MAKEFILE) $(MIPS_ROOTFS_MANIFEST) \
    $(MIPS_ROOTFS_BOARD_MANIFEST) $(MIPS_ROOTFS_USER_STAMP) \
    $(MIPS_ROOTFS_EXTRA_STAMPS)
	cp $(MIPS_ROOTFS_MANIFEST) $@
	if [ "$(MIPS_ROOTFS_NATIVE_PCC)" != "1" ]; then \
	    awk 'BEGIN { skip = 0 } \
	        /^symlink \/bin\/cpp$$/ { skip = 1; next } \
	        skip && /^target / { skip = 0; next } \
	        /^dir \/usr\/libexec\/pcc$$/ { next } \
	        /^file \/(usr\/bin\/(cc|cpp|pcc)|usr\/lib\/libpcc\.a|usr\/libexec\/pcc\/(ccom|cpp))$$/ { skip = 1; next } \
	        skip && /^mode / { skip = 0; next } \
	        { skip = 0; print }' $@ > $@.tmp; \
	    mv $@.tmp $@; \
	fi
	if [ "$(MIPS_ROOTFS_ENDIAN)" = "little" ]; then \
	    sed 's|^file /lib/retroImageBE$$|file /lib/retroImage|' \
	        $@ > $@.tmp; \
	    mv $@.tmp $@; \
	fi
	if [ -n "$(MIPS_ROOTFS_BOARD_MANIFEST)" ] && \
	    [ -f "$(MIPS_ROOTFS_BOARD_MANIFEST)" ]; then \
	    cat $(MIPS_ROOTFS_BOARD_MANIFEST) >> $@; \
	fi
	find $(MIPS_ROOTFS_USR_INCLUDE) -type d | sort | while read path; do \
	    rel=$${path#$(MIPS_ROOTFS_STAGE)}; \
	    printf '\ndir %s\n' "$$rel" >> $@; \
	done
	find $(MIPS_ROOTFS_USR_INCLUDE) -type f ! -name '.gitignore' \
	    ! -name Makefile ! -name Makefile.install | sort | while read path; do \
	    rel=$${path#$(MIPS_ROOTFS_STAGE)}; \
	    printf '\nfile %s\n' "$$rel" >> $@; \
	done
	find $(MIPS_ROOTFS_USR_INCLUDE) -maxdepth 1 -type l | \
	    sort | while read path; do \
	    rel=$${path#$(MIPS_ROOTFS_STAGE)}; \
	    target=$$(readlink "$$path"); \
	    printf '\nsymlink %s\ntarget %s\n' "$$rel" "$$target" >> $@; \
	done
	for path in $(MIPS_ROOTFS_BOARD_MANIFEST_0775); do \
	    printf '\nfile %s\nmode 0775\n' "$$path" >> $@; \
	done
	for path in $(MIPS_ROOTFS_BOARD_MANIFEST_0755); do \
	    printf '\nfile %s\nmode 0755\n' "$$path" >> $@; \
	done
	for path in $(MIPS_ROOTFS_BOARD_MANIFEST_0664); do \
	    printf '\nfile %s\n' "$$path" >> $@; \
	done
	for page in $(MIPS_ROOTFS_BOARD_CAT1_PAGES); do \
	    printf '\nfile /usr/share/man/cat1/%s.0\n' "$$page" >> $@; \
	done
	for spec in $(MIPS_ROOTFS_BOARD_CMD_CAT1_SOURCES); do \
	    page=$${spec#*:}; \
	    printf '\nfile /usr/share/man/cat1/%s.0\n' "$$page" >> $@; \
	done
	printf '\nfile %s\n' "$(MIPS_ROOTFS_INSTALLED_LDSCRIPT_PATH)" >> $@
	if [ "$(MIPS_ROOTFS_NATIVE_PCC)" = "1" ]; then \
	    printf '\ndir /usr/lib/softfloat\n' >> $@; \
	    printf '\nfile /usr/lib/softfloat/libpcc.a\n' >> $@; \
	    if [ "$(MIPS_ROOTFS_LINPACK_GCC)" = "1" ]; then \
	        printf '\nfile /root/linpack-gcc\nmode 0775\n' >> $@; \
	        printf '\nfile /root/linpack-kernels-gcc\nmode 0775\n' >> $@; \
	    fi; \
	    printf '\nfile /root/linpack-pcc\nmode 0775\n' >> $@; \
	    printf '\nfile /root/linpack-kernels-pcc\nmode 0775\n' >> $@; \
	    if [ "$(MIPS_ROOTFS_LINPACK_GCC)" = "1" ]; then \
	        printf '\nfile /root/mips-compiler-bench-gcc\nmode 0775\n' >> $@; \
	    fi; \
	    printf '\nfile /root/mips-compiler-bench-pcc\nmode 0775\n' >> $@; \
	fi
	printf '\nfile /root/vm-process-smoke\nmode 0775\n' >> $@
	printf '\nfile /root/net-smoke\nmode 0775\n' >> $@
	printf '\nfile /root/libc-abi-smoke\nmode 0775\n' >> $@

$(MIPS_LINPACK_ROOTFS_STAMP): $(MIPS_ROOTFS_USER_STAMP) $(MIPS_PCC_PROVIDER_DEPS) \
    $(MIPS_ROOTFS_EXEC_FORMAT_DEPS) $(MIPS_LINPACK_SMOKE_SCRIPT) \
    $(MIPS_LINPACK_SMOKE_SRC) $(MIPS_LINPACK_GCC_DEPS)
	python3 $(MIPS_LINPACK_SMOKE_SCRIPT) build \
	    --source $(abspath $(MIPS_LINPACK_SMOKE_SRC)) \
	    --out $(MIPS_LINPACK_SMOKE_OUT) \
	    --rootfs $(abspath $(MIPS_ROOTFS_STAGE)) \
	    --pcc $(MIPS_PCC_CC) \
	    --cpu $(MIPS_ROOTFS_CPU) \
	    --float-abi $(MIPS_ROOTFS_FLOAT) \
	    --endian $(MIPS_ROOTFS_ENDIAN)
	$(MIPS_PCC_CC) -march=$(MIPS_ROOTFS_CPU) $(MIPS_ROOTFS_FLOAT_FLAG) \
	    -O2 -DLINPACK_KERNEL_BENCH \
	    $(MIPS_LINPACK_PCC_LINKER_SCRIPT) \
	    -I$(abspath $(MIPS_ROOTFS_USR_INCLUDE)) \
	    -L$(abspath $(MIPS_ROOTFS_USR_LIB)) \
	    -o $(MIPS_LINPACK_SMOKE_OUT)/linpack-kernels-pcc \
	    $(MIPS_LINPACK_SMOKE_SRC) -lm
	if [ "$(MIPS_ROOTFS_LINPACK_GCC)" = "1" ]; then \
	    $(MIPS_LINPACK_GCC_COMPILE) \
	        -I$(abspath $(MIPS_ROOTFS_USR_INCLUDE)) \
	        -O2 -c -o $(MIPS_LINPACK_SMOKE_OUT)/linpack-gcc.o \
	        $(MIPS_LINPACK_SMOKE_SRC); \
	    $(MIPS_LINPACK_GCC_LINK) \
	        $(MIPS_LINPACK_SMOKE_OUT)/linpack-gcc.o \
	        -L$(MIPS_LINPACK_GCC_LIBDIR) \
	        -o $(MIPS_LINPACK_SMOKE_OUT)/linpack-gcc.linked \
	        -lm -lc; \
	    if [ "$(MIPS_LINPACK_GCC_LINK_FORMAT)" = "aout" ]; then \
	        mv -f $(MIPS_LINPACK_SMOKE_OUT)/linpack-gcc.linked \
	            $(MIPS_LINPACK_SMOKE_OUT)/linpack-gcc; \
	    elif [ "$(MIPS_ROOTFS_EXEC_FORMAT)" = "aout" ]; then \
	        $(abspath $(MIPS_ROOTFS_ELF2AOUT)) \
	            $(MIPS_LINPACK_SMOKE_OUT)/linpack-gcc.linked \
	            $(MIPS_LINPACK_SMOKE_OUT)/linpack-gcc; \
	        rm -f $(MIPS_LINPACK_SMOKE_OUT)/linpack-gcc.linked; \
	    else \
	        mv -f $(MIPS_LINPACK_SMOKE_OUT)/linpack-gcc.linked \
	            $(MIPS_LINPACK_SMOKE_OUT)/linpack-gcc; \
	    fi; \
	    $(MIPS_LINPACK_GCC_COMPILE) \
	        -I$(abspath $(MIPS_ROOTFS_USR_INCLUDE)) \
	        -O2 -DLINPACK_KERNEL_BENCH -c \
	        -o $(MIPS_LINPACK_SMOKE_OUT)/linpack-kernels-gcc.o \
	        $(MIPS_LINPACK_SMOKE_SRC); \
	    $(MIPS_LINPACK_GCC_LINK) \
	        $(MIPS_LINPACK_SMOKE_OUT)/linpack-kernels-gcc.o \
	        -L$(MIPS_LINPACK_GCC_LIBDIR) \
	        -o $(MIPS_LINPACK_SMOKE_OUT)/linpack-kernels-gcc.linked \
	        -lm -lc; \
	    if [ "$(MIPS_LINPACK_GCC_LINK_FORMAT)" = "aout" ]; then \
	        mv -f $(MIPS_LINPACK_SMOKE_OUT)/linpack-kernels-gcc.linked \
	            $(MIPS_LINPACK_SMOKE_OUT)/linpack-kernels-gcc; \
	    elif [ "$(MIPS_ROOTFS_EXEC_FORMAT)" = "aout" ]; then \
	        $(abspath $(MIPS_ROOTFS_ELF2AOUT)) \
	            $(MIPS_LINPACK_SMOKE_OUT)/linpack-kernels-gcc.linked \
	            $(MIPS_LINPACK_SMOKE_OUT)/linpack-kernels-gcc; \
	        rm -f $(MIPS_LINPACK_SMOKE_OUT)/linpack-kernels-gcc.linked; \
	    else \
	        mv -f $(MIPS_LINPACK_SMOKE_OUT)/linpack-kernels-gcc.linked \
	            $(MIPS_LINPACK_SMOKE_OUT)/linpack-kernels-gcc; \
	    fi; \
	    cp -p $(MIPS_LINPACK_SMOKE_OUT)/linpack-gcc \
	        $(MIPS_ROOTFS_STAGE)/root/linpack-gcc; \
	    chmod 0775 $(MIPS_ROOTFS_STAGE)/root/linpack-gcc; \
	    cp -p $(MIPS_LINPACK_SMOKE_OUT)/linpack-kernels-gcc \
	        $(MIPS_ROOTFS_STAGE)/root/linpack-kernels-gcc; \
	    chmod 0775 $(MIPS_ROOTFS_STAGE)/root/linpack-kernels-gcc; \
	fi
	cp -p $(MIPS_LINPACK_SMOKE_OUT)/linpack-pcc \
	    $(MIPS_ROOTFS_STAGE)/root/linpack-pcc
	chmod 0775 $(MIPS_ROOTFS_STAGE)/root/linpack-pcc
	cp -p $(MIPS_LINPACK_SMOKE_OUT)/linpack-kernels-pcc \
	    $(MIPS_ROOTFS_STAGE)/root/linpack-kernels-pcc
	chmod 0775 $(MIPS_ROOTFS_STAGE)/root/linpack-kernels-pcc
	touch $@

$(MIPS_COMPILER_BENCH_ROOTFS_STAMP): $(MIPS_ROOTFS_USER_STAMP) \
    $(MIPS_PCC_PROVIDER_DEPS) $(MIPS_ROOTFS_EXEC_FORMAT_DEPS) \
    $(MIPS_COMPILER_BENCH_SRC) $(MIPS_LINPACK_GCC_DEPS)
	rm -rf $(MIPS_COMPILER_BENCH_OUT)
	mkdir -p $(MIPS_COMPILER_BENCH_OUT)
	$(MIPS_PCC_CC) -march=$(MIPS_ROOTFS_CPU) $(MIPS_ROOTFS_FLOAT_FLAG) \
	    -O2 $(MIPS_LINPACK_PCC_LINKER_SCRIPT) \
	    -I$(abspath $(MIPS_ROOTFS_USR_INCLUDE)) \
	    -L$(abspath $(MIPS_ROOTFS_USR_LIB)) \
	    -o $(MIPS_COMPILER_BENCH_OUT)/mips-compiler-bench-pcc \
	    $(MIPS_COMPILER_BENCH_SRC)
	if [ "$(MIPS_ROOTFS_LINPACK_GCC)" = "1" ]; then \
	    $(MIPS_LINPACK_GCC_COMPILE) \
	        -I$(abspath $(MIPS_ROOTFS_USR_INCLUDE)) \
	        -O2 -c -o $(MIPS_COMPILER_BENCH_OUT)/mips-compiler-bench-gcc.o \
	        $(MIPS_COMPILER_BENCH_SRC); \
	    $(MIPS_LINPACK_GCC_LINK) \
	        $(MIPS_COMPILER_BENCH_OUT)/mips-compiler-bench-gcc.o \
	        -L$(MIPS_LINPACK_GCC_LIBDIR) \
	        -o $(MIPS_COMPILER_BENCH_OUT)/mips-compiler-bench-gcc.linked \
	        -lc; \
	    if [ "$(MIPS_LINPACK_GCC_LINK_FORMAT)" = "aout" ]; then \
	        mv -f $(MIPS_COMPILER_BENCH_OUT)/mips-compiler-bench-gcc.linked \
	            $(MIPS_COMPILER_BENCH_OUT)/mips-compiler-bench-gcc; \
	    elif [ "$(MIPS_ROOTFS_EXEC_FORMAT)" = "aout" ]; then \
	        $(abspath $(MIPS_ROOTFS_ELF2AOUT)) \
	            $(MIPS_COMPILER_BENCH_OUT)/mips-compiler-bench-gcc.linked \
	            $(MIPS_COMPILER_BENCH_OUT)/mips-compiler-bench-gcc; \
	        rm -f $(MIPS_COMPILER_BENCH_OUT)/mips-compiler-bench-gcc.linked; \
	    else \
	        mv -f $(MIPS_COMPILER_BENCH_OUT)/mips-compiler-bench-gcc.linked \
	            $(MIPS_COMPILER_BENCH_OUT)/mips-compiler-bench-gcc; \
	    fi; \
	    cp -p $(MIPS_COMPILER_BENCH_OUT)/mips-compiler-bench-gcc \
	        $(MIPS_ROOTFS_STAGE)/root/mips-compiler-bench-gcc; \
	    chmod 0775 $(MIPS_ROOTFS_STAGE)/root/mips-compiler-bench-gcc; \
	fi
	cp -p $(MIPS_COMPILER_BENCH_OUT)/mips-compiler-bench-pcc \
	    $(MIPS_ROOTFS_STAGE)/root/mips-compiler-bench-pcc
	chmod 0775 $(MIPS_ROOTFS_STAGE)/root/mips-compiler-bench-pcc
	touch $@

$(MIPS_VM_PROCESS_SMOKE_ROOTFS_STAMP): $(MIPS_ROOTFS_USER_STAMP) \
    $(MIPS_VM_PROCESS_SMOKE_SRC) $(MIPS_VM_PROCESS_SMOKE_GPR64_SRCS) \
    $(MIPS_USERLAND_EXTRA_DEPS) \
    $(MIPS_ROOTFS_MAKEFILE)
	rm -f $(MIPS_VM_PROCESS_SMOKE_OUT) \
	    $(MIPS_VM_PROCESS_SMOKE_OUT).o \
	    $(MIPS_VM_PROCESS_SMOKE_GPR64_OBJ) \
	    $(MIPS_VM_PROCESS_SMOKE_OUT).linked
	mkdir -p $(dir $(MIPS_VM_PROCESS_SMOKE_OUT))
	$(MIPS_USERLAND_CC) -O2 -c \
	    -o $(MIPS_VM_PROCESS_SMOKE_OUT).o $(MIPS_VM_PROCESS_SMOKE_SRC)
	if [ -n "$(MIPS_VM_PROCESS_SMOKE_GPR64_SRCS)" ]; then \
	    $(MIPS_USERLAND_AS) -o $(MIPS_VM_PROCESS_SMOKE_GPR64_OBJ) \
	        $(MIPS_VM_PROCESS_SMOKE_GPR64_SRC); \
	fi
	$(MIPS_USERLAND_LD) $(MIPS_USERLAND_LDFLAGS) \
	    -o $(MIPS_VM_PROCESS_SMOKE_OUT).linked \
	    $(MIPS_VM_PROCESS_SMOKE_OUT).o \
	    $(MIPS_VM_PROCESS_SMOKE_GPR64_OBJS) -lc
	$(MIPS_USERLAND_ELF2AOUT) $(MIPS_VM_PROCESS_SMOKE_OUT).linked \
	    $(MIPS_VM_PROCESS_SMOKE_OUT)
	rm -f $(MIPS_VM_PROCESS_SMOKE_OUT).linked
	cp -p $(MIPS_VM_PROCESS_SMOKE_OUT) \
	    $(MIPS_ROOTFS_STAGE)/root/vm-process-smoke
	chmod 0775 $(MIPS_ROOTFS_STAGE)/root/vm-process-smoke
	touch $@

$(MIPS_NET_SMOKE_ROOTFS_STAMP): $(MIPS_ROOTFS_USER_STAMP) \
    $(MIPS_NET_SMOKE_SRC) $(MIPS_USERLAND_EXTRA_DEPS) \
    $(MIPS_ROOTFS_MAKEFILE)
	rm -f $(MIPS_NET_SMOKE_OUT) \
	    $(MIPS_NET_SMOKE_OUT).o \
	    $(MIPS_NET_SMOKE_OUT).linked
	mkdir -p $(dir $(MIPS_NET_SMOKE_OUT))
	$(MIPS_USERLAND_CC) -O2 -c \
	    -o $(MIPS_NET_SMOKE_OUT).o $(MIPS_NET_SMOKE_SRC)
	$(MIPS_USERLAND_LD) $(MIPS_USERLAND_LDFLAGS) \
	    -o $(MIPS_NET_SMOKE_OUT).linked \
	    $(MIPS_NET_SMOKE_OUT).o -lc
	$(MIPS_USERLAND_ELF2AOUT) $(MIPS_NET_SMOKE_OUT).linked \
	    $(MIPS_NET_SMOKE_OUT)
	rm -f $(MIPS_NET_SMOKE_OUT).linked
	cp -p $(MIPS_NET_SMOKE_OUT) $(MIPS_ROOTFS_STAGE)/root/net-smoke
	chmod 0775 $(MIPS_ROOTFS_STAGE)/root/net-smoke
	touch $@

$(MIPS_LIBC_ABI_SMOKE_ROOTFS_STAMP): $(MIPS_ROOTFS_USER_STAMP) \
    $(MIPS_LIBC_ABI_SMOKE_SRC) $(MIPS_USERLAND_EXTRA_DEPS) \
    $(MIPS_ROOTFS_MAKEFILE)
	rm -f $(MIPS_LIBC_ABI_SMOKE_OUT) \
	    $(MIPS_LIBC_ABI_SMOKE_OUT).o \
	    $(MIPS_LIBC_ABI_SMOKE_OUT).linked
	mkdir -p $(dir $(MIPS_LIBC_ABI_SMOKE_OUT))
	$(MIPS_USERLAND_CC) -O2 -c \
	    -o $(MIPS_LIBC_ABI_SMOKE_OUT).o $(MIPS_LIBC_ABI_SMOKE_SRC)
	$(MIPS_USERLAND_LD) $(MIPS_USERLAND_LDFLAGS) \
	    -o $(MIPS_LIBC_ABI_SMOKE_OUT).linked \
	    $(MIPS_LIBC_ABI_SMOKE_OUT).o -lc
	$(MIPS_USERLAND_ELF2AOUT) $(MIPS_LIBC_ABI_SMOKE_OUT).linked \
	    $(MIPS_LIBC_ABI_SMOKE_OUT)
	rm -f $(MIPS_LIBC_ABI_SMOKE_OUT).linked
	cp -p $(MIPS_LIBC_ABI_SMOKE_OUT) \
	    $(MIPS_ROOTFS_STAGE)/root/libc-abi-smoke
	chmod 0775 $(MIPS_ROOTFS_STAGE)/root/libc-abi-smoke
	touch $@

$(MIPS_ROOTFS_BASE_STAMP): $(MIPS_ROOTFS_MAKEFILE) \
    $(MIPS_ROOTFS_FILES) \
    $(MIPS_UTILITY_SMOKE_SRCS) $(MIPS_TERMCAP) \
    $(MIPS_INCLUDE_SRCS) $(MIPS_INCLUDE_LINKS) \
    $(MIPS_NATIVE_AS_SMOKE_SCRIPT) $(MIPS_NATIVE_AS_MATRIX_SCRIPT)
	rm -rf $(MIPS_ROOTFS_STAGE)
	mkdir -p $(MIPS_ROOTFS_STAGE)
	cp -pR $(MIPS_ROOTFS_COMMON_DIR)/. $(MIPS_ROOTFS_STAGE)/
	if [ -n "$(MIPS_ROOTFS_BOARD_DIR)" ] && \
	    [ -d "$(MIPS_ROOTFS_BOARD_DIR)" ]; then \
	    cp -pR $(MIPS_ROOTFS_BOARD_DIR)/. $(MIPS_ROOTFS_STAGE)/; \
	fi
	mkdir -p $(MIPS_ROOTFS_STAGE)/root/utility-src
	for src in $(MIPS_UTILITY_SMOKE_SRCS); do \
	    cp -p $$src $(MIPS_ROOTFS_STAGE)/root/utility-src/; \
	done
	cp -p $(MIPS_TERMCAP) $(MIPS_ROOTFS_STAGE)/etc/termcap
	mkdir -p $(MIPS_ROOTFS_USR_INCLUDE)
	cp -pR $(TOPSRC)/include/. $(MIPS_ROOTFS_USR_INCLUDE)/
	rm -rf $(MIPS_ROOTFS_USR_INCLUDE)/machine $(MIPS_ROOTFS_USR_INCLUDE)/sys
	cp -pR $(TOPSRC)/sys/include $(MIPS_ROOTFS_USR_INCLUDE)/sys
	mkdir -p $(MIPS_ROOTFS_USR_INCLUDE)/net
	cp -p $(TOPSRC)/sys/net/*.h $(MIPS_ROOTFS_USR_INCLUDE)/net/
	mkdir -p $(MIPS_ROOTFS_USR_INCLUDE)/netinet
	for header in $(MIPS_NETINET_USER_HEADERS); do \
	    cp -p $$header $(MIPS_ROOTFS_USR_INCLUDE)/netinet/; \
	done
	mkdir -p $(MIPS_ROOTFS_USR_INCLUDE)/mips
	cp -p $(TOPSRC)/sys/mips/*.h $(MIPS_ROOTFS_USR_INCLUDE)/mips/
	mkdir -p $(MIPS_ROOTFS_USR_INCLUDE)/machine
	if [ -n "$(MIPS_BOARD_MACHINE_HEADER_DIR)" ] && \
	    [ -d "$(MIPS_BOARD_MACHINE_HEADER_DIR)" ]; then \
	    cp -p $(MIPS_BOARD_MACHINE_HEADER_DIR)/*.h \
	        $(MIPS_ROOTFS_USR_INCLUDE)/machine/; \
	fi
	for header in $(MIPS_COMMON_MACHINE_HEADERS); do \
	    cp -p $(TOPSRC)/sys/mips/$$header.h \
	        $(MIPS_ROOTFS_USR_INCLUDE)/machine/$$header.h; \
	done
	cp -p $(TOPSRC)/sys/mips/include/machine/*.h \
	    $(MIPS_ROOTFS_USR_INCLUDE)/machine/
	mkdir -p $(MIPS_ROOTFS_STAGE)/lib
	mkdir -p $(MIPS_ROOTFS_STAGE)/sbin
	mkdir -p $(MIPS_ROOTFS_STAGE)/bin
	mkdir -p $(MIPS_ROOTFS_STAGE)/libexec
	mkdir -p $(MIPS_ROOTFS_STAGE)/share/misc
	mkdir -p $(MIPS_ROOTFS_STAGE)/share/man/cat1
	mkdir -p $(MIPS_ROOTFS_STAGE)/share/man/cat5
	mkdir -p $(MIPS_ROOTFS_STAGE)/share/man/cat8
	mkdir -p $(MIPS_ROOTFS_USR_BIN)
	mkdir -p $(MIPS_ROOTFS_USR_LIB)
	mkdir -p $(MIPS_ROOTFS_USR_LIBEXEC)
	mkdir -p $(MIPS_ROOTFS_USR_SHARE)/misc
	mkdir -p $(MIPS_ROOTFS_USR_SHARE)/man/cat1
	mkdir -p $(MIPS_ROOTFS_USR_SHARE)/man/cat5
	mkdir -p $(MIPS_ROOTFS_USR_SHARE)/man/cat8
	for spec in $(MIPS_ROOTFS_EXTRA_USR_BIN_FILES); do \
	    src=$${spec%:*}; dst=$${spec#*:}; \
	    cp -p $$src $(MIPS_ROOTFS_USR_BIN)/$$dst; \
	    chmod 0775 $(MIPS_ROOTFS_USR_BIN)/$$dst; \
	done
	touch $@

$(MIPS_ROOTFS_USER_STAMP): $(MIPS_ROOTFS_BASE_STAMP) \
    $(MIPS_ROOTFS_USERLAND_STAMP) $(MIPS_ROOTFS_NATIVE_PCC_STAMPS) \
    $(MIPS_MAKEWHATIS_SED) \
    $(MIPS_ROOTFS_MAKEFILE) Makefile
	mkdir -p $(MIPS_ROOTFS_STAGE)/share/misc \
	    $(MIPS_ROOTFS_STAGE)/share/man/cat1 \
	    $(MIPS_ROOTFS_STAGE)/share/man/cat5 \
	    $(MIPS_ROOTFS_STAGE)/share/man/cat8
	$(MIPS_SRC_MAKE) install
	$(MIPS_AWK_MAKE) install
	mkdir -p $(MIPS_ROOTFS_USR_BIN) $(MIPS_ROOTFS_USR_LIB) \
	    $(MIPS_ROOTFS_USR_LIB)/$(MIPS_ROOTFS_LDSCRIPTS_DIR) \
	    $(MIPS_ROOTFS_USR_LIB)/softfloat \
	    $(MIPS_ROOTFS_USR_LIBEXEC) $(MIPS_ROOTFS_USR_SHARE)
	rm -f $(MIPS_ROOTFS_USR_LIB)/elf32-mips.ld
	rm -f $(MIPS_ROOTFS_USR_LIB)/$(MIPS_ROOTFS_LDSCRIPTS_DIR)/elf32-mips.ld
	cp -p $(MIPS_ROOTFS_USER_LDSCRIPT) \
	    $(MIPS_ROOTFS_USR_LIB)/$(MIPS_ROOTFS_INSTALLED_LDSCRIPT)
	for bin in $(MIPS_USR_BIN_FILES); do \
	    if [ -e $(MIPS_ROOTFS_STAGE)/bin/$$bin ]; then \
	        mv -f $(MIPS_ROOTFS_STAGE)/bin/$$bin $(MIPS_ROOTFS_USR_BIN)/$$bin; \
	    fi; \
	done
	for file in $(MIPS_USR_LIBEXEC_FILES); do \
	    if [ -e $(MIPS_ROOTFS_STAGE)/libexec/$$file ]; then \
	        mv -f $(MIPS_ROOTFS_STAGE)/libexec/$$file $(MIPS_ROOTFS_USR_LIBEXEC)/$$file; \
	    fi; \
	done
	if [ "$(MIPS_ROOTFS_NATIVE_PCC)" = "1" ]; then \
	    rm -f $(MIPS_ROOTFS_USR_BIN)/cc $(MIPS_ROOTFS_USR_BIN)/cpp \
	        $(MIPS_ROOTFS_USR_BIN)/pcc $(MIPS_ROOTFS_USR_BIN)/p++ \
	        $(MIPS_ROOTFS_USR_BIN)/lcc $(MIPS_ROOTFS_USR_BIN)/scc; \
	    rm -f $(MIPS_ROOTFS_USR_LIBEXEC)/ccom $(MIPS_ROOTFS_USR_LIBEXEC)/lccom \
	        $(MIPS_ROOTFS_USR_LIBEXEC)/smallc $(MIPS_ROOTFS_USR_LIBEXEC)/smlrc; \
	    rm -f $(MIPS_ROOTFS_USR_LIBEXEC)/pcc/cxxcom; \
	    mkdir -p $(MIPS_ROOTFS_USR_LIBEXEC)/pcc; \
	    cp -p $(MIPS_NATIVE_PCC_DIR)/cc $(MIPS_ROOTFS_USR_BIN)/cc; \
	    cp -p $(MIPS_NATIVE_PCC_DIR)/cc $(MIPS_ROOTFS_USR_BIN)/pcc; \
	    cp -p $(MIPS_NATIVE_PCC_DIR)/cc $(MIPS_ROOTFS_USR_BIN)/cpp; \
	    cp -p $(MIPS_NATIVE_PCC_DIR)/cpp $(MIPS_ROOTFS_USR_LIBEXEC)/pcc/cpp; \
	    cp -p $(MIPS_NATIVE_PCC_DIR)/ccom $(MIPS_ROOTFS_USR_LIBEXEC)/pcc/ccom; \
	fi
	if [ -d $(MIPS_ROOTFS_STAGE)/share ]; then \
	    cp -pR $(MIPS_ROOTFS_STAGE)/share/. $(MIPS_ROOTFS_USR_SHARE)/; \
	    rm -rf $(MIPS_ROOTFS_STAGE)/share; \
	fi
	mkdir -p $(MIPS_ROOTFS_USR_INCLUDE)/mips
	cp -p $(TOPSRC)/sys/mips/*.h $(MIPS_ROOTFS_USR_INCLUDE)/mips/
	if [ "$(MIPS_ROOTFS_NATIVE_PCC)" = "1" ]; then \
	    for lib in $(MIPS_NATIVE_LIBS); do \
	        cp -p $(MIPS_NATIVE_DIR)/$$lib $(MIPS_ROOTFS_USR_LIB)/$$lib; \
	    done; \
	    for lib in $(MIPS_NATIVE_SOFTFLOAT_LIBS); do \
	        cp -p $(MIPS_NATIVE_SOFTFLOAT_DIR)/$$lib \
	            $(MIPS_ROOTFS_USR_LIB)/softfloat/$$lib; \
	    done; \
	    for lib in libc.a libm.a libpcc.a; do \
	        $(MIPS_PCC_RANLIB) $(MIPS_ROOTFS_USR_LIB)/$$lib; \
	    done; \
	    for lib in $(MIPS_NATIVE_SOFTFLOAT_LIBS); do \
	        $(MIPS_PCC_RANLIB) $(MIPS_ROOTFS_USR_LIB)/softfloat/$$lib; \
	    done; \
	else \
	    cp -p $(MIPS_BUILD_SRC_DIR)/crt0.o $(MIPS_ROOTFS_USR_LIB)/crt0.o; \
	    cp -p $(MIPS_BUILD_SRC_DIR)/libc.a $(MIPS_ROOTFS_USR_LIB)/libc.a; \
	    cp -p $(MIPS_BUILD_SRC_DIR)/libm.a $(MIPS_ROOTFS_USR_LIB)/libm.a; \
	    $(MIPS_USERLAND_RANLIB) $(MIPS_ROOTFS_USR_LIB)/libc.a; \
	    $(MIPS_USERLAND_RANLIB) $(MIPS_ROOTFS_USR_LIB)/libm.a; \
	fi
	mkdir -p $(MIPS_ROOTFS_USR_SHARE)/man/cat1 \
	    $(MIPS_ROOTFS_USR_SHARE)/man/cat5 \
	    $(MIPS_ROOTFS_USR_SHARE)/man/cat8
	for page in $(MIPS_ROOTFS_CAT1_PAGES); do \
	    GROFF_NO_SGR=1 $(MANROFF) $(TOPSRC)/src/man/man1/$$page.1 > $(MIPS_ROOTFS_USR_SHARE)/man/cat1/$$page.0; \
	done
	for spec in $(MIPS_ROOTFS_CMD_CAT1_SOURCES); do \
	    dir=$${spec%:*}; page=$${spec#*:}; \
	    GROFF_NO_SGR=1 $(MANROFF) $(TOPSRC)/src/cmd/$$dir/$$page.1 > $(MIPS_ROOTFS_USR_SHARE)/man/cat1/$$page.0; \
	done
	for spec in $(MIPS_ROOTFS_BOARD_CMD_CAT1_SOURCES); do \
	    dir=$${spec%:*}; page=$${spec#*:}; \
	    GROFF_NO_SGR=1 $(MANROFF) $(TOPSRC)/src/cmd/$$dir/$$page.1 > $(MIPS_ROOTFS_USR_SHARE)/man/cat1/$$page.0; \
	done
	for alias in $(MIPS_ROOTFS_CAT1_ALIASES); do \
	    dst=$${alias%:*}; src=$${alias#*:}; \
	    cp $(MIPS_ROOTFS_USR_SHARE)/man/cat1/$$src.0 $(MIPS_ROOTFS_USR_SHARE)/man/cat1/$$dst.0; \
	done
	for page in $(MIPS_ROOTFS_CAT8_PAGES); do \
	    GROFF_NO_SGR=1 $(MANROFF) $(TOPSRC)/src/man/man8/$$page.8 > $(MIPS_ROOTFS_USR_SHARE)/man/cat8/$$page.0; \
	done
	for alias in $(MIPS_ROOTFS_CAT8_ALIASES); do \
	    dst=$${alias%:*}; src=$${alias#*:}; \
	    if [ ! -e $(MIPS_ROOTFS_USR_SHARE)/man/cat8/$$dst.0 ] || \
	        ! cmp -s $(MIPS_ROOTFS_USR_SHARE)/man/cat8/$$src.0 $(MIPS_ROOTFS_USR_SHARE)/man/cat8/$$dst.0; then \
	        cp $(MIPS_ROOTFS_USR_SHARE)/man/cat8/$$src.0 $(MIPS_ROOTFS_USR_SHARE)/man/cat8/$$dst.0; \
	    fi; \
	done
	rm -f $(MIPS_ROOTFS_WHATIS).tmp
	touch $(MIPS_ROOTFS_WHATIS).tmp
	for file in `find $(MIPS_ROOTFS_USR_SHARE)/man/cat* -type f -name '*.0' -print`; do \
	    sed -n -f $(MIPS_MAKEWHATIS_SED) $$file >> $(MIPS_ROOTFS_WHATIS).tmp; \
	done
	sort -u $(MIPS_ROOTFS_WHATIS).tmp > $(MIPS_ROOTFS_WHATIS)
	rm -f $(MIPS_ROOTFS_WHATIS).tmp
	rm -f $(MIPS_ROOTFS_STAGE)/.userland.gcc* \
	    $(MIPS_ROOTFS_STAGE)/.userland.pcc* \
	    $(MIPS_ROOTFS_USER_COMPAT_STAMP)
	touch $@
	touch $(MIPS_ROOTFS_USER_COMPAT_STAMP)

$(MIPS_ROOTFS_USER_COMPAT_STAMP): $(MIPS_ROOTFS_USER_STAMP)
	touch $@

rootfs.img: $(FSUTIL) $(MIPS_ROOTFS_IMAGE_DEPS)
	rm -f $@
	$(FSUTIL) --new --endian=$(MIPS_ROOTFS_ENDIAN) \
	    --size=$(MIPS_ROOTFS_IMAGE_KBYTES) \
	    --manifest=$(MIPS_ROOTFS_IMAGE_MANIFEST) $@ $(MIPS_ROOTFS_IMAGE_STAGE)
	$(FSUTIL) --check $@

rootfs.o: rootfs.img
	$(LD) -r -b binary -o $@ rootfs.img
	$(OBJCOPY) --rename-section .data=.romdisk,alloc,load,readonly,data,contents $@

.PHONY: mips-rootfs-userland-clean mips-rootfs-clean
mips-rootfs-userland-clean:
	@MAKEFLAGS="$(MAKEFLAGS) -s" $(MIPS_SRC_MAKE) clean
	@MAKEFLAGS="$(MAKEFLAGS) -s" $(MIPS_AWK_MAKE) clean

mips-rootfs-clean: mips-rootfs-userland-clean
	@rm -rf $(MIPS_ROOTFS_CLEAN_ARTIFACTS)

$(MIPS_BUILD_SRC_DIR)/crt0.o: $(MIPS_NATIVE_CRT0_SRC)
	mkdir -p $(dir $@)
	$(MIPS_ROOTFS_GCC_PREFIX)gcc $(MIPS_ROOTFS_ARCH) $(MIPS_ROOTFS_CODE) -x assembler-with-cpp -c $< -o $@

$(MIPS_ROOTFS_USERLAND_STAMP): $(MIPS_ROOTFS_USER_LDSCRIPT) \
    $(MIPS_USERLAND_CRT0_DEPS) $(MIPS_NATIVE_CRT0_SRC) $(MIPS_LIBC_SRCS) \
    $(MIPS_LIBM_SRCS) $(MIPS_LIBUTIL_SRCS) $(MIPS_LIBTERMLIB_SRCS) \
    $(MIPS_LIBCURSES_SRCS) $(MIPS_LIBVMF_SRCS) $(MIPS_LIBREADLINE_SRCS) \
    $(MIPS_LIBTCL_SRCS) $(MIPS_USER_SRCS) $(MIPS_AWK_SRCS) \
    $(MIPS_INCLUDE_SRCS) $(MIPS_INCLUDE_LINKS) \
    $(MIPS_USERLAND_EXTRA_DEPS) $(MIPS_ROOTFS_MAKEFILE) Makefile
	$(MIPS_SRC_MAKE) clean
	if [ "$(MIPS_ROOTFS_COMPILER)" = "gcc" ]; then \
	    $(MAKE) $(MIPS_BUILD_SRC_DIR)/crt0.o; \
	fi
	$(MIPS_SRC_MAKE) all
	$(MIPS_AWK_MAKE) clean
	$(MIPS_AWK_MAKE) awk
	rm -f mips-userland*.stamp n64-userland*.stamp
	touch $@

$(MIPS_NATIVE_HOST_INCLUDE_STAMP): $(MIPS_NATIVE_MKHOSTINCLUDE) \
    $(MIPS_INCLUDE_SRCS) $(MIPS_INCLUDE_LINKS)
	rm -rf $(MIPS_NATIVE_HOST_INCLUDE)
	$(MIPS_NATIVE_MKHOSTINCLUDE) $(TOPSRC) $(MIPS_NATIVE_HOST_INCLUDE)
	touch $@

$(MIPS_NATIVE_AS): $(MIPS_NATIVE_HOST_INCLUDE_STAMP) \
    $(TOPSRC)/src/cmd/as/as.c $(TOPSRC)/src/cmd/aoutio.c \
    $(TOPSRC)/src/cmd/aoutio.h $(TOPSRC)/include/a.out.h
	mkdir -p $(MIPS_NATIVE_TOOLS)
	cc -DCROSS $(MIPS_NATIVE_TARGET_FLAGS) \
	    -I$(MIPS_NATIVE_HOST_INCLUDE) -I$(TOPSRC)/src/cmd \
	    -o $@ $(TOPSRC)/src/cmd/as/as.c $(TOPSRC)/src/cmd/aoutio.c

$(MIPS_NATIVE_LD): $(MIPS_NATIVE_HOST_INCLUDE_STAMP) \
    $(TOPSRC)/src/cmd/ld/ld.c $(TOPSRC)/src/cmd/aoutio.c \
    $(TOPSRC)/src/cmd/aoutio.h $(TOPSRC)/include/a.out.h
	mkdir -p $(MIPS_NATIVE_TOOLS)
	cc -DCROSS $(MIPS_NATIVE_TARGET_FLAGS) \
	    -I$(MIPS_NATIVE_HOST_INCLUDE) -I$(TOPSRC)/src/cmd \
	    -o $@ $(TOPSRC)/src/cmd/ld/ld.c $(TOPSRC)/src/cmd/aoutio.c

$(MIPS_NATIVE_AOUT): $(MIPS_NATIVE_HOST_INCLUDE_STAMP) \
    $(TOPSRC)/src/cmd/aout/aout.c $(TOPSRC)/src/cmd/aout/mips-dis.c \
    $(TOPSRC)/src/cmd/aout/mips-opcode.h \
    $(TOPSRC)/src/cmd/aoutio.c $(TOPSRC)/src/cmd/aoutio.h \
    $(TOPSRC)/include/a.out.h
	mkdir -p $(MIPS_NATIVE_TOOLS)
	cc -DCROSS $(MIPS_NATIVE_TARGET_FLAGS) \
	    -I$(MIPS_NATIVE_HOST_INCLUDE) -I$(TOPSRC)/src/cmd \
	    -I$(TOPSRC)/src/cmd/aout \
	    -o $@ $(TOPSRC)/src/cmd/aout/aout.c \
	    $(TOPSRC)/src/cmd/aout/mips-dis.c \
	    $(TOPSRC)/src/cmd/aoutio.c

$(MIPS_NATIVE_AOUT_AR): $(MIPS_NATIVE_HOST_INCLUDE_STAMP) \
    $(TOPSRC)/src/cmd/ar/append.c $(TOPSRC)/src/cmd/ar/ar.c \
    $(TOPSRC)/src/cmd/ar/archive.c $(TOPSRC)/src/cmd/ar/contents.c \
    $(TOPSRC)/src/cmd/ar/delete.c $(TOPSRC)/src/cmd/ar/extract.c \
    $(TOPSRC)/src/cmd/ar/extern.h $(TOPSRC)/src/cmd/ar/misc.c \
    $(TOPSRC)/src/cmd/ar/move.c $(TOPSRC)/src/cmd/ar/print.c \
    $(TOPSRC)/src/cmd/ar/replace.c $(TOPSRC)/src/cmd/ar/strmode.c
	mkdir -p $(MIPS_NATIVE_TOOLS)
	cc -DCROSS $(MIPS_NATIVE_TARGET_FLAGS) \
	    -I$(MIPS_NATIVE_HOST_INCLUDE) -I$(TOPSRC)/src/cmd \
	    -I$(TOPSRC)/src/cmd/ar \
	    -o $@ \
	    $(TOPSRC)/src/cmd/ar/append.c \
	    $(TOPSRC)/src/cmd/ar/ar.c \
	    $(TOPSRC)/src/cmd/ar/archive.c \
	    $(TOPSRC)/src/cmd/ar/contents.c \
	    $(TOPSRC)/src/cmd/ar/delete.c \
	    $(TOPSRC)/src/cmd/ar/extract.c \
	    $(TOPSRC)/src/cmd/ar/misc.c \
	    $(TOPSRC)/src/cmd/ar/move.c \
	    $(TOPSRC)/src/cmd/ar/print.c \
	    $(TOPSRC)/src/cmd/ar/replace.c \
	    $(TOPSRC)/src/cmd/ar/strmode.c

$(MIPS_NATIVE_AOUT_RANLIB): $(MIPS_NATIVE_HOST_INCLUDE_STAMP) \
    $(TOPSRC)/src/cmd/ranlib/ranlib.c $(TOPSRC)/src/cmd/ar/archive.c \
    $(TOPSRC)/src/cmd/ar/extern.h
	mkdir -p $(MIPS_NATIVE_TOOLS)
	cc -DCROSS $(MIPS_NATIVE_TARGET_FLAGS) \
	    -I$(MIPS_NATIVE_HOST_INCLUDE) -I$(TOPSRC)/src/cmd \
	    -I$(TOPSRC)/src/cmd/ar \
	    -o $@ $(TOPSRC)/src/cmd/ranlib/ranlib.c \
	    $(TOPSRC)/src/cmd/ar/archive.c $(TOPSRC)/src/cmd/aoutio.c

$(MIPS_NATIVE_AOUT_NM): $(MIPS_NATIVE_HOST_INCLUDE_STAMP) \
    $(TOPSRC)/src/cmd/nm/nm.c $(TOPSRC)/src/cmd/aoutio.c \
    $(TOPSRC)/src/cmd/aoutio.h $(TOPSRC)/src/cmd/elf32_mips.h
	mkdir -p $(MIPS_NATIVE_TOOLS)
	cc -DCROSS $(MIPS_NATIVE_TARGET_FLAGS) \
	    -I$(MIPS_NATIVE_HOST_INCLUDE) -I$(TOPSRC)/src/cmd \
	    -I$(TOPSRC)/src/cmd/ar \
	    -o $@ $(TOPSRC)/src/cmd/nm/nm.c $(TOPSRC)/src/cmd/aoutio.c

$(MIPS_NATIVE_AOUT_SIZE): $(MIPS_NATIVE_HOST_INCLUDE_STAMP) \
    $(TOPSRC)/src/cmd/size.c $(TOPSRC)/src/cmd/aoutio.c \
    $(TOPSRC)/src/cmd/aoutio.h $(TOPSRC)/src/cmd/elf32_mips.h
	mkdir -p $(MIPS_NATIVE_TOOLS)
	cc $(MIPS_NATIVE_TARGET_FLAGS) \
	    -I$(MIPS_NATIVE_HOST_INCLUDE) -I$(TOPSRC)/src/cmd \
	    -o $@ $(TOPSRC)/src/cmd/size.c $(TOPSRC)/src/cmd/aoutio.c

$(MIPS_NATIVE_AOUT_STRIP): $(MIPS_NATIVE_HOST_INCLUDE_STAMP) \
    $(TOPSRC)/src/cmd/strip.c $(TOPSRC)/src/cmd/aoutio.c \
    $(TOPSRC)/src/cmd/aoutio.h $(TOPSRC)/src/cmd/elf32_mips.h
	mkdir -p $(MIPS_NATIVE_TOOLS)
	cc $(MIPS_NATIVE_TARGET_FLAGS) \
	    -I$(MIPS_NATIVE_HOST_INCLUDE) -I$(TOPSRC)/src/cmd \
	    -o $@ $(TOPSRC)/src/cmd/strip.c $(TOPSRC)/src/cmd/aoutio.c

$(MIPS_PCC_TOOLCHAIN_STAMP): $(MIPS_HOST_PORTABLECC_SCRIPT) $(MIPS_DEV_PCC_SRCS) \
    $(MIPS_PCC_HOST_INCLUDE_STAMP) $(MIPS_NATIVE_AS) $(MIPS_NATIVE_LD) \
    $(MIPS_NATIVE_AOUT) $(MIPS_NATIVE_AOUT_AR) $(MIPS_NATIVE_AOUT_RANLIB) \
    $(MIPS_NATIVE_AOUT_NM) $(MIPS_NATIVE_AOUT_SIZE) \
    $(MIPS_NATIVE_AOUT_STRIP) $(MIPS_ROOTFS_USER_LDSCRIPT)
	sh $(MIPS_HOST_PORTABLECC_SCRIPT) $(abspath $(TOPSRC)) \
	    $(abspath $(MIPS_PCC_HOST_BUILD)) \
	    $(abspath $(MIPS_PCC_HOST_PREFIX)) \
	    $(abspath $(MIPS_PCC_HOST_INCLUDE)) \
	    $(abspath $(MIPS_NATIVE_AS)) $(abspath $(MIPS_NATIVE_LD)) \
	    $(MIPS_ROOTFS_CPU) $(MIPS_ROOTFS_FLOAT) $(MIPS_ROOTFS_ENDIAN) \
	    $(abspath $(MIPS_ROOTFS_USER_LDSCRIPT)) \
	    $(MIPS_ROOTFS_EXEC_FORMAT)
	@test -x $(MIPS_HOST_PCC)
	@test -x $(MIPS_HOST_CCOM)
	@{ cksum $(MIPS_HOST_PCC); cksum $(MIPS_HOST_CCOM); } > $@.tmp
	@mv -f $@.tmp $@

$(MIPS_HOST_PCC): $(MIPS_PCC_TOOLCHAIN_STAMP)
	@test -x $@

.PHONY: smoke-as-vr4300 matrix-as-vr4300 smoke-aout-toolchain \
        smoke-host-portablecc
smoke-as-vr4300: $(MIPS_NATIVE_AS) $(MIPS_NATIVE_AS_SMOKE_SCRIPT)
	$(MIPS_NATIVE_AS_SMOKE_SCRIPT) $(abspath $(MIPS_NATIVE_AS))

matrix-as-vr4300: $(MIPS_NATIVE_AS) $(MIPS_NATIVE_AS_MATRIX_SCRIPT)
	$(MIPS_NATIVE_AS_MATRIX_SCRIPT) $(abspath $(MIPS_NATIVE_AS)) $(MIPS_ROOTFS_GCC_PREFIX)as

smoke-aout-toolchain: $(MIPS_NATIVE_AS) $(MIPS_NATIVE_LD) \
    $(MIPS_NATIVE_AOUT_AR) $(MIPS_NATIVE_AOUT_RANLIB) \
    $(MIPS_NATIVE_AOUT_NM) $(MIPS_NATIVE_AOUT_SIZE) $(MIPS_NATIVE_AOUT_STRIP) \
    $(MIPS_NATIVE_AOUT_SMOKE_SCRIPT)
	$(MIPS_NATIVE_AOUT_SMOKE_SCRIPT) $(abspath $(MIPS_NATIVE_AS)) \
	    $(abspath $(MIPS_NATIVE_LD)) $(abspath $(MIPS_NATIVE_AOUT_AR)) \
	    $(abspath $(MIPS_NATIVE_AOUT_RANLIB)) $(abspath $(MIPS_NATIVE_AOUT_NM)) \
	    $(abspath $(MIPS_NATIVE_AOUT_SIZE)) $(abspath $(MIPS_NATIVE_AOUT_STRIP)) \
	    $(MIPS_ROOTFS_GCC_PREFIX)as

ifeq ($(MIPS_PCC_PROVIDER),cross)
smoke-host-portablecc: $(MIPS_PCC_TOOLCHAIN_STAMP)
	@echo "smoke-host-portablecc: ok"
else
smoke-host-portablecc:
	@echo "smoke-host-portablecc: using system $(MIPS_PCC_CC)"
endif

$(MIPS_NATIVE_DIR)/crt0.o: $(MIPS_NATIVE_CRT0_SRC) $(MIPS_PCC_PROVIDER_DEPS)
	mkdir -p $(MIPS_NATIVE_DIR)
	$(MIPS_PCC_CC) -march=$(MIPS_ROOTFS_CPU) $(MIPS_ROOTFS_FLOAT_FLAG) \
	    -x assembler-with-cpp -c -o $@ $(MIPS_NATIVE_CRT0_SRC)

$(MIPS_NATIVE_DIR)/libc.a $(MIPS_NATIVE_DIR)/libm.a \
    $(MIPS_NATIVE_DIR)/libpcc.a $(MIPS_NATIVE_SOFTFLOAT_DIR)/libpcc.a: \
    | $(MIPS_NATIVE_STAMP)
	@test -f $@

MIPS_NATIVE_LIBC_MAKE = $(MAKE) -C $(MIPS_NATIVE_TREE)/src/libc \
	TARGET_PLATFORM=$(MIPS_ROOTFS_TARGET_PLATFORM) \
	MIPS_ROOTFS_CPU=$(MIPS_ROOTFS_CPU) \
	MIPS_ROOTFS_ENDIAN=$(MIPS_ROOTFS_ENDIAN) \
	N64_USER_LDSCRIPT=$(abspath $(MIPS_ROOTFS_USER_LDSCRIPT)) \
	CC="$(MIPS_PCC_RUNTIME_CC)" AS="$(MIPS_PCC_RUNTIME_AS)" \
	AR="$(MIPS_PCC_AR)" RANLIB="$(MIPS_PCC_RANLIB)" \
	LIBC_COMPILER_RUNTIME=libpcc
MIPS_NATIVE_LIBM_MAKE = $(MAKE) -C $(MIPS_NATIVE_TREE)/src/libm \
	TARGET_PLATFORM=$(MIPS_ROOTFS_TARGET_PLATFORM) \
	MIPS_ROOTFS_CPU=$(MIPS_ROOTFS_CPU) \
	MIPS_ROOTFS_ENDIAN=$(MIPS_ROOTFS_ENDIAN) \
	N64_USER_LDSCRIPT=$(abspath $(MIPS_ROOTFS_USER_LDSCRIPT)) \
	CC="$(MIPS_PCC_RUNTIME_CC)" AS="$(MIPS_PCC_RUNTIME_AS)" \
	AR="$(MIPS_PCC_AR)" RANLIB="$(MIPS_PCC_RANLIB)"

$(MIPS_NATIVE_STAMP): $(MIPS_NATIVE_RUNTIME_SRCS) $(MIPS_PCC_PROVIDER_DEPS) \
    $(MIPS_NATIVE_DIR)/crt0.o $(MIPS_ROOTFS_MAKEFILE)
	rm -rf $(MIPS_NATIVE_TREE)
	mkdir -p $(MIPS_NATIVE_TREE)/src $(MIPS_NATIVE_TREE)/sys/mips/n64
	cp -pR $(TOPSRC)/include $(MIPS_NATIVE_TREE)/
	cp -pR $(TOPSRC)/sys/include $(MIPS_NATIVE_TREE)/sys/
	cp -p $(TOPSRC)/sys/mips/*.h $(MIPS_NATIVE_TREE)/sys/mips/
	cp -p $(TOPSRC)/sys/mips/n64/*.h $(MIPS_NATIVE_TREE)/sys/mips/n64/
	cp -pR $(TOPSRC)/sys/mips/n64/include $(MIPS_NATIVE_TREE)/sys/mips/n64/
	cp -pR $(TOPSRC)/src/libc $(TOPSRC)/src/libm $(MIPS_NATIVE_TREE)/src/
	cp -p $(TOPSRC)/target.mk $(TOPSRC)/target-mips.mk \
	    $(TOPSRC)/target-n64.mk $(MIPS_NATIVE_TREE)/
	$(MIPS_NATIVE_LIBC_MAKE) clean
	$(MIPS_NATIVE_LIBC_MAKE) all
	$(MIPS_PCC_RANLIB) $(MIPS_NATIVE_TREE)/src/libc.a
	$(MIPS_NATIVE_LIBM_MAKE) clean
	$(MIPS_NATIVE_LIBM_MAKE) all
	rm -rf $(MIPS_NATIVE_DIR)/libpcc-build
	mkdir -p $(MIPS_NATIVE_DIR)/libpcc-build
	set -e; for obj in $(MIPS_LIBPCC_OBJS); do \
	    src=$(abspath $(MIPS_LIBPCC_DIR))/$${obj%.o}.c; \
	    $(MIPS_PCC_RUNTIME_CC) -O -fno-builtin -fno-stack-protector \
	        $(MIPS_ROOTFS_ENDIAN_CPP1) -Dos_rebsd -Dmach_mips \
	        -I$(abspath $(MIPS_LIBPCC_DIR)) \
	        -I$(abspath $(MIPS_LIBPCC_DIR))/include \
	        -c -o $(MIPS_NATIVE_DIR)/libpcc-build/$$obj $$src; \
	done
	set -e; for obj in $(MIPS_LIBPCC_RUNTIME_OBJS); do \
	    src=$(abspath $(MIPS_LIBPCC_RUNTIME_DIR))/$${obj%.o}.c; \
	    $(MIPS_PCC_RUNTIME_CC) -O -fno-builtin -fno-stack-protector \
	        $(MIPS_ROOTFS_ENDIAN_CPP1) -Dos_rebsd -Dmach_mips \
	        -I$(abspath $(MIPS_LIBPCC_RUNTIME_DIR)) \
	        -c -o $(MIPS_NATIVE_DIR)/libpcc-build/$$obj $$src; \
	done
	rm -f $(MIPS_NATIVE_DIR)/libpcc.a
	cd $(MIPS_NATIVE_DIR)/libpcc-build && \
	    $(MIPS_PCC_AR) r ../libpcc.a \
	        $(MIPS_LIBPCC_OBJS) $(MIPS_LIBPCC_RUNTIME_OBJS)
	$(MIPS_PCC_RANLIB) $(MIPS_NATIVE_DIR)/libpcc.a
	rm -rf $(MIPS_NATIVE_DIR)/softfloat-build
	mkdir -p $(MIPS_NATIVE_DIR)/softfloat-build $(MIPS_NATIVE_SOFTFLOAT_DIR)
	set -e; for obj in $(MIPS_LIBPCC_OBJS); do \
	    src=$(abspath $(MIPS_LIBPCC_DIR))/$${obj%.o}.c; \
	    $(MIPS_PCC_RUNTIME_SOFT_CC) -O -fno-builtin -fno-stack-protector \
	        $(MIPS_ROOTFS_ENDIAN_CPP1) -Dos_rebsd -Dmach_mips \
	        -I$(abspath $(MIPS_LIBPCC_DIR)) \
	        -I$(abspath $(MIPS_LIBPCC_DIR))/include \
	        -c -o $(MIPS_NATIVE_DIR)/softfloat-build/$$obj $$src; \
	done
	set -e; for obj in $(MIPS_LIBPCC_RUNTIME_OBJS); do \
	    src=$(abspath $(MIPS_LIBPCC_RUNTIME_DIR))/$${obj%.o}.c; \
	    $(MIPS_PCC_RUNTIME_SOFT_CC) -O -fno-builtin -fno-stack-protector \
	        $(MIPS_ROOTFS_ENDIAN_CPP1) -Dos_rebsd -Dmach_mips \
	        -I$(abspath $(MIPS_LIBPCC_RUNTIME_DIR)) \
	        -c -o $(MIPS_NATIVE_DIR)/softfloat-build/$$obj $$src; \
	done
	rm -f $(MIPS_NATIVE_SOFTFLOAT_DIR)/libpcc.a
	cd $(MIPS_NATIVE_DIR)/softfloat-build && \
	    $(MIPS_PCC_AR) r ../softfloat/libpcc.a \
	        $(MIPS_LIBPCC_OBJS) $(MIPS_LIBPCC_RUNTIME_OBJS)
	$(MIPS_PCC_RANLIB) $(MIPS_NATIVE_SOFTFLOAT_DIR)/libpcc.a
	cp $(MIPS_NATIVE_TREE)/src/libc.a $(MIPS_NATIVE_DIR)/libc.a
	cp $(MIPS_NATIVE_TREE)/src/libm.a $(MIPS_NATIVE_DIR)/libm.a
	if [ "$(MIPS_PCC_PROVIDER)" = "cross" ]; then \
	    mkdir -p $(MIPS_CROSS_PCC_LIB) $(MIPS_CROSS_PCC_SOFTFLOAT_LIB); \
	    for lib in $(MIPS_NATIVE_LIBS); do \
	        cp -p $(MIPS_NATIVE_DIR)/$$lib $(MIPS_CROSS_PCC_LIB)/$$lib; \
	    done; \
	    for lib in $(MIPS_NATIVE_SOFTFLOAT_LIBS); do \
	        cp -p $(MIPS_NATIVE_SOFTFLOAT_DIR)/$$lib \
	            $(MIPS_CROSS_PCC_SOFTFLOAT_LIB)/$$lib; \
	    done; \
	fi
	touch $@

$(MIPS_NATIVE_PCC_STAMP): $(MIPS_DEV_PCC_SRCS) $(MIPS_NATIVE_STAMP) \
    $(MIPS_NATIVE_DIR)/crt0.o $(MIPS_NATIVE_DIR)/libc.a \
    $(MIPS_NATIVE_DIR)/libm.a $(MIPS_NATIVE_DIR)/libpcc.a \
    $(MIPS_NATIVE_SOFTFLOAT_DIR)/libpcc.a $(MIPS_ROOTFS_USER_LDSCRIPT) \
    $(MIPS_ROOTFS_MAKEFILE) Makefile
	$(MIPS_REAL_MAKE) -f $(MIPS_NATIVE_PCC_MAKEFILE) \
	    TOPSRC=$(abspath $(TOPSRC)) \
	    BUILD=$(abspath $(MIPS_NATIVE_PCC_BUILD)) \
	    OUT=$(abspath $(MIPS_NATIVE_PCC_DIR)) \
	    TARGET_CC="$(MIPS_PCC_CC)" \
	    TARGET_LD="$(MIPS_PCC_LD)" \
	    LDSCRIPT=$(abspath $(MIPS_ROOTFS_USER_LDSCRIPT)) \
	    CRT0=$(abspath $(MIPS_NATIVE_DIR)/crt0.o) \
	    LIBDIR=$(abspath $(MIPS_NATIVE_DIR)) \
	    PCC_CPU=$(MIPS_ROOTFS_CPU) \
	    PCC_FLOAT=$(MIPS_ROOTFS_FLOAT) \
	    PCC_ENDIAN=$(MIPS_ROOTFS_ENDIAN) \
	    PCC_EXEC_FORMAT=$(MIPS_ROOTFS_EXEC_FORMAT) \
	    BISON="$(MIPS_BISON)" all
	@test -x $(MIPS_NATIVE_PCC_DIR)/cc
	@test -x $(MIPS_NATIVE_PCC_DIR)/cpp
	@test -x $(MIPS_NATIVE_PCC_DIR)/ccom
	touch $@
