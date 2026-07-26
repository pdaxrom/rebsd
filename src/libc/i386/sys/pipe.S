#include "SYS.h"

ENTRY(pipe)
	pushl	%ebx
	movl	8(%esp), %ebx
	movl	$SYS_pipe, %eax
	int	$0x80
	jc	1f
	movl	%eax, 0(%ebx)
	movl	%edx, 4(%ebx)
	xorl	%eax, %eax
	popl	%ebx
	ret
1:
	movl	%eax, errno
	movl	$-1, %eax
	popl	%ebx
	ret

	.section .note.GNU-stack, "", @progbits
