TOPSRC          = ../..
BOARD           ?= malta
BOARD_DIR       = $(BOARD)
KCONFIG_DIR     = $(TOPSRC)/tools/kconfig
KCONFIG         = $(KCONFIG_DIR)/kconfig
KCONFIG_SRCS    = $(KCONFIG_DIR)/Makefile $(KCONFIG_DIR)/config.h \
                  $(KCONFIG_DIR)/config.y $(KCONFIG_DIR)/lang.l \
                  $(KCONFIG_DIR)/main.c $(KCONFIG_DIR)/mkioconf.c \
                  $(KCONFIG_DIR)/mkmakefile.c $(KCONFIG_DIR)/mkswapconf.c

.PHONY: all reconfig clean clean-all unix kernel

all: $(BOARD_DIR)/Makefile
	$(MAKE) -C $(BOARD_DIR) all

unix kernel: $(BOARD_DIR)/Makefile
	$(MAKE) -C $(BOARD_DIR) $@

reconfig: $(KCONFIG)
	cd $(BOARD_DIR) && ../../../tools/kconfig/kconfig Config

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

$(BOARD_DIR)/Makefile: $(BOARD_DIR)/Config files.kconf devices.kconf Makefile.kconf $(KCONFIG)
	cd $(BOARD_DIR) && ../../../tools/kconfig/kconfig Config

$(KCONFIG): $(KCONFIG_SRCS)
	$(MAKE) -C $(KCONFIG_DIR)
