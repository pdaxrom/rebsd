/*
 * Linux/x86 boot protocol 2.02 setup image.
 *
 * Linux/x86 loaders, including QEMU -kernel and GRUB Legacy, enter at file
 * offset 0x200 in real mode.  The protected-mode payload immediately
 * following this 4608-byte setup image is loaded at physical address
 * 0x00100000.
 */

#ifndef SETUP_SECTS
#define SETUP_SECTS 8
#endif

#define BOOT_PARAMS_SEG_DELTA   0x20
#define BOOT_PARAMS_E820_COUNT  0x01e8
#define BOOT_PARAMS_E820_TABLE  0x02d0
#define E820_ENTRY_BYTES        20
#define E820_MAX_ENTRIES        128
#define SMAP_MAGIC              0x534d4150
#define KERNEL_LOAD_ADDRESS     0x00100000

    .section .setup, "ax", @progbits
    .code16

    /*
     * Fixed Linux setup header offsets.  mkboot.py fills syssize after the
     * protected-mode payload is converted to a flat binary.
     */
    .org 0x01f1
    .byte SETUP_SECTS
    .word 0x0000                       /* root_flags */
    .long 0x00000000                   /* syssize, patched by mkboot.py */
    .word 0x0000                       /* ram_size */
    .word 0xffff                       /* vid_mode: normal */
    .word 0x0000                       /* root_dev */
    .word 0xaa55                       /* boot_flag */

    .globl setup_start
setup_start:
    .byte 0xeb
    .byte setup_trampoline - . - 1
    .ascii "HdrS"
    .word 0x0202                       /* protocol version */
    .long 0x00000000                   /* realmode_swtch */
    .word 0x0000                       /* start_sys_seg */
    .word kernel_version - 0x0200      /* kernel_version string offset */
    .byte 0x00                         /* type_of_loader */
    .byte 0x01                         /* loadflags: LOAD_HIGH */
    .word 0x0000                       /* setup_move_size */
    .long KERNEL_LOAD_ADDRESS          /* code32_start */
    .long 0x00000000                   /* ramdisk_image */
    .long 0x00000000                   /* ramdisk_size */
    .long 0x00000000                   /* bootsect_kludge */
    .word 0x0000                       /* heap_end_ptr */
    .byte 0x00                         /* ext_loader_ver */
    .byte 0x00                         /* ext_loader_type */
    .long 0x00000000                   /* cmd_line_ptr */

    /*
     * Keep the Linux zero-page E820 table clear of executable setup code.
     * The first jump must stay short because HdrS begins at offset 0x202.
     */
    .org 0x0270
setup_trampoline:
    jmp setup_entry

    .org 0x1000
setup_entry:
    cli
    cld

    /*
     * The boot loader enters at CS = boot_params segment + 0x20 while the
     * boot parameter/header block begins 0x20 paragraphs earlier.
     */
    movw    %cs, %ax
    subw    $BOOT_PARAMS_SEG_DELTA, %ax
    movw    %ax, %ds
    movw    %ax, %es
    movw    %ax, %ss
    movw    $0x8000, %sp

    movw    %ds, %ax
    movzwl  %ax, %esi
    shll    $4, %esi                    /* physical boot_params address */

    /* Fast A20 gate, supported by the target IBM and QEMU PC machines. */
    inb     $0x92, %al
    andb    $0xfe, %al
    orb     $0x02, %al
    outb    %al, $0x92

    /*
     * Always refresh the E820 map.  This gives QEMU and GRUB Legacy the same
     * normalized input even if a loader supplied fewer fields.
     */
    movb    $0, BOOT_PARAMS_E820_COUNT
    movw    $BOOT_PARAMS_E820_TABLE, %di
    xorl    %ebx, %ebx

1:
    movw    %ds, %ax
    movw    %ax, %es
    movl    $0xe820, %eax
    movl    $SMAP_MAGIC, %edx
    movl    $E820_ENTRY_BYTES, %ecx
    int     $0x15
    jc      2f
    cmpl    $SMAP_MAGIC, %eax
    jne     2f

    incb    BOOT_PARAMS_E820_COUNT
    addw    $E820_ENTRY_BYTES, %di
    cmpb    $E820_MAX_ENTRIES, BOOT_PARAMS_E820_COUNT
    jae     2f
    testl   %ebx, %ebx
    jne     1b

2:
    cli

    /* Patch physical addresses into the position-independent setup data. */
    movl    %esi, %eax
    addl    $gdt, %eax
    movl    %eax, gdt_descriptor + 2

    movl    %esi, %eax
    addl    $protected_entry, %eax
    movl    %eax, protected_jump

    lgdt    gdt_descriptor
    movl    %cr0, %eax
    orl     $0x00000001, %eax
    movl    %eax, %cr0
    ljmpl   *protected_jump

    .align 8
gdt:
    .quad 0x0000000000000000
    .quad 0x00cf9a000000ffff           /* selector 0x08: flat code */
    .quad 0x00cf92000000ffff           /* selector 0x10: flat data */
    .quad 0x00cffa000000ffff           /* selector 0x1b: user code */
    .quad 0x00cff2000000ffff           /* selector 0x23: user data */
    .quad 0x0000000000000000           /* selector 0x28: runtime TSS */
gdt_end:

gdt_descriptor:
    .word gdt_end - gdt - 1
    .long 0

protected_jump:
    .long 0
    .word 0x0008

    .code32
protected_entry:
    movw    $0x0010, %ax
    movw    %ax, %ds
    movw    %ax, %es
    movw    %ax, %fs
    movw    %ax, %gs
    movw    %ax, %ss
    leal    0x8000(%esi), %esp

    movl    $KERNEL_LOAD_ADDRESS, %eax
    jmp     *%eax

    .code16
    .org 0x1180
kernel_version:
    .asciz "ReBSD i686 early"

    .org 0x11ff
    .byte 0
