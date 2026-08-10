/*
 * Re-enter the production libdragon IPL3 from the cartridge ROM.
 *
 * N64cart's menu uses the same chain-load contract: copy the 0xfc0-byte
 * IPL3 body from ROM to SP DMEM, restore the IPL2 register interface, and
 * jump to 0xa4000040.  The IPL3 warm path then clears RDRAM and both CPU
 * caches before loading the ROM ELF again.
 */
        .set    noreorder
        .set    noat
        .set    mips3

        .section .text, "ax", @progbits
        .globl  n64_rom_restart
        .ent    n64_rom_restart
n64_rom_restart:
        move    $t9, $a0               /* retained IPL2 TV type */
        lui     $t0, 0xb000
        addiu   $t0, $t0, 0x0040       /* ROM IPL3 source */
        lui     $t1, 0xb000
        addiu   $t1, $t1, 0x1000       /* source end */
        lui     $t3, 0xa400
        addiu   $t3, $t3, 0x0040       /* SP DMEM destination */
1:
        lw      $t2, 0($t0)
        sw      $t2, 0($t3)
        addiu   $t0, $t0, 4
        addiu   $t3, $t3, 4
        bne     $t0, $t1, 1b
        nop
        sync

        /* Register state consumed by libdragon's production IPL3. */
        move    $s3, $zero             /* cartridge ROM */
        move    $s4, $t9               /* retained TV type */
        addiu   $s5, $zero, 1          /* warm reset */
        move    $s6, $zero             /* unused by this IPL3 */
        move    $s7, $zero             /* unused by this IPL3 */
        move    $t1, $zero
        addiu   $t2, $zero, 0x0040
        lui     $t3, 0xa400
        addiu   $t3, $t3, 0x0040
        lui     $sp, 0xa400
        addiu   $sp, $sp, 0x1ff0
        lui     $ra, 0xa400
        addiu   $ra, $ra, 0x1550
        jr      $t3
        nop
        .end    n64_rom_restart
