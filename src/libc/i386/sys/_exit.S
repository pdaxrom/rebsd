#include "SYS.h"

ENTRY(_exit)
	movl	4(%esp), %ebx
	movl	$SYS_exit, %eax
	int	$0x80
1:
	jmp	1b

	.section .note.GNU-stack, "", @progbits
