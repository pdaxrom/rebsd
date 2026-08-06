	.text
	.globl external_function
	.type external_function,@function
external_function:
	movl external_object,%eax
	ret
	.size external_function,.-external_function

	.data
	.globl external_object
	.type external_object,@object
external_object:
	.long 0x12345678
	.size external_object,.-external_object
