OUTPUT_ARCH(riscv)
ENTRY(_start)
SECTIONS
{
    . = 0xffffffc000000000;
    . += 0x00200000;
    _start = .;
    .text : { *(.text.boot) *(.text) }
    .rodata : { *(.srodata*) *(.rodata*) }
    .data : { *(.sdata*) *(.data*) }
    . = ALIGN(4096);
    .boot_pgtable (NOLOAD) : { *(.boot_pgtable) }
    .bss : {
        __bss_start = .;
        *(.sbss*) *(.bss*)
        __bss_stop = .;
    }
    . = ALIGN(16);
    . = . + 0x4000;
    _end = .;
}

