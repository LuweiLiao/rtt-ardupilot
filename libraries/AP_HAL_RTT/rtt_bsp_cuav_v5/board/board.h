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
 *   0x20020000 – 0x2007FFFF  SRAM1 384 KB  (DMA-accessible)
 *
 * BSS/data lives in DTCM.  The RT-Thread heap must start in SRAM1 so
 * that rt_malloc'd buffers (DMA transfers, thread stacks) are always
 * DMA-accessible.  ~18 KB at the tail of DTCM is unused but avoids
 * silent DMA failures and HardFaults caused by DMA-to-DTCM writes.
 */
#define STM32F7_SRAM1_START  ((void *)0x20020000UL)

#if defined(__ARMCC_VERSION)
extern int Image$$RW_IRAM1$$ZI$$Limit;
#define HEAP_BEGIN      STM32F7_SRAM1_START
#elif __ICCARM__
#pragma section="CSTACK"
#define HEAP_BEGIN      STM32F7_SRAM1_START
#else
extern int __bss_end;
#define HEAP_BEGIN      STM32F7_SRAM1_START
#endif

#define HEAP_END        STM32_SRAM_END

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

#endif
