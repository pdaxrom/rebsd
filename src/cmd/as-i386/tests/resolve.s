	.text
	.globl external_function
	.type external_function,@function
external_function:
	movl $0x89abcdef,pcc_local_common
	movl external_object,%eax
	ret
	.size external_function,.-external_function

	.local pcc_local_common
	.comm pcc_local_common,4,4

	.data
	.globl external_object
	.type external_object,@object
external_object:
	.long 0x12345678
	.size external_object,.-external_object
