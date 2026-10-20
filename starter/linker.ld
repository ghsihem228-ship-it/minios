ENTRY(_start)
PHDRS
{
    text PT_LOAD FLAGS(5);
    data PT_LOAD FLAGS(6);
}
SECTIONS
{
    . = 0x100000;
    __load_start = .;
    .multiboot : { KEEP(*(.multiboot)) } :text
    .text : { *(.text*) } :text
    .rodata : { *(.rodata*) } :text
    /* The final byte makes the flat file length exactly load_end-load_start. */
    .data : { *(.data*) BYTE(0) } :data
    __load_end = .;
    .bss ALIGN(4096) (NOLOAD) : {
        __bss_start = .;
        *(.bss*) *(COMMON)
        . = ALIGN(4096);
        __bss_end = .;
    } :data
    /DISCARD/ : { *(.eh_frame*) *(.note*) *(.comment*) }
}
ASSERT(ADDR(.multiboot) - __load_start + SIZEOF(.multiboot) <= 8192,
       "Multiboot header must fit in the first 8192 file bytes")
ASSERT(__bss_end < 0x200000, "Bootstrap image and stack must fit below 2 MiB")
