/* 3GX の三つのロード領域と CTRPF の起動・TLS に必要な記号。 */
OUTPUT_FORMAT("elf32-littlearm", "elf32-bigarm", "elf32-littlearm")
OUTPUT_ARCH(arm)
ENTRY(_start)

PHDRS
{
    code PT_LOAD FLAGS(5);
    rodata PT_LOAD FLAGS(4);
    data PT_LOAD FLAGS(6);
}

SECTIONS
{
    PROVIDE(__start__ = 0x07000100);
    . = __start__;
    .text ALIGN(4) :
    {
        KEEP(*(.crt0))
        KEEP(*(.init))
        *(.text .text.* .gnu.linkonce.t.*)
        *(.glue_7 .glue_7t .stub .gnu.warning)
        KEEP(*(.fini))
        . = ALIGN(4);
    } : code

    .rodata ALIGN(4) :
    {
        *(.rodata .rodata.* .roda .gnu.linkonce.r.*)
        *all.rodata*(*)
        SORT(CONSTRUCTORS)
        . = ALIGN(4);
        __tdata_align = .;
        LONG(ALIGNOF(.tdata));
    } : rodata
    .ARM.extab ALIGN(4) : { *(.ARM.extab* .gnu.linkonce.armextab.*) } : rodata
    __exidx_start = .;
    /* 3gxtool に渡す program header は三つに限る。専用 PHDR の自動追加を避ける。 */
    ARM.exidx ALIGN(4) : { *(.ARM.exidx* .gnu.linkonce.armexidx.*) } : rodata
    __exidx_end = .;

    .data ALIGN(4) :
    {
        *(.data .data.* .gnu.linkonce.d.*)
        CONSTRUCTORS
        . = ALIGN(4);
    } : data
    .tdata ALIGN(4) :
    {
        __tdata_lma = .;
        *(.tdata .tdata.* .gnu.linkonce.td.*)
        . = ALIGN(4);
        __tdata_lma_end = .;
    } : data
    .tbss ALIGN(4) :
    {
        *(.tbss .tbss.* .gnu.linkonce.tb.* .tcommon)
        . = ALIGN(4);
    } : data

    .preinit_array ALIGN(4) :
    {
        PROVIDE(__preinit_array_start = .);
        KEEP(*(.preinit_array))
        PROVIDE(__preinit_array_end = .);
    } : data
    .init_array ALIGN(4) :
    {
        PROVIDE(__init_array_start = .);
        KEEP(*(SORT(.init_array.*)))
        KEEP(*(.init_array))
        PROVIDE(__init_array_end = .);
    } : data
    .fini_array ALIGN(4) :
    {
        PROVIDE(__fini_array_start = .);
        KEEP(*(.fini_array))
        KEEP(*(SORT(.fini_array.*)))
        PROVIDE(__fini_array_end = .);
    } : data
    .ctors ALIGN(4) :
    {
        KEEP(*crtbegin.o(.ctors))
        KEEP(*(EXCLUDE_FILE(*crtend.o) .ctors))
        KEEP(*(SORT(.ctors.*)))
        KEEP(*(.ctors))
    } : data
    .dtors ALIGN(4) :
    {
        KEEP(*crtbegin.o(.dtors))
        KEEP(*(EXCLUDE_FILE(*crtend.o) .dtors))
        KEEP(*(SORT(.dtors.*)))
        KEEP(*(.dtors))
    } : data

    .bss ALIGN(4) :
    {
        __bss_start__ = .;
        *(.dynbss .bss .bss.* .gnu.linkonce.b.*)
        *(COMMON)
        /* ARM TLS ヘッダーの8バイトを考慮して主スレッド分を確保する。 */
        . = 8 + ABSOLUTE(ALIGN(ABSOLUTE(. - 8), MAX(4, ALIGNOF(.tdata))));
        __tls_start = .;
        . += SIZEOF(.tdata) + SIZEOF(.tbss);
        __tls_end = .;
        . = ALIGN(4);
        __bss_end__ = .;
    } : data
    __end__ = ABSOLUTE(.);
    /DISCARD/ : { *(.group .comment .note .note.*) }
}
