	.text
	.globl reloc16_start
reloc16_start:
	addr16 movl	external16, %eax
	.word	external16
	.byte	external8
