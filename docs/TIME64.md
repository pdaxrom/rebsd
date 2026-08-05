# Native 64-bit time ABI

ReBSD uses a signed 64-bit `time_t` on every supported 32-bit architecture.
The native system-call ABI carries that type directly in `timeval`, `timespec`,
`stat`, SysV shared-memory timestamps, process start times, and userland time
interfaces.  Dates after 2038 therefore do not use a parallel `time64` syscall
family.

There is no time32 compatibility ABI.  ReBSD has no released legacy binaries
whose old structure layouts must be retained, so the original `stat`, `lstat`,
and `fstat` syscall numbers now expose the native time64 layout.  This follows
the current NetBSD model for new binaries while intentionally omitting
NetBSD's separate compatibility syscalls for old binaries.

All native structures crossing the kernel/user boundary have an explicit,
compiler-independent 32-bit layout.  In particular, `timeval` and `timespec`
are 16 bytes, `itimerval` is 32 bytes, and `rusage` is 88 bytes.  Explicit
reserved words also fix the 32-bit layouts of `shmid_ds`, `kinfo_proc`, and
`kinfo_procfile`.  Header assertions make both the GCC-built kernel and the
GCC/PCC-built userland reject any accidental layout drift at compile time.
The reserved words are not compatibility shims: they define the only native
ABI and must be zero when the kernel returns a structure to userland.

Calendar conversion uses Gregorian 400-year eras rather than loops bounded by
a 32-bit epoch.  The common RTC layer accepts post-2038 dates.  Individual RTC
drivers still reject values their hardware cannot represent: MC146818 and
PCF8563 devices without a known century extension stop at 2099, and the JZ4780
seconds counter stops at its unsigned 32-bit hardware limit.

Named timezone files use standard TZif v2: `zic` writes the required 32-bit
compatibility block followed by the authoritative 64-bit transition block,
and libc selects the latter.  The regression suite verifies PST/PDT
transitions in 2040 through a generated zone file.

`/usr/sbin/ntpdate` is a common socket-based SNTP client for every platform
with IPv4 connectivity.  It validates the server reply, uses the four NTP
timestamps to compensate for network delay, reconstructs NTP eras around the
64-bit system clock, and updates the system time through `settimeofday`.
That syscall already synchronizes every writable RTC through the common TODR
layer.  The old WIZnet-only demonstration is not a second implementation.

## Filesystem boundary

The in-memory VFS and inode timestamps use `time_t`, but the existing ReBSD UFS
disk format remains unchanged.  Its superblock and 64-byte disk inode store
signed 32-bit timestamps.  This is an explicit serialization boundary, not a
second process ABI.

This filesystem is a historical ReBSD/RetroBSD format with `FS<<` and `>>FS`
superblock magic, 512-byte filesystem blocks, and its own inode layout.  It is
not NetBSD UFS1/UFS2, Linux UFS, or macOS UFS, so those systems do not mount a
ReBSD image directly.  A future interoperable and post-2038 writable disk
format requires a separately versioned filesystem or a conversion tool; it
must not be introduced by silently changing the current format.

FAT timestamps are likewise constrained by the FAT on-disk specification even
though the VFS representation is 64-bit.

## Regression gates

- Common RTC tests cover the first second after the signed 32-bit limit and
  the Gregorian year 2100 rule.  They also cover NTP era reconstruction and
  four-timestamp offset calculation with a 2040 timestamp.
- The i686 QEMU boot smoke sets and reads the year 2040 through the real
  userland and syscall path.
- The common full MIPS rootfs smoke performs the same 2040 check, including
  PCC-built Malta, Malta64, and Malta little-endian configurations.
