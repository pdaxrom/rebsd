CC		= gcc -g
CFLAGS		= -O -Wall
DESTDIR		= /usr/local
OBJDIR		?= .
OBJNAMES	= fsutil.o superblock.o block.o inode.o create.o check.o \
                  file.o mount.o manifest.o
OBJS		= $(addprefix $(OBJDIR)/,$(OBJNAMES))
PROGNAME	= fsutil
PROG		= $(OBJDIR)/$(PROGNAME)

# Enable mount option.
#ENABLE_FUSE    = 1

# For Mac OS X
ifneq ($(wildcard /usr/local/lib/pkgconfig),)
    FUSE_PATH   = /usr/local/lib/pkgconfig
endif

# Fuse
ifneq ($(ENABLE_FUSE),)
    CFLAGS       += -DENABLE_FUSE
    MOUNT_CFLAGS = -DFUSE_USE_VERSION=26
    MOUNT_CFLAGS += $(shell PKG_CONFIG_PATH=$(FUSE_PATH) pkg-config fuse --cflags)
    LIBS         += $(shell PKG_CONFIG_PATH=$(FUSE_PATH) pkg-config fuse --libs)
endif

all:		$(PROG)

install:	$(PROG)
		install -s $(PROG) ${DESTDIR}/bin/$(PROGNAME)
clean:
		rm -f $(OBJS) $(PROG)
		rm -rf $(PROG).dSYM

$(PROG):	$(OBJS) | $(OBJDIR)
		$(CC) $(LDFLAGS) -o $@ $(OBJS) $(LIBS)

root.bin:       $(PROG)
		$(PROG) -n16384 -s2048 $@
		$(PROG) -v $@
		$(PROG) -c $@

swap.bin:
		dd bs=1k count=2048 < /dev/zero > $@

$(OBJDIR):
		mkdir -p $@

$(OBJDIR)/mount.o: mount.c bsdfs.h | $(OBJDIR)
		$(CC) $(CFLAGS) $(MOUNT_CFLAGS) -c -o $@ $<

$(OBJDIR)/block.o: block.c bsdfs.h
$(OBJDIR)/check.o: check.c bsdfs.h
$(OBJDIR)/create.o: create.c bsdfs.h
$(OBJDIR)/file.o: file.c bsdfs.h
$(OBJDIR)/fsutil.o: fsutil.c bsdfs.h manifest.h
$(OBJDIR)/inode.o: inode.c bsdfs.h
$(OBJDIR)/manifest.o: manifest.c bsdfs.h manifest.h
$(OBJDIR)/superblock.o: superblock.c bsdfs.h

$(OBJDIR)/%.o: %.c | $(OBJDIR)
		$(CC) $(CFLAGS) -c -o $@ $<
