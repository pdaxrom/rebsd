	.text
	.arch bdver4
	monitorx
	mwaitx
	.arch znver1
	clzero
	.arch znver2
	rdpru
	.arch btver2
	xsaveopt	(%eax)
	movbel	(%eax), %ecx
