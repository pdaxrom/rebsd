#include "SYS.h"

ENTRY(ptrace)
	movl	$0, errno
	I386_SAVE
	I386_LOAD6
	movl	$SYS_ptrace, %eax
	int	$0x80
	jnc	1f
	I386_ERROR
1:
	I386_RESTORE
	ret

	.section .note.GNU-stack, "", @progbits
