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
