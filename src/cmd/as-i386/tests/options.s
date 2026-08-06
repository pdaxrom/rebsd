	.text
options_start:
	crc32l	%eax, %ecx
	vmcall
	xstore
	prefetchw	(%eax)
	clflush	(%eax)
	xsaveopt	(%eax)
