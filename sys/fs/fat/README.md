# ReBSD FAT filesystem

This directory contains the device-independent FAT filesystem code.  It is
mounted through the common VFS and block-device interfaces; USB mass storage,
SD/MMC, IDE and SATA drivers do not belong here.

The implementation supports FAT16 and FAT32 volumes with 512-byte logical
sectors, cluster-chain validation, 8.3 names, and VFAT long names up to ReBSD's
63-byte pathname-component limit. ASCII letters are matched case-insensitively.
Non-ASCII long names are exposed as UTF-8 but do not yet have Unicode case
folding.

The writable implementation supports creating 8.3 regular files, extending
and overwriting their cluster chains, truncating them, and removing them. It
also supports creating and removing 8.3 directories, including nested
directories and on-disk `.`/`..` linkage. Existing VFAT long names remain
readable. Creating long names and renaming entries are deliberately deferred to
the next filesystem slice. Mounting with `-r` preserves strict read-only
behavior, and a backend that cannot be opened for writing cannot be mounted
read-write.

Current structural limits are a 2 GiB minus one byte maximum file size (the
kernel has signed 32-bit `off_t`) and a volume smaller than 120 GiB (the
synthetic inode encoding reserves the upper inode range for directories).
Free-space accounting is not scanned yet, so `df` does not yet report the
available cluster count accurately.
