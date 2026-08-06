	.text
	.globl pic_function
	.type pic_function,@function
pic_function:
	call 1f
1:
	popl %ebx
	addl $_GLOBAL_OFFSET_TABLE_+[.-1b],%ebx
	leal pic_object@GOTOFF(%ebx),%eax
	ret
	.size pic_function,.-pic_function

	.data
	.local pic_object
	.type pic_object,@object
pic_object:
	.long 1
	.size pic_object,.-pic_object
