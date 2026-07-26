#include "SYS.h"

ENTRY(sigaction)
	pushl	%ebx
	pushl	%esi
	movl	12(%esp), %ebx
	movl	16(%esp), %ecx
	movl	20(%esp), %edx
	movl	$sigtramp, %esi
	movl	$SYS_sigaction, %eax
	int	$0x80
	jnc	1f
	movl	%eax, errno
	movl	$-1, %eax
1:
	popl	%esi
	popl	%ebx
	ret

	.type sigtramp, @function
sigtramp:
	movl	8(%esp), %ebx
	movl	$SYS_sigreturn, %eax
	int	$0x80
	movl	$127, %ebx
	movl	$SYS_exit, %eax
	int	$0x80
2:
	jmp	2b

	.section .note.GNU-stack, "", @progbits
