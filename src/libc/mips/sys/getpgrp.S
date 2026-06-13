/*
 * Copyright (c) 2026
 * All rights reserved.
 */
#include "SYS.h"

ENTRY(getpgrp)
		/*
		 * The kernel entry is the historical BSD getpgrp(pid), where
		 * pid 0 means the current process.  The installed public header
		 * exposes the POSIX getpgrp(void), so do not pass a stale caller
		 * register as pid.
		 */
		move	$a0, $zero
		.set	noreorder
		syscall	SYS_getpgrp
		lui	$t1, %hi(errno)
		sw      $t0, %lo(errno)($t1)
		.set	reorder
		jr	$ra
