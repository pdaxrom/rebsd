/*
 * Linux/x86 boot protocol 2.02 setup image.
 *
 * QEMU -kernel and LILO image= both enter at file offset 0x200 in real
 * mode.  The protected-mode payload immediately following this 4608-byte
 * setup image is loaded at physical address 0x00100000.
 */

#ifndef SETUP_SECTS
#define SETUP_SECTS 8
#endif

#define BOOT_PARAMS_SEG_DELTA   0x20
#define BOOT_PARAMS_E820_COUNT  0x01e8
#define BOOT_PARAMS_E820_TABLE  0x02d0
#define BOOT_PARAMS_LOADER_TYPE 0x0210
#define BOOT_PARAMS_EXT_LOADER_VER 0x0226
#define BOOT_PARAMS_EXT_LOADER_TYPE 0x0227
#define BIOS_LOADER_TYPE        0x00ff
#define BIOS_LOADER_VERSION     0x0052
#define BIOS_LOADER_EXTENSION   0x0042
#define E820_ENTRY_BYTES        20
#define E820_MAX_ENTRIES        128
#define SMAP_MAGIC              0x534d4150
#define KERNEL_LOAD_ADDRESS     0x00100000
#define BIOS_SETUP_SEGMENT      0x9000
#define BIOS_SETUP_ENTRY_SEGMENT 0x9020
#define BIOS_KERNEL_STAGING_SEGMENT 0x1000
#define BIOS_KERNEL_CHUNK_SECTORS 128
#define BIOS_FLOPPY_SECTORS     2880
#define BIOS_KERNEL_START_LBA   (SETUP_SECTS + 1)
#define BIOS_KERNEL_MAX_SECTORS (BIOS_FLOPPY_SECTORS - BIOS_KERNEL_START_LBA)

    .section .setup, "ax", @progbits
    .code16

    /*
     * The first sector doubles as a legacy BIOS boot sector.  Linux/x86
     * loaders ignore this code and enter setup_start at file offset 0x200.
     * BIOS starts here at 0000:7c00; relocate the zero page to 0x90000,
     * load the remaining setup sectors, and enter the same setup_start.
     */
    .org 0
bios_boot_entry:
    jmp     bios_boot_start
    nop

bios_boot_start:
    cli
    cld
    xorw    %ax, %ax
    movw    %ax, %ss
    movw    $0x7c00, %sp
    movw    %ax, %ds
    movw    $0x7c00, %si
    movw    $BIOS_SETUP_SEGMENT, %ax
    movw    %ax, %es
    xorw    %di, %di
    movw    $256, %cx
    rep movsw

    movw    %ax, %ds
    sti
    movb    %dl, bios_boot_drive
    movw    $3, bios_boot_retries

bios_boot_read_setup:
    movw    $BIOS_SETUP_ENTRY_SEGMENT, %ax
    movw    %ax, %es
    xorw    %bx, %bx
    movb    $0x02, %ah
    movb    $SETUP_SECTS, %al
    xorw    %cx, %cx
    movb    $2, %cl
    xorw    %dx, %dx
    movb    bios_boot_drive, %dl
    int     $0x13
    jnc     bios_boot_setup_ready

    xorw    %ax, %ax
    movb    bios_boot_drive, %dl
    int     $0x13
    decw    bios_boot_retries
    jne     bios_boot_read_setup

    movw    $bios_boot_error, %si
    call    bios_puts
bios_boot_halt:
    cli
    hlt
    jmp     bios_boot_halt

bios_boot_setup_ready:
    movb    $BIOS_LOADER_TYPE, BOOT_PARAMS_LOADER_TYPE
    movb    $BIOS_LOADER_VERSION, BOOT_PARAMS_EXT_LOADER_VER
    movb    $BIOS_LOADER_EXTENSION, BOOT_PARAMS_EXT_LOADER_TYPE
    ljmp    $BIOS_SETUP_ENTRY_SEGMENT, $0x0000

bios_puts:
    lodsb
    testb   %al, %al
    jz      bios_puts_done
    movb    $0x0e, %ah
    movw    $0x0007, %bx
    int     $0x10
    jmp     bios_puts
bios_puts_done:
    ret

bios_boot_retries:
    .word 0
bios_boot_drive:
    .byte 0
bios_boot_error:
    .asciz "ReBSD setup read error"

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

/*
 * Read the protected-mode payload through legacy INT 13h, one sector at a
 * time.  Each 64 KiB chunk is staged at 0x10000 and moved to its final
 * high-memory address with the standard INT 15h/AH=87 service used by
 * historical bzImage loaders.  Disk access remains CHS-only, so no Enhanced
 * Disk Drive extension is required.
 */
bios_load_kernel:
    movl    0x01f4, %eax                /* syssize in 16-byte paragraphs */
    testl   %eax, %eax
    jz      bios_load_failed
    addl    $31, %eax
    shrl    $5, %eax                    /* sectors = ceil(syssize / 32) */
    cmpl    $BIOS_KERNEL_MAX_SECTORS, %eax
    ja      bios_load_failed
    movw    %ax, bios_sector_count

    movw    $18, bios_sectors_per_track
    movw    $2, bios_head_count
    movb    bios_boot_drive, %dl
    movb    $0x08, %ah
    int     $0x13
    jc      bios_geometry_ready
    movb    %cl, %al
    andb    $0x3f, %al
    jz      bios_load_failed
    xorb    %ah, %ah
    movw    %ax, bios_sectors_per_track
    movb    %dh, %al
    incb    %al
    xorb    %ah, %ah
    movw    %ax, bios_head_count

bios_geometry_ready:
    movw    bios_sectors_per_track, %ax
    mulw    bios_head_count
    testw   %dx, %dx
    jnz     bios_load_failed
    movw    %ax, bios_sectors_per_cylinder
    movw    $BIOS_KERNEL_START_LBA, bios_current_lba
    movw    $BIOS_KERNEL_STAGING_SEGMENT, bios_load_segment
    movw    $0, bios_chunk_sectors
    movb    $0x10, bios_move_dst_base + 2

bios_load_next:
    cmpw    $0, bios_sector_count
    je      bios_load_final_chunk

    movw    bios_current_lba, %ax
    xorw    %dx, %dx
    divw    bios_sectors_per_cylinder
    cmpw    $1023, %ax
    ja      bios_load_failed
    movw    %ax, bios_cylinder
    movw    %dx, %ax
    xorw    %dx, %dx
    divw    bios_sectors_per_track
    movb    %al, bios_head
    incw    %dx
    movb    %dl, bios_sector
    movw    $3, bios_read_retries

bios_read_sector:
    movw    bios_load_segment, %ax
    movw    %ax, %es
    xorw    %bx, %bx
    movw    bios_cylinder, %ax
    movb    %al, %ch
    movb    bios_sector, %cl
    andb    $0x03, %ah
    shlb    $6, %ah
    orb     %ah, %cl
    movb    bios_head, %dh
    movb    bios_boot_drive, %dl
    movb    $0x02, %ah
    movb    $1, %al
    int     $0x13
    jnc     bios_sector_loaded

    xorw    %ax, %ax
    movb    bios_boot_drive, %dl
    int     $0x13
    decw    bios_read_retries
    jne     bios_read_sector
    jmp     bios_load_failed

bios_sector_loaded:
    incw    bios_current_lba
    addw    $0x20, bios_load_segment
    incw    bios_chunk_sectors
    decw    bios_sector_count
    cmpw    $BIOS_KERNEL_CHUNK_SECTORS, bios_chunk_sectors
    jb      bios_load_next

    movw    $0x8000, %cx                /* words in a full 64 KiB chunk */
    call    bios_move_kernel_chunk
    jc      bios_load_failed
    movw    $0, bios_chunk_sectors
    movw    $BIOS_KERNEL_STAGING_SEGMENT, bios_load_segment
    jmp     bios_load_next

bios_load_final_chunk:
    movw    bios_chunk_sectors, %cx
    testw   %cx, %cx
    jz      bios_load_done
    shlw    $8, %cx                     /* 512-byte sectors to words */
    call    bios_move_kernel_chunk
    jc      bios_load_failed

bios_load_done:
    clc
    ret
bios_load_failed:
    stc
    ret

bios_move_kernel_chunk:
    pushw   %ax
    pushw   %bx
    pushw   %dx
    pushw   %si
    pushw   %di
    pushw   %es
    movw    %ds, %ax
    movw    %ax, %es
    movw    $bios_move_gdt, %si
    movw    $0x8700, %ax
    int     $0x15
    jc      bios_move_kernel_failed
    incb    bios_move_dst_base + 2      /* advance destination by 64 KiB */
    clc
    jmp     bios_move_kernel_return
bios_move_kernel_failed:
    stc
bios_move_kernel_return:
    popw    %es
    popw    %di
    popw    %si
    popw    %dx
    popw    %bx
    popw    %ax
    ret

.align 8
bios_move_gdt:
    .word 0, 0, 0, 0
    .word 0, 0, 0, 0
bios_move_src:
    .word 0xffff
bios_move_src_base:
    .byte 0x00, 0x00, 0x01             /* base 0x00010000 */
    .byte 0x93
    .word 0x0000
bios_move_dst:
    .word 0xffff
bios_move_dst_base:
    .byte 0x00, 0x00, 0x10             /* base 0x00100000 */
    .byte 0x93
    .word 0x0000
    .word 0, 0, 0, 0                   /* BIOS code descriptor */
    .word 0, 0, 0, 0                   /* BIOS data descriptor */

bios_sectors_per_track:
    .word 0
bios_head_count:
    .word 0
bios_sectors_per_cylinder:
    .word 0
bios_current_lba:
    .word 0
bios_sector_count:
    .word 0
bios_load_segment:
    .word 0
bios_chunk_sectors:
    .word 0
bios_cylinder:
    .word 0
bios_read_retries:
    .word 0
bios_head:
    .byte 0
bios_sector:
    .byte 0

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

    cmpb    $BIOS_LOADER_TYPE, BOOT_PARAMS_LOADER_TYPE
    jne     setup_kernel_ready
    cmpb    $BIOS_LOADER_VERSION, BOOT_PARAMS_EXT_LOADER_VER
    jne     setup_kernel_ready
    cmpb    $BIOS_LOADER_EXTENSION, BOOT_PARAMS_EXT_LOADER_TYPE
    jne     setup_kernel_ready
    sti
    call    bios_load_kernel
    cli
    jnc     setup_kernel_ready
    movw    $bios_kernel_error, %si
    call    bios_puts
    jmp     bios_boot_halt

setup_kernel_ready:
    movw    %ds, %ax
    movzwl  %ax, %esi
    shll    $4, %esi                    /* physical boot_params address */

    /* Fast A20 gate, supported by the target IBM and QEMU PC machines. */
    inb     $0x92, %al
    andb    $0xfe, %al
    orb     $0x02, %al
    outb    %al, $0x92

    /*
     * Always refresh the E820 map.  This gives QEMU direct boot and LILO
     * the same normalized input even if a loader supplied fewer fields.
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
bios_kernel_error:
    .asciz "ReBSD kernel read error"

    .org 0x11ff
    .byte 0
