	.text
vendor_start:
	syscall
	sysret
	rdtscp
	monitor	%eax, %ecx, %edx
	mwait	%eax, %ecx
	vmrun
	vmrun	%eax
	vmmcall
	vmload
	vmload	%ax
	vmsave
	vmsave	%eax
	stgi
	clgi
	skinit
	skinit	%eax
	invlpga
	invlpga	%ax, %ecx
	movntsd	%xmm0, (%eax)
	movntss	%xmm0, (%eax)
	extrq	$8, $16, %xmm0
	extrq	%xmm0, %xmm1
	insertq	%xmm0, %xmm1
	insertq	$8, $16, %xmm0, %xmm1
	lzcntl	%eax, %ecx
	popcntl	%eax, %ecx
	xstore
	xcryptecb
	xcryptcbc
	xcryptctr
	xcryptcfb
	xcryptofb
	montmul
	xsha1
	xsha256
	xrng2
	xsha384
	xsha512
	sm2
	sm3
	sm4
	monitorx
	monitorx	%eax, %ecx, %edx
	mwaitx
	mwaitx	%eax, %ecx, %ebx
	clzero
	clzero	%ax
	rdpru
	vmcall
	vmlaunch
	vmresume
	vmxoff
	vmfunc
	vmclear	(%eax)
	vmptrld	(%eax)
	vmptrst	(%eax)
	vmxon	(%eax)
	vmread	%eax, %ecx
	vmwrite	%eax, %ecx
	invept	(%eax), %ecx
	invvpid	(%eax), %ecx
	getsec
	xgetbv
	xsetbv
	xsave	(%eax)
	xrstor	(%eax)
	xsaveopt	(%eax)
