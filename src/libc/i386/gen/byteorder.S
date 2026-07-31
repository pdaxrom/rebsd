/*
 * Host/network byte-order conversion for little-endian i386.
 */

	.text
	.globl	htonl
	.globl	ntohl
	.type	htonl, @function
	.type	ntohl, @function
htonl:
ntohl:
	movl	4(%esp), %eax
	bswap	%eax
	ret

	.globl	htons
	.globl	ntohs
	.type	htons, @function
	.type	ntohs, @function
htons:
ntohs:
	movl	4(%esp), %eax
	rolw	$8, %ax
	andl	$0xffff, %eax
	ret

	.section .note.GNU-stack, "", @progbits
