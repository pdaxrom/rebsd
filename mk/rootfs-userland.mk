# Architecture-independent contents of the full ReBSD root filesystem.
#
# Machine ports add only tools that actually implement a machine-specific
# object format or hardware interface.  Keeping the portable selection here
# prevents board makefiles from growing private copies of the userland policy.

REBSD_ROOTFS_LIBS = libc libm libutil libtermlib libcurses libvmf \
                    libreadline libtcl libmagic
REBSD_ROOTFS_LIBC_DIRS = gen stdio stdlib string inet net compat runtime sys
REBSD_ROOTFS_SUBDIRS = cmd

REBSD_ROOTFS_CMD_SUBDIRS = basic calendar chown chroot compress date2 deco \
    dhclient diff dmesg emg env fdisk find fold forth fsck fsck.fat fstat getty \
    gpt hostname id ifconfig inetd init login ls make man md5 med mkfs \
    mkfs.fat mknod mkpasswd mount more netstat ntpdate pdc picoc ping printf pstat \
    ptytest reboot renice retroforth route sed setty sh shutdown sl smux \
    stty sysctl tcl telnet telnetd test wget umount uname xargs
# These portable commands are intentionally not part of the historical
# src/cmd/Makefile SUBDIR_ALL set.  Add them through its public extension
# point so every full-rootfs architecture builds the same programs.
REBSD_ROOTFS_CMD_EXTRA_SUBDIRS = deco ptytest tcl
REBSD_ROOTFS_CMD_STDS = basename cal cat cb chgrp chmod cmp col comm cp dd \
    diskspeed du echo ed fgrep free grep head hostid iostat join kill last ln \
    mesg mkdir mv nice od pagesize pr printenv ps pwd rev rm rmail rmdir \
    sleep sort split strace sum sync tail tar tee time touch vmstat top tr \
    tsort tty uniq uptime vm-pressure-smoke w wc whereis who
REBSD_ROOTFS_CMD_NSTDS = egrep expr file
REBSD_ROOTFS_CMD_OPERATORS = df
REBSD_ROOTFS_CMD_SCRIPTS = false nohup true

REBSD_ROOTFS_USR_BIN_FILES = apropos awk basename basic cal calendar cb \
    chgrp cmp col comm compress deco diff diskspeed du ed egrep emg env \
    fgrep file find fold forth free grep groups head hostid id iostat join \
    last make man md5 med mesg more nice nohup od pagesize pdc picoc pr \
    printf printenv ps ptytest renice retroforth rev rmail setty sl smux \
    sort split strace \
    sum sysctl tail tar tcl tee telnet time top touch tsort tty uncompress \
    uniq uptime vm-pressure-smoke vmstat w wc wget whatis whereis who whoami \
    xargs zcat
REBSD_ROOTFS_USR_LIBEXEC_FILES = bigram code
REBSD_ROOTFS_USR_SBIN_FILES = chown chroot inetd mkpasswd ntpdate pstat \
    updatedb

REBSD_ROOTFS_CAT1_PAGES = apropos awk basename cal cat cb chgrp chmod cmp col \
    comm compress cp date dd df diff du echo ed expr false file find fold \
    free grep head hostid iostat join kill last ln login ls make man mesg \
    mkdir more mv nice od pagesize pr ps printenv pwd rev rm rmail rmdir sed \
    sh sleep sort split sum tail tar tee time top touch tr true tsort tty \
    uniq uptime vmstat w wc whatis who
REBSD_ROOTFS_CAT5_PAGES = magic
REBSD_ROOTFS_CMD_CAT1_SOURCES = emg:emg env:env sl:sl wget:wget
REBSD_ROOTFS_CAT1_ALIASES = egrep:grep fgrep:grep uncompress:compress \
    zcat:compress nohup:nice
REBSD_ROOTFS_CAT8_PAGES = fsck getty sync
REBSD_ROOTFS_CAT8_ALIASES = fsck.ufs:fsck mkfs.ufs:mkfs \
    fastboot:reboot halt:reboot poweroff:reboot bootloader:reboot
