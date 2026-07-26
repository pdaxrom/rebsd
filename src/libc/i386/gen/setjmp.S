/*
 * i386 setjmp family.
 *
 * jmp_buf words:
 *   0 ebx, 1 esi, 2 edi, 3 ebp, 4 eip, 5 esp,
 *   6 signal-mask flag, 7 signal mask.
 */

	.text
	.globl	_setjmp
	.type	_setjmp, @function
_setjmp:
	movl	4(%esp), %edx
	movl	%ebx, 0(%edx)
	movl	%esi, 4(%edx)
	movl	%edi, 8(%edx)
	movl	%ebp, 12(%edx)
	movl	0(%esp), %ecx
	movl	%ecx, 16(%edx)
	leal	4(%esp), %ecx
	movl	%ecx, 20(%edx)
	xorl	%eax, %eax
	ret

	.globl	_longjmp
	.type	_longjmp, @function
_longjmp:
	movl	4(%esp), %edx
	movl	8(%esp), %eax
	testl	%eax, %eax
	jne	1f
	incl	%eax
1:
	movl	0(%edx), %ebx
	movl	4(%edx), %esi
	movl	8(%edx), %edi
	movl	12(%edx), %ebp
	movl	20(%edx), %esp
	jmp	*16(%edx)

	.globl	setjmp
	.type	setjmp, @function
setjmp:
	movl	4(%esp), %edx
	movl	$1, 24(%edx)
	leal	28(%edx), %eax
	pushl	%eax
	pushl	$0
	pushl	$1
	call	sigprocmask
	addl	$12, %esp
	jmp	_setjmp

	.globl	longjmp
	.type	longjmp, @function
longjmp:
	movl	4(%esp), %edx
	leal	28(%edx), %eax
	pushl	$0
	pushl	%eax
	pushl	$3
	call	sigprocmask
	addl	$12, %esp
	jmp	_longjmp

	.globl	sigsetjmp
	.type	sigsetjmp, @function
sigsetjmp:
	movl	4(%esp), %edx
	cmpl	$0, 8(%esp)
	je	2f
	jmp	setjmp
2:
	movl	$0, 24(%edx)
	jmp	_setjmp

	.globl	siglongjmp
	.type	siglongjmp, @function
siglongjmp:
	movl	4(%esp), %edx
	cmpl	$0, 24(%edx)
	je	3f
	jmp	longjmp
3:
	jmp	_longjmp

	.section .note.GNU-stack, "", @progbits
