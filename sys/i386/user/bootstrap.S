#include "user_bootstrap.h"

    .section .text, "ax", @progbits
    .code32
    .globl _start
    .type _start, @function
_start:
    cmpl    $2, %ebx
    jne     .Lfailed
    testl   $7, %esp
    jne     .Lfailed
    cmpl    $0, (%esp)
    jne     .Lfailed
    cmpl    %ecx, I386_BOOTSTRAP_USER_TOP_WORD
    jne     .Lfailed
    cmpl    $0, 8(%ecx)
    jne     .Lfailed
    movl    (%ecx), %esi
    cmpl    $0x74696e69, (%esi)
    jne     .Lfailed
    cmpb    $0, 4(%esi)
    jne     .Lfailed
    movl    4(%ecx), %esi
    cmpw    $0x002d, (%esi)
    jne     .Lfailed
    testl   %edx, %edx
    jne     .Lfailed
    cmpl    $I386_BOOTSTRAP_USER_DATA_MAGIC, bootstrap_data
    jne     .Lfailed
    cmpl    $0, bootstrap_bss
    jne     .Lfailed
    movl    $I386_BOOTSTRAP_USER_DATA_MAGIC, bootstrap_bss
    cmpl    $I386_BOOTSTRAP_USER_DATA_MAGIC, bootstrap_bss
    jne     .Lfailed
    movl    $20, %eax
    int     $0x80
    cmpl    $1, %eax
    jne     .Lfailed
    movl    $5, %eax
    movl    $bootstrap_init_path, %ebx
    xorl    %ecx, %ecx
    xorl    %edx, %edx
    int     $0x80
    jnc     .Lvfs_fd_opened
    cmpl    $I386_BOOTSTRAP_ENOENT, %eax
    jne     .Lfailed
    jmp     .Lvfs_fd_done
.Lvfs_fd_opened:
    testl   %eax, %eax
    jne     .Lfailed
    movl    %eax, %ebx
    movl    $3, %eax
    movl    $bootstrap_read_data, %ecx
    movl    $4, %edx
    int     $0x80
    jc      .Lfailed
    cmpl    $4, %eax
    jne     .Lfailed
    cmpl    $0x464c457f, bootstrap_read_data
    jne     .Lfailed
    movl    $19, %eax
    xorl    %ebx, %ebx
    movl    $1, %ecx
    xorl    %edx, %edx
    xorl    %esi, %esi
    int     $0x80
    jc      .Lfailed
    cmpl    $1, %eax
    jne     .Lfailed
    movl    $3, %eax
    xorl    %ebx, %ebx
    movl    $bootstrap_read_data, %ecx
    movl    $3, %edx
    int     $0x80
    jc      .Lfailed
    cmpl    $3, %eax
    jne     .Lfailed
    cmpb    $0x45, bootstrap_read_data
    jne     .Lfailed
    cmpb    $0x4c, bootstrap_read_data + 1
    jne     .Lfailed
    cmpb    $0x46, bootstrap_read_data + 2
    jne     .Lfailed
    movl    $1, bootstrap_vfs_fd_active
.Lvfs_fd_done:
    movl    $2, %eax
    int     $0x80
    jc      .Lfailed
    testl   %eax, %eax
    jz      .Lchild1
    cmpl    $2, %eax
    jne     .Lfailed
    movl    $I386_BOOTSTRAP_FORK_PARENT_MAGIC, %ebx
    int     $0x30
    subl    $4, %esp
    movl    $0, (%esp)
    movl    $7, %eax
    movl    $2, %ebx
    movl    %esp, %ecx
    movl    $1, %edx
    xorl    %esi, %esi
    int     $0x80
    jc      .Lfailed
    testl   %eax, %eax
    jne     .Lfailed
    cmpl    $0, (%esp)
    jne     .Lfailed
    movl    $7, %eax
    movl    $2, %ebx
    movl    %esp, %ecx
    xorl    %edx, %edx
    xorl    %esi, %esi
    int     $0x80
    jc      .Lfailed
    cmpl    $2, %eax
    jne     .Lfailed
    cmpl    $(I386_BOOTSTRAP_CHILD_EXIT_STATUS << 8), (%esp)
    jne     .Lfailed
    cmpl    $0, bootstrap_vfs_fd_active
    je      .Lvfs_fd_parent_done
    movl    $19, %eax
    xorl    %ebx, %ebx
    xorl    %ecx, %ecx
    movl    $1, %edx
    xorl    %esi, %esi
    int     $0x80
    jc      .Lfailed
    cmpl    $8, %eax
    jne     .Lfailed
    movl    $6, %eax
    xorl    %ebx, %ebx
    int     $0x80
    jc      .Lfailed
    movl    $6, %eax
    xorl    %ebx, %ebx
    int     $0x80
    jnc     .Lfailed
    cmpl    $I386_BOOTSTRAP_EBADF, %eax
    jne     .Lfailed
    movl    $5, %eax
    movl    $bootstrap_init_path, %ebx
    movl    $1, %ecx
    xorl    %edx, %edx
    int     $0x80
    jnc     .Lfailed
    cmpl    $I386_BOOTSTRAP_EROFS, %eax
    jne     .Lfailed
    xorl    %eax, %eax
    movl    $I386_BOOTSTRAP_VFS_FD_MAGIC, %ebx
    int     $0x30
.Lvfs_fd_parent_done:
    movl    $0, (%esp)
    movl    $2, %eax
    int     $0x80
    jc      .Lfailed
    testl   %eax, %eax
    jz      .Lchild2
    cmpl    $3, %eax
    jne     .Lfailed
    movl    $I386_BOOTSTRAP_FORK2_PARENT_MAGIC, %ebx
    int     $0x30
    movl    $7, %eax
    movl    $3, %ebx
    movl    $I386_BOOTSTRAP_USER_END, %ecx
    xorl    %edx, %edx
    xorl    %esi, %esi
    int     $0x80
    jnc     .Lfailed
    movl    $7, %eax
    movl    $3, %ebx
    movl    %esp, %ecx
    xorl    %edx, %edx
    xorl    %esi, %esi
    int     $0x80
    jc      .Lfailed
    cmpl    $3, %eax
    jne     .Lfailed
    cmpl    $(I386_BOOTSTRAP_CHILD2_EXIT_STATUS << 8), (%esp)
    jne     .Lfailed
    addl    $4, %esp
    movl    $I386_BOOTSTRAP_USER_RETURN_MAGIC, %ebx
    int     $0x30
    ud2
.Lchild1:
    cmpl    $0, bootstrap_vfs_fd_active
    je      .Lchild1_exit
    movl    $3, %eax
    xorl    %ebx, %ebx
    movl    $bootstrap_read_data, %ecx
    movl    $4, %edx
    int     $0x80
    jc      .Lfailed
    cmpl    $4, %eax
    jne     .Lfailed
    cmpl    $0x00010101, bootstrap_read_data
    jne     .Lfailed
.Lchild1_exit:
    movl    $1, %eax
    movl    $I386_BOOTSTRAP_CHILD_EXIT_STATUS, %ebx
    int     $0x80
    ud2
.Lchild2:
    movl    $1, %eax
    movl    $I386_BOOTSTRAP_CHILD2_EXIT_STATUS, %ebx
    int     $0x80
    ud2
.Lfailed:
    xorl    %ebx, %ebx
    int     $0x30
    ud2
    .size _start, . - _start

    .section .data, "aw", @progbits
    .p2align 2
bootstrap_data:
    .long I386_BOOTSTRAP_USER_DATA_MAGIC
bootstrap_init_path:
    .asciz "/sbin/init"

    .section .bss, "aw", @nobits
    .p2align 2
bootstrap_bss:
    .space 16
bootstrap_read_data:
    .space 4
bootstrap_vfs_fd_active:
    .space 4

    .section .note.GNU-stack, "", @progbits
