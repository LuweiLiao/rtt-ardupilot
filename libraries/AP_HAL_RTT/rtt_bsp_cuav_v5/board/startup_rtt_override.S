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
    .fpu softvfp
    .thumb

    .global Reset_Handler
    .section .text.Reset_Handler
    .type Reset_Handler, %function
Reset_Handler:
    ldr   sp, =_estack

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

    /* NO __libc_init_array here — ctors run via INIT_COMPONENT_EXPORT */

    bl    entry
    bx    lr
    .size Reset_Handler, .-Reset_Handler
