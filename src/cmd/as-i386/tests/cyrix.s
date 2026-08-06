	.text
cyrix_start:
	smint
	smintold
	svdc	%ds, (%eax)
	rsdc	(%eax), %ds
	svldt	(%eax)
	rsldt	(%eax)
	svts	(%eax)
	rsts	(%eax)
