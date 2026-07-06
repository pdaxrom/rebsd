        .section .rootfs_blob,"a",@progbits
        .balign 16
        .globl  __malta64_rootfs_blob_start
        .globl  __malta64_rootfs_blob_end
__malta64_rootfs_blob_start:
        .incbin "rootfs.img"
        .balign 16
__malta64_rootfs_blob_end:
