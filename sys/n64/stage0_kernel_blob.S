        .section .rodata.kernel, "a", @progbits
        .balign 16
        .globl __n64_kernel_elf_start
__n64_kernel_elf_start:
        .incbin N64_KERNEL_ELF
        .balign 16
        .globl __n64_kernel_elf_end
__n64_kernel_elf_end:
