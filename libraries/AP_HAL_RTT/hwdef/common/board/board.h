/*
 * Copyright (c) 2006-2021, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2018-11-5      SummerGift   first version
 * 2019-1-10      e31207077    add stm32f767-st-nucleo bsp
 */

#ifndef __BOARD_H__
#define __BOARD_H__

#include <rtthread.h>
#include <stm32f7xx.h>
#include "drv_common.h"
#include "drv_gpio.h"

#define STM32_FLASH_START_ADRESS     ((uint32_t)0x08000000)
#define STM32_FLASH_SIZE             (2048 * 1024)
#define STM32_FLASH_END_ADDRESS      ((uint32_t)(STM32_FLASH_START_ADRESS + STM32_FLASH_SIZE))

#define STM32_SRAM_SIZE           (512)
#define STM32_SRAM_END            (0x20000000 + STM32_SRAM_SIZE * 1024)

/*
 * STM32F767 memory map:
 *   0x20000000 – 0x2001FFFF  DTCM  128 KB  (CPU-only, DMA cannot access)
 *   0x20020000 – 0x2002FFFF  SRAM1  64 KB  (DMA non-cacheable window)
 *   0x20030000 – 0x2007FFFF  SRAM1 320 KB  (app data/bss/heap)
 *
 * link.lds keeps the runtime stack and selected CPU-only statics in DTCM,
 * places .sram1_bss in the non-cacheable DMA window, and places .data/.bss
 * plus heap in SRAM1_APP.
 */
#define STM32F7_SRAM1_START  0x20020000UL
#define STM32F7_SRAM1_DMA_START  0x20020000UL
#define STM32F7_SRAM1_APP_START  0x20030000UL
#define STM32F7_SRAM1_END    0x20080000UL

/* Place DMA-accessible BSS in the MPU non-cacheable SRAM1 window. */
#define RTT_SECTION_SRAM1_BSS __attribute__((section(".sram1_bss")))

extern int _end;  /* after .data/.bss in SRAM1_APP */
#define HEAP_BEGIN       ((void *)&_end)
#define HEAP_END         ((void *)STM32F7_SRAM1_END)

void rtt_clock_init(void);
void rtt_enable_peripheral_clocks(void);
void SystemClock_Config(void);

/*
 * Force SPI1 to alternate DMA2 streams so SPI4 can use its
 * default assignments (Stream0-RX, Stream1-TX).  Without this
 * override both SPI1 and SPI4 try to use DMA2_Stream0 for RX.
 */
#define SPI1_DMA_RX_IRQHandler    DMA2_Stream2_IRQHandler
#define SPI1_RX_DMA_RCC           RCC_AHB1ENR_DMA2EN
#define SPI1_RX_DMA_INSTANCE      DMA2_Stream2
#define SPI1_RX_DMA_CHANNEL       DMA_CHANNEL_3
#define SPI1_RX_DMA_IRQ           DMA2_Stream2_IRQn

#define SPI1_DMA_TX_IRQHandler    DMA2_Stream5_IRQHandler
#define SPI1_TX_DMA_RCC           RCC_AHB1ENR_DMA2EN
#define SPI1_TX_DMA_INSTANCE      DMA2_Stream5
#define SPI1_TX_DMA_CHANNEL       DMA_CHANNEL_3
#define SPI1_TX_DMA_IRQ           DMA2_Stream5_IRQn

/*
 * SPI4 uses its default DMA2 streams freed by the SPI1 remap above.
 * Pre-define here (like SPI1) so dma_config.h #elif chains are bypassed
 * consistently and all symbols are available in both drv_spi.c and LLD.
 */
#define SPI4_DMA_RX_IRQHandler    DMA2_Stream0_IRQHandler
#define SPI4_RX_DMA_RCC           RCC_AHB1ENR_DMA2EN
#define SPI4_RX_DMA_INSTANCE      DMA2_Stream0
#define SPI4_RX_DMA_CHANNEL       DMA_CHANNEL_4
#define SPI4_RX_DMA_IRQ           DMA2_Stream0_IRQn

#define SPI4_DMA_TX_IRQHandler    DMA2_Stream1_IRQHandler
#define SPI4_TX_DMA_RCC           RCC_AHB1ENR_DMA2EN
#define SPI4_TX_DMA_INSTANCE      DMA2_Stream1
#define SPI4_TX_DMA_CHANNEL       DMA_CHANNEL_4
#define SPI4_TX_DMA_IRQ           DMA2_Stream1_IRQn

#endif
