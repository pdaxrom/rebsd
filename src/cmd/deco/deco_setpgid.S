#include <syscall.h>

	.text
	.globl	deco_setpgid
	.type	deco_setpgid, @function
deco_setpgid:
	.set	noreorder
	syscall	SYS_setpgrp
	lui	$t1, %hi(errno)
	sw	$t0, %lo(errno)($t1)
	.set	reorder
	jr	$ra
