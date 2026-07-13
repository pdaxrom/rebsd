# ReBSD FAT filesystem

This directory contains the device-independent FAT filesystem code.  It is
mounted through the common VFS and block-device interfaces; USB mass storage,
SD/MMC, IDE and SATA drivers do not belong here.

The first implementation is deliberately read-only.  It supports FAT16 and
FAT32 volumes with 512-byte logical sectors, cluster-chain validation, 8.3
names, and VFAT long names up to ReBSD's 63-byte pathname-component limit.
ASCII letters are matched case-insensitively.  Non-ASCII long names are exposed
as UTF-8 but do not yet have Unicode case folding.

Current structural limits are a 2 GiB minus one byte maximum file size (the
kernel has signed 32-bit `off_t`) and a volume smaller than 120 GiB (the
synthetic inode encoding reserves the upper inode range for directories).
Free-space accounting is not scanned yet, so `df` reports no writable space on
these read-only mounts.
