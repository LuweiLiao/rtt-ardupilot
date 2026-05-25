/*
 * RT-Thread startup override for STM32F767.
 *
 * The CMSIS package startup (startup_stm32f767xx.s) defines Reset_Handler
 * as weak.  This file provides a strong override that:
 *   - Skips __libc_init_array (C++ global ctors run later via
 *     INIT_COMPONENT_EXPORT in rt_board_init.c)
 *   - Calls entry() instead of main() so the full RT-Thread startup chain
 *     (rtthread_startup -> rt_hw_board_init -> heap init -> scheduler ->
 *     main_thread -> rt_components_init -> __libc_init_array -> main)
 *     executes in the correct order.
 *
 * The vector table and Default_Handler stay in the CMSIS file; the
 * .word Reset_Handler there resolves to this strong definition.
 */

    .syntax unified
    .cpu cortex-m7
    .fpu fpv5-d16
    .thumb

    .global Reset_Handler
    .section .text.Reset_Handler
    .type Reset_Handler, %function
Reset_Handler:
    ldr   sp, =_estack

    /* Set VTOR to app vector table BEFORE anything else.
     * The boot_stub leaves VTOR pointing at 0x200000 (SRAM copy of
     * its own vector table).  Any fault before rt_hw_board_init()
     * would invoke the wrong handler and escalate to a double-fault.
     * ROM origin is 0x08008000 (see link.lds MEMORY block). */
    ldr   r0, =0x08008000
    ldr   r1, =0xE000ED08
    str   r0, [r1]
    dsb
    isb

    bl    SystemInit

    /* Copy .data from flash to SRAM */
    ldr   r0, =_sdata
    ldr   r1, =_edata
    ldr   r2, =_sidata
    movs  r3, #0
    b     .L_LoopCopyData
.L_CopyData:
    ldr   r4, [r2, r3]
    str   r4, [r0, r3]
    adds  r3, r3, #4
.L_LoopCopyData:
    adds  r4, r0, r3
    cmp   r4, r1
    bcc   .L_CopyData

    /* Zero-fill .bss */
    ldr   r2, =_sbss
    ldr   r4, =_ebss
    movs  r3, #0
    b     .L_LoopFillBss
.L_FillBss:
    str   r3, [r2]
    adds  r2, r2, #4
.L_LoopFillBss:
    cmp   r2, r4
    bcc   .L_FillBss

    /* __libc_init_array is deferred to rt_board_init.c via INIT_COMPONENT_EXPORT,
       so C++ ctors run AFTER RTT heap is initialized. */
    bl    entry
    bx    lr
    .size Reset_Handler, .-Reset_Handler
