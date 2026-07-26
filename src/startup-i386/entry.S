/*
 * ReBSD i386 exec entry ABI:
 *     ebx = argc, ecx = argv, edx = envp, esp = user stack.
 *
 * Convert it once to the normal i386 cdecl ABI and enter the shared crt0.
 */
	.section .text, "ax", @progbits
	.code32
	.globl _start
	.type _start, @function
_start:
	xorl	%ebp, %ebp
	pushl	%edx
	pushl	%ecx
	pushl	%ebx
	call	__rebsd_start
1:
	hlt
	jmp	1b
	.size _start, . - _start

	.section .note.GNU-stack, "", @progbits
