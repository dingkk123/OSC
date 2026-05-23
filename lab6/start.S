    .section ".text.boot"
    .global _start
_start:
    
    la   t0, __bss_start
    la   t1, __bss_stop
    li   t2, 0
1:
    bgeu t0, t1, 2f
    sd   t2, 0(t0)
    addi t0, t0, 8
    j    1b
2:
   
    la   sp, _end

    mv   a0, a1
    tail start_kernel

    .align 3
    .global copy_kernel_and_boot_start
    .global copy_kernel_and_boot_end
copy_kernel_and_boot_start:
    /* a0: source, a1: destination/entry, a2: size, a3: fdt */
    mv   t0, a0
    mv   t1, a1
    mv   t2, a2
1:
    beqz t2, 2f
    lbu  t3, 0(t0)
    sb   t3, 0(t1)
    addi t0, t0, 1
    addi t1, t1, 1
    addi t2, t2, -1
    j    1b
2:
    .word 0x0000100f
    mv   a0, a3
    jr   a1
copy_kernel_and_boot_end:

