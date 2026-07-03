        .section .kernel_blob,"a",@progbits
        .balign 16
        .globl  __malta64_kernel_blob_start
        .globl  __malta64_kernel_blob_end
__malta64_kernel_blob_start:
        .incbin "unix.bin"
        .balign 16
__malta64_kernel_blob_end:
