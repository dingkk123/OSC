OUTPUT_ARCH(riscv)
ENTRY(_start)
SECTIONS
{
    . = 0x20000000;
    _start = .;
    .text : { *(.text.boot) *(.text) }
    .rodata : { *(.rodata) }
    .data : { *(.data) }
    .bss : {
        __bss_start = .;
        *(.sbss) *(.bss)
        __bss_stop = .;
    }
    . = ALIGN(16);
    . = . + 0x4000;
    _end = .;
}
