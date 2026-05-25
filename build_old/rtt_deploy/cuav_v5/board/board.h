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
 * BSS/data starts in DTCM but may spill into SRAM1 when the firmware
 * is large.  The heap MUST start after _ebss so it never overlaps BSS.
 * We also enforce a minimum of SRAM1 start to keep heap DMA-accessible.
 */
#define STM32F7_SRAM1_START  0x20020000UL

extern int _end;  /* after .bss AND .sram1_bss in linker script */
#define _HEAP_AFTER_ALL  ((rt_ubase_t)&_end)
#define HEAP_BEGIN       ((void *)((_HEAP_AFTER_ALL > STM32F7_SRAM1_START) \
                                    ? _HEAP_AFTER_ALL : STM32F7_SRAM1_START))
#define HEAP_END         STM32_SRAM_END

void SystemClock_Config(void);

/*
 * SPI1 DMA stream assignments per RM0410 Table 40:
 *   SPI1_RX → DMA2_Stream2 / Channel 3
 *   SPI1_TX → DMA2_Stream3 / Channel 3
 * SPI4 uses its own streams (Stream0-RX, Stream1-TX) so there is
 * no conflict with SPI1.
 */
#define SPI1_DMA_RX_IRQHandler    DMA2_Stream2_IRQHandler
#define SPI1_RX_DMA_RCC           RCC_AHB1ENR_DMA2EN
#define SPI1_RX_DMA_INSTANCE      DMA2_Stream2
#define SPI1_RX_DMA_CHANNEL       DMA_CHANNEL_3
#define SPI1_RX_DMA_IRQ           DMA2_Stream2_IRQn

#define SPI1_DMA_TX_IRQHandler    DMA2_Stream3_IRQHandler
#define SPI1_TX_DMA_RCC           RCC_AHB1ENR_DMA2EN
#define SPI1_TX_DMA_INSTANCE      DMA2_Stream3
#define SPI1_TX_DMA_CHANNEL       DMA_CHANNEL_3
#define SPI1_TX_DMA_IRQ           DMA2_Stream3_IRQn

/*
 * The generated CUAV V5 RT-Thread BSP currently compiles drivers against this
 * board header, but the per-board rtconfig DMA enable macros are not always
 * visible from every translation unit. Define the SPI DMA enable flags here so
 * SPI1 reliably takes the DMA/LLD path instead of falling back to the blocking
 * HAL polling transfer path.
 */
#ifndef BSP_SPI_USING_DMA
#define BSP_SPI_USING_DMA
#endif
#ifndef BSP_SPI1_RX_USING_DMA
#define BSP_SPI1_RX_USING_DMA
#endif
#ifndef BSP_SPI1_TX_USING_DMA
#define BSP_SPI1_TX_USING_DMA
#endif

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

#ifndef BSP_SPI4_RX_USING_DMA
#define BSP_SPI4_RX_USING_DMA
#endif
#ifndef BSP_SPI4_TX_USING_DMA
#define BSP_SPI4_TX_USING_DMA
#endif

#endif
