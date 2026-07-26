/*
 * i386 cdecl to ReBSD int 0x80 ABI adapter.
 *
 * The first six words are passed in ebx, ecx, edx, esi, edi and ebp.
 * Eight-word calls place words six and seven at the interrupted user stack
 * pointer, where the kernel ABI adapter copies them into u.u_arg.
 */
#include <syscall.h>

#define ENTRY(s) \
	.globl s; \
	.type s, @function; \
s:

#define I386_SAVE \
	pushl %ebx; \
	pushl %esi; \
	pushl %edi; \
	pushl %ebp

#define I386_LOAD6 \
	movl 20(%esp), %ebx; \
	movl 24(%esp), %ecx; \
	movl 28(%esp), %edx; \
	movl 32(%esp), %esi; \
	movl 36(%esp), %edi; \
	movl 40(%esp), %ebp

#define I386_RESTORE \
	popl %ebp; \
	popl %edi; \
	popl %esi; \
	popl %ebx

#define I386_ERROR \
	movl %eax, errno; \
	movl $-1, %eax

#define SYS(s) \
	ENTRY(s); \
	I386_SAVE; \
	I386_LOAD6; \
	movl $SYS_ ## s, %eax; \
	int $0x80; \
	jnc 1f; \
	I386_ERROR; \
1:	I386_RESTORE; \
	ret

#define SYS2(s,k) \
	ENTRY(s); \
	I386_SAVE; \
	I386_LOAD6; \
	movl $SYS_ ## k, %eax; \
	int $0x80; \
	jnc 1f; \
	I386_ERROR; \
1:	I386_RESTORE; \
	ret

#define SYS8(s,k) \
	ENTRY(s); \
	I386_SAVE; \
	I386_LOAD6; \
	pushl 48(%esp); \
	pushl 48(%esp); \
	movl $SYS_ ## k, %eax; \
	int $0x80; \
	jnc 1f; \
	I386_ERROR; \
1:	addl $8, %esp; \
	I386_RESTORE; \
	ret
