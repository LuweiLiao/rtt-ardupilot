/*
 * linker script for STM32F4xx with GNU ld
 * bernard.xiong 2009-10-14
 */

/* Program Entry, set to mark it as "used" and avoid gc */
MEMORY
{
    ROM (rx) : ORIGIN = 0x08008000, LENGTH = 2016k /* 2MB — TEST mode (app at 0x08008000, bootloader reserved) */
    RAM_STACK (rw) : ORIGIN = 0x20000000, LENGTH = 128K /* STM32F7 DTCM: CPU-only stack */
    RAM_DMA (rw) : ORIGIN = 0x20020000, LENGTH = 64K /* SRAM1 non-cacheable DMA window */
    RAM_APP (rw) : ORIGIN = 0x20030000, LENGTH = 320K /* SRAM1 cacheable app data/bss/heap */
}
ENTRY(Reset_Handler)
_system_stack_size = 0x4000;

SECTIONS
{
    .text :
    {
        . = ALIGN(4);
        _stext = .;
        KEEP(*(.isr_vector))            /* Startup code */
        KEEP(*(.apsec_data))
        KEEP(*(.app_descriptor))

        . = ALIGN(4);
        *(.text)                        /* remaining code */
        *(.text.*)                      /* remaining code */
        *(.rodata)                      /* read-only data (constants) */
        *(.rodata*)
        *(.glue_7)
        *(.glue_7t)
        *(.gnu.linkonce.t*)

        /* section information for finsh shell */
        . = ALIGN(4);
        __fsymtab_start = .;
        KEEP(*(FSymTab))
        __fsymtab_end = .;

        . = ALIGN(4);
        __vsymtab_start = .;
        KEEP(*(VSymTab))
        __vsymtab_end = .;

        /* section information for initial. */
        . = ALIGN(4);
        __rt_init_start = .;
        KEEP(*(SORT(.rti_fn*)))
        __rt_init_end = .;

        . = ALIGN(4);

        /* C++ global constructors (newlib __libc_init_array) */
        . = ALIGN(4);
        __preinit_array_start = .;
        KEEP (*(.preinit_array*))
        __preinit_array_end = .;

        . = ALIGN(4);
        __init_array_start = .;
        KEEP (*(SORT(.init_array.*)))
        KEEP (*(.init_array*))
        __init_array_end = .;

        PROVIDE(__ctors_start__ = __init_array_start);
        PROVIDE(__ctors_end__ = __init_array_end);

        . = ALIGN(4);
        __fini_array_start = .;
        KEEP (*(SORT(.fini_array.*)))
        KEEP (*(.fini_array*))
        __fini_array_end = .;

        . = ALIGN(4);

        _etext = .;
    } > ROM = 0

    /* .ARM.exidx is sorted, so has to go in its own output section.  */
    __exidx_start = .;
    .ARM.exidx :
    {
        *(.ARM.exidx* .gnu.linkonce.armexidx.*)

        /* This is used by the startup in order to initialize the .data secion */
        _sidata = .;
    } > ROM
    __exidx_end = .;

    /* .data section which is used for initialized data */

    .data : AT (_sidata)
    {
        . = ALIGN(4);
        /* This is used by the startup in order to initialize the .data secion */
        _sdata = . ;

        *(.data)
        *(.data.*)
        *(.gnu.linkonce.d*)

        PROVIDE(__dtors_start__ = .);
        KEEP(*(SORT(.dtors.*)))
        KEEP(*(.dtors))
        PROVIDE(__dtors_end__ = .);

        . = ALIGN(4);
        /* This is used by the startup in order to initialize the .data secion */
        _edata = . ;
    } > RAM_APP

    .stack (NOLOAD) :
    {
        . = ALIGN(4);
        _sstack = .;
        . = . + _system_stack_size;
        . = ALIGN(4);
        _estack = .;
    } > RAM_STACK

    /*
     * GDB-visible RTT diagnostics. The symbols still use the historical
     * ".dtcm_bss.rtt_dbg" input name, but runtime gates depend on reading them
     * after USB/PARAM stress via SWD. Keep this diagnostic-only data in the
     * non-cacheable RAM_DMA window so OpenOCD/GDB sees CPU writes without a
     * D-cache clean, and without relying on DTCM debug visibility.
     */
    .rtt_dbg_bss (NOLOAD) :
    {
        . = ALIGN(8);
        _srtt_dbg_bss = .;
        KEEP(*(.dtcm_bss.rtt_dbg))
        KEEP(*(.dtcm_bss.rtt_dbg.*))
        . = ALIGN(8);
        _ertt_dbg_bss = .;
    } > RAM_DMA

    /*
     * CPU-only static BSS.  These large objects are not used by DMA and do
     * not need to live in SRAM1_APP; keeping them in DTCM preserves SRAM1_APP
     * for RT-Thread dynamic thread stacks and ArduPilot runtime heap.
     */
    .dtcm_bss (NOLOAD) :
    {
        . = ALIGN(8);
        _sdtcm_bss = .;
        *(.dtcm_bss)
        *(.dtcm_bss.*)
        *(.bss._ZL13storageDriver)
        *(.bss._ZL16supported_frames)
        . = ALIGN(8);
        _edtcm_bss = .;
    } > RAM_STACK

    /* DMA-safe BSS: first 64KB of SRAM1 is made non-cacheable by the MPU. */
    .sram1_bss (NOLOAD) :
    {
        . = ALIGN(32);
        _sram1_bss_start = .;
        *(.sram1_bss)
        *(.sram1_bss.*)
        . = ALIGN(32);
        _sram1_bss_end = .;
    } > RAM_DMA

    .bss (NOLOAD) :
    {
        . = ALIGN(4);
        /* This is used by the startup in order to initialize the .bss secion */
        __bss_start = .;
        _sbss = .;

        *(.bss)
        *(.bss.*)
        *(COMMON)
        *(.bss.init)

        . = ALIGN(4);
        /* This is used by the startup in order to initialize the .bss secion */
        _ebss = . ;
        __bss_end = .;
    } > RAM_APP

    _end = .;

    ASSERT(SIZEOF(.stack) <= LENGTH(RAM_STACK), "RTT stack exceeds RAM_STACK")
    ASSERT((SIZEOF(.stack) + SIZEOF(.dtcm_bss)) <= LENGTH(RAM_STACK), "RTT DTCM stack+.dtcm_bss exceeds RAM_STACK")
    ASSERT(SIZEOF(.sram1_bss) <= LENGTH(RAM_DMA), "RTT .sram1_bss exceeds non-cacheable RAM_DMA window")
    ASSERT(_sram1_bss_end <= ORIGIN(RAM_DMA) + LENGTH(RAM_DMA), "RTT .rtt_dbg_bss+.sram1_bss exceeds non-cacheable RAM_DMA window")

    /* Stabs debugging sections.  */
    .stab          0 : { *(.stab) }
    .stabstr       0 : { *(.stabstr) }
    .stab.excl     0 : { *(.stab.excl) }
    .stab.exclstr  0 : { *(.stab.exclstr) }
    .stab.index    0 : { *(.stab.index) }
    .stab.indexstr 0 : { *(.stab.indexstr) }
    .comment       0 : { *(.comment) }
    /* DWARF debug sections.
     * Symbols in the DWARF debugging sections are relative to the beginning
     * of the section so we begin them at 0.  */
    /* DWARF 1 */
    .debug          0 : { *(.debug) }
    .line           0 : { *(.line) }
    /* GNU DWARF 1 extensions */
    .debug_srcinfo  0 : { *(.debug_srcinfo) }
    .debug_sfnames  0 : { *(.debug_sfnames) }
    /* DWARF 1.1 and DWARF 2 */
    .debug_aranges  0 : { *(.debug_aranges) }
    .debug_pubnames 0 : { *(.debug_pubnames) }
    /* DWARF 2 */
    .debug_info     0 : { *(.debug_info .gnu.linkonce.wi.*) }
    .debug_abbrev   0 : { *(.debug_abbrev) }
    .debug_line     0 : { *(.debug_line) }
    .debug_frame    0 : { *(.debug_frame) }
    .debug_str      0 : { *(.debug_str) }
    .debug_loc      0 : { *(.debug_loc) }
    .debug_macinfo  0 : { *(.debug_macinfo) }
    /* SGI/MIPS DWARF 2 extensions */
    .debug_weaknames 0 : { *(.debug_weaknames) }
    .debug_funcnames 0 : { *(.debug_funcnames) }
    .debug_typenames 0 : { *(.debug_typenames) }
    .debug_varnames  0 : { *(.debug_varnames) }
}
