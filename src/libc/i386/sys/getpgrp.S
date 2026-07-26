#include "SYS.h"

ENTRY(getpgrp)
	pushl	%ebx
	xorl	%ebx, %ebx
	movl	$SYS_getpgrp, %eax
	int	$0x80
	jnc	1f
	movl	%eax, errno
	movl	$-1, %eax
1:
	popl	%ebx
	ret

	.section .note.GNU-stack, "", @progbits
