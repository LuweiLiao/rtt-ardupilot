/*
 * This file is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This file is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * SD card support for AP_HAL_RTT — direct SDIO register operations.
 *
 * Implements the ChibiOS-compatible API (sdcard_init / sdcard_stop / sdcard_retry)
 * using direct CMSIS SDIO register access instead of RTT's DFS/mmcsd framework.
 *
 * ChibiOS Reference: modules/ChibiOS/os/hal/ports/STM32/LLD/SDIOv1/hal_sdc_lld.c
 *   Key functions:
 *     sdc_lld_start()          — L476-464  SDIO clock/POWER/ICR init
 *     sdc_lld_start_clk()      — L499-508  Set 400kHz init clock
 *     sdc_lld_set_data_clk()   — L518-540  Set 25/50 MHz data clock
 *     sdc_lld_set_bus_mode()   — L563-577  1/4/8-bit bus width
 *     sdc_lld_send_cmd_short() — L612-629  CMD+ARG+wait RESP/CTIMEOUT
 *     sdc_lld_send_cmd_long()  — L677-701  Long response (CID/CSD)
 *
 * Architecture decision: This file manages the SDIO peripheral and card
 * detection at the register level, then relies on the board-init RTT DFS
 * mount (sd_card_mount_sync in rt_board_init.c) for filesystem access via
 * the POSIX backend (AP_FILESYSTEM_POSIX_ENABLED=1).
 *
 * ADR-012: Direct SDIO register access vs RTT drv_sdio.c block device.
 *   RTT's drv_sdio.c registers an "sd0" block device via the MMCSD/SDIO
 *   framework.  This file replaces that dependency with direct register
 *   operations for card power/detect/init, while the filesystem layer
 *   still goes through RTT DFS (board init mounts at "/").
 *
 * ChibiOS: SDC driver uses SDMMC IDMA.  RTT uses system DMA2 for the
 *   block data path (DMA2_Stream6/Ch4 TX, DMA2_Stream3/Ch4 RX).  Clock
 *   source: 48 MHz PLL48CLK for both.
 *
 * Register map reference (STM32F7 RM0430 §35):
 *   SDIO_BASE = 0x40012C00
 *   +0x00 POWER   — power supply control
 *   +0x04 CLKCR   — clock control (CLKDIV[7:0], BYPASS, WIDBUS[1:0], CLKEN)
 *   +0x08 ARG     — command argument
 *   +0x0C CMD     — command (CMDINDEX[5:0], WAITRESP[1:0], CPSMEN)
 *   +0x10 RESPCMD — response command index
 *   +0x14 RESP1   — response 1 .. +0x20 RESP4
 *   +0x24 DTIMER  — data timeout
 *   +0x28 DLEN    — data length
 *   +0x2C DCTRL   — data control (DTEN, DTDIR, DTMODE, DMAEN, DBLOCKSIZE[3:0])
 *   +0x30 DCOUNT  — data count
 *   +0x34 STA     — status register
 *   +0x38 ICR     — interrupt clear register
 *   +0x3C MASK    — mask register
 *   +0x48 FIFOCNT — FIFO counter
 *   +0x80 FIFO    — data FIFO (32-bit access)
 */

#include <sys/stat.h>

#include <board.h>             /* stm32f7xx.h (→ stm32f765xx.h), GPIO defs */
#include "sdcard.h"

/*
 * CMSIS names the SDIO peripheral "SDMMC1" (SDMMC_TypeDef).
 * ChibiOS uses the alias "SDIO" (from old STM32F1 naming).
 * Provide the alias so this file stays source-compatible with the
 * ChibiOS reference code wherever possible.
 *
 * ChibiOS Reference: hal_sdc_lld.c:413  SDCD1.sdio = SDIO;
 * CMSIS Reference:   stm32f765xx.h:1407 #define SDMMC1 ((SDMMC_TypeDef *) SDMMC1_BASE)
 */
#define SDIO    SDMMC1

#include <AP_BoardConfig/AP_BoardConfig.h>

extern const AP_HAL::HAL& hal;

/* ---------------------------------------------------------------------------
 * SDIO register constants (subset of CMSIS defines, verified against RM0430)
 * -------------------------------------------------------------------------*/

/* SDIO_POWER register */
#define SDIO_POWER_PWRCTRL_Pos            0U
#define SDIO_POWER_PWRCTRL_Msk            (0x3UL << SDIO_POWER_PWRCTRL_Pos)
#define SDIO_POWER_PWRCTRL_0              (0x1UL << SDIO_POWER_PWRCTRL_Pos)
#define SDIO_POWER_PWRCTRL_1              (0x2UL << SDIO_POWER_PWRCTRL_Pos)

/* SDIO_CLKCR register */
#define SDIO_CLKCR_CLKDIV_Pos             0U
#define SDIO_CLKCR_CLKDIV_Msk             (0xFFUL << SDIO_CLKCR_CLKDIV_Pos)
#define SDIO_CLKCR_CLKEN_Pos              8U
#define SDIO_CLKCR_CLKEN                  (0x1UL << SDIO_CLKCR_CLKEN_Pos)
#define SDIO_CLKCR_PWRSAV_Pos             9U
#define SDIO_CLKCR_PWRSAV                 (0x1UL << SDIO_CLKCR_PWRSAV_Pos)
#define SDIO_CLKCR_BYPASS_Pos             10U
#define SDIO_CLKCR_BYPASS                 (0x1UL << SDIO_CLKCR_BYPASS_Pos)
#define SDIO_CLKCR_WIDBUS_Pos             11U
#define SDIO_CLKCR_WIDBUS                 (0x3UL << SDIO_CLKCR_WIDBUS_Pos)
#define SDIO_CLKCR_WIDBUS_0               (0x1UL << SDIO_CLKCR_WIDBUS_Pos)
#define SDIO_CLKCR_WIDBUS_1               (0x2UL << SDIO_CLKCR_WIDBUS_Pos)
#define SDIO_CLKCR_NEGEDGE_Pos            13U
#define SDIO_CLKCR_NEGEDGE                (0x1UL << SDIO_CLKCR_NEGEDGE_Pos)
#define SDIO_CLKCR_HWFC_EN_Pos            14U
#define SDIO_CLKCR_HWFC_EN                (0x1UL << SDIO_CLKCR_HWFC_EN_Pos)

/* SDIO_CMD register */
#define SDIO_CMD_CMDINDEX_Pos             0U
#define SDIO_CMD_CMDINDEX_Msk             (0x3FUL << SDIO_CMD_CMDINDEX_Pos)
#define SDIO_CMD_WAITRESP_Pos             6U
#define SDIO_CMD_WAITRESP                 (0x3UL << SDIO_CMD_WAITRESP_Pos)
#define SDIO_CMD_WAITRESP_0               (0x1UL << SDIO_CMD_WAITRESP_Pos)
#define SDIO_CMD_WAITRESP_1               (0x2UL << SDIO_CMD_WAITRESP_Pos)
#define SDIO_CMD_CPSMEN_Pos               8U
#define SDIO_CMD_CPSMEN                   (0x1UL << SDIO_CMD_CPSMEN_Pos)
#define SDIO_CMD_SDIOSUSPEND_Pos          9U
#define SDIO_CMD_SDIOSUSPEND              (0x1UL << SDIO_CMD_SDIOSUSPEND_Pos)
#define SDIO_CMD_ENCMDCOMPL_Pos           10U
#define SDIO_CMD_ENCMDCOMPL               (0x1UL << SDIO_CMD_ENCMDCOMPL_Pos)
#define SDIO_CMD_NIEN_Pos                 11U
#define SDIO_CMD_NIEN                     (0x1UL << SDIO_CMD_NIEN_Pos)
#define SDIO_CMD_CEATACMD_Pos             12U
#define SDIO_CMD_CEATACMD                 (0x1UL << SDIO_CMD_CEATACMD_Pos)

/* SDIO_DCTRL register */
#define SDIO_DCTRL_SDIOEN_Pos             11U
#define SDIO_DCTRL_SDIOEN                 (0x1UL << SDIO_DCTRL_SDIOEN_Pos)

/* SDIO_STA status flags */
#define SDIO_STA_CCRCFAIL_Pos             0U
#define SDIO_STA_CCRCFAIL                 (0x1UL << SDIO_STA_CCRCFAIL_Pos)
#define SDIO_STA_DCRCFAIL_Pos             1U
#define SDIO_STA_DCRCFAIL                 (0x1UL << SDIO_STA_DCRCFAIL_Pos)
#define SDIO_STA_CTIMEOUT_Pos             2U
#define SDIO_STA_CTIMEOUT                 (0x1UL << SDIO_STA_CTIMEOUT_Pos)
#define SDIO_STA_DTIMEOUT_Pos             3U
#define SDIO_STA_DTIMEOUT                 (0x1UL << SDIO_STA_DTIMEOUT_Pos)
#define SDIO_STA_TXUNDERR_Pos             4U
#define SDIO_STA_TXUNDERR                 (0x1UL << SDIO_STA_TXUNDERR_Pos)
#define SDIO_STA_RXOVERR_Pos              5U
#define SDIO_STA_RXOVERR                  (0x1UL << SDIO_STA_RXOVERR_Pos)
#define SDIO_STA_CMDREND_Pos              6U
#define SDIO_STA_CMDREND                  (0x1UL << SDIO_STA_CMDREND_Pos)
#define SDIO_STA_CMDSENT_Pos              7U
#define SDIO_STA_CMDSENT                  (0x1UL << SDIO_STA_CMDSENT_Pos)
#define SDIO_STA_DATAEND_Pos              8U
#define SDIO_STA_DATAEND                  (0x1UL << SDIO_STA_DATAEND_Pos)
#define SDIO_STA_STBITERR_Pos             9U
#define SDIO_STA_STBITERR                 (0x1UL << SDIO_STA_STBITERR_Pos)
#define SDIO_STA_DBCKEND_Pos              10U
#define SDIO_STA_DBCKEND                  (0x1UL << SDIO_STA_DBCKEND_Pos)
#define SDIO_STA_CMDACT_Pos               11U
#define SDIO_STA_CMDACT                   (0x1UL << SDIO_STA_CMDACT_Pos)
#define SDIO_STA_TXACT_Pos                12U
#define SDIO_STA_TXACT                    (0x1UL << SDIO_STA_TXACT_Pos)
#define SDIO_STA_RXACT_Pos                13U
#define SDIO_STA_RXACT                    (0x1UL << SDIO_STA_RXACT_Pos)
#define SDIO_STA_TXFIFOHE_Pos             14U
#define SDIO_STA_TXFIFOHE                 (0x1UL << SDIO_STA_TXFIFOHE_Pos)
#define SDIO_STA_RXFIFOHF_Pos             15U
#define SDIO_STA_RXFIFOHF                 (0x1UL << SDIO_STA_RXFIFOHF_Pos)
#define SDIO_STA_TXFIFOF_Pos              16U
#define SDIO_STA_TXFIFOF                  (0x1UL << SDIO_STA_TXFIFOF_Pos)
#define SDIO_STA_RXFIFOF_Pos              17U
#define SDIO_STA_RXFIFOF                  (0x1UL << SDIO_STA_RXFIFOF_Pos)
#define SDIO_STA_TXFIFOE_Pos              18U
#define SDIO_STA_TXFIFOE                  (0x1UL << SDIO_STA_TXFIFOE_Pos)
#define SDIO_STA_RXFIFOE_Pos              19U
#define SDIO_STA_RXFIFOE                  (0x1UL << SDIO_STA_RXFIFOE_Pos)
#define SDIO_STA_TXDAVL_Pos               20U
#define SDIO_STA_TXDAVL                   (0x1UL << SDIO_STA_TXDAVL_Pos)
#define SDIO_STA_RXDAVL_Pos               21U
#define SDIO_STA_RXDAVL                   (0x1UL << SDIO_STA_RXDAVL_Pos)

/* SDIO_ICR clear bits (same positions as STA) */
#define SDIO_ICR_ALL_FLAGS                0xFFFFFFFFUL
#define SDIO_ICR_CMDSENTC                 (1U << 5)     /* CMDSENT flag clear */

/* SDIO_MASK interrupt enable bits (same positions as STA) */
#define SDIO_MASK_CCRCFAILIE              SDIO_STA_CCRCFAIL
#define SDIO_MASK_DCRCFAILIE              SDIO_STA_DCRCFAIL
#define SDIO_MASK_CTIMEOUTIE              SDIO_STA_CTIMEOUT
#define SDIO_MASK_DTIMEOUTIE              SDIO_STA_DTIMEOUT
#define SDIO_MASK_TXUNDERRIE              SDIO_STA_TXUNDERR
#define SDIO_MASK_RXOVERRIE               SDIO_STA_RXOVERR
#define SDIO_MASK_DATAENDIE               SDIO_STA_DATAEND
#define SDIO_MASK_STBITERRIE              SDIO_STA_STBITERR

/* Combined error mask — ChibiOS hal_sdc_lld.c:59-62 */
#define SDIO_STA_ERROR_MASK                                             \
  (SDIO_STA_CCRCFAIL | SDIO_STA_DCRCFAIL |                             \
   SDIO_STA_CTIMEOUT | SDIO_STA_DTIMEOUT |                             \
   SDIO_STA_TXUNDERR | SDIO_STA_RXOVERR)

/* DBLOCKSIZE encoding: 2^(N) bytes per block.  512 bytes = 9 (0b1001).
 * Bits: DBLOCKSIZE_3 | DBLOCKSIZE_0 */
/* ---------------------------------------------------------------------------
 * SD card protocol constants (MMCSD)
 * -------------------------------------------------------------------------*/

/* SD commands (CMD index) */
#define MMCSD_CMD_GO_IDLE_STATE           0U
#define MMCSD_CMD_ALL_SEND_CID            2U
#define MMCSD_CMD_SET_RELATIVE_ADDR       3U
#define MMCSD_CMD_SET_DSR                 4U
#define MMCSD_CMD_SELECT_CARD             7U
#define MMCSD_CMD_SEND_IF_COND            8U
#define MMCSD_CMD_SEND_CSD                9U
#define MMCSD_CMD_SEND_CID                10U
#define MMCSD_CMD_VOLTAGE_SWITCH          11U
#define MMCSD_CMD_STOP_TRANSMISSION       12U
#define MMCSD_CMD_SEND_STATUS             13U
#define MMCSD_CMD_GO_INACTIVE_STATE       15U
#define MMCSD_CMD_SET_BLOCKLEN            16U
#define MMCSD_CMD_APP_CMD                 55U

/* Application-specific commands (CMD55 + ACMD) */
#define MMCSD_ACMD_SD_SEND_OP_COND        41U
#define MMCSD_ACMD_SET_BUS_WIDTH          6U

/* R1 response error bits */
#define MMCSD_R1_OUT_OF_RANGE             (1U << 31)
#define MMCSD_R1_ADDRESS_ERROR            (1U << 30)
#define MMCSD_R1_BLOCK_LEN_ERROR          (1U << 29)
#define MMCSD_R1_ERASE_SEQ_ERROR          (1U << 28)
#define MMCSD_R1_ERASE_PARAM              (1U << 27)
#define MMCSD_R1_WP_VIOLATION             (1U << 26)
#define MMCSD_R1_CARD_IS_LOCKED           (1U << 25)
#define MMCSD_R1_LOCK_UNLOCK_FAILED       (1U << 24)
#define MMCSD_R1_COM_CRC_ERROR            (1U << 23)
#define MMCSD_R1_ILLEGAL_COMMAND          (1U << 22)
#define MMCSD_R1_CARD_ECC_FAILED          (1U << 21)
#define MMCSD_R1_CC_ERROR                 (1U << 20)
/* MMCSD_R1_ERROR_BIT defined below as function-style mask — single bit unused */
#define MMCSD_R1_UNDERRUN                 (1U << 18)
#define MMCSD_R1_OVERRUN                  (1U << 17)
#define MMCSD_R1_CID_CSD_OVERWRITE        (1U << 16)
#define MMCSD_R1_WP_ERASE_SKIP            (1U << 15)
#define MMCSD_R1_CARD_ECC_DISABLED        (1U << 14)
#define MMCSD_R1_ERASE_RESET              (1U << 13)
#define MMCSD_R1_READY_FOR_DATA            (1U << 8)
#define MMCSD_R1_CURRENT_STATE_Pos         9U
#define MMCSD_R1_CURRENT_STATE_Msk        (0xFUL << MMCSD_R1_CURRENT_STATE_Pos)

#define MMCSD_R1_ERROR(x)                 ((x) & 0xFC008000UL)  /* error bits */
#define MMCSD_R1_READY(x)                 ((x) & MMCSD_R1_READY_FOR_DATA)

/* CMD8 check pattern */
#define SD_CMD8_PATTERN                   0xAAUL
#define SD_CMD8_MASK                      0xFFUL
#define SD_CMD8_VOLTAGE_27_36             (0x1UL << 8)

/* ACMD41 HCS (High Capacity Support) */
#define SD_ACMD41_HCS                     (1UL << 30)
/* OCR bits */
#define SD_OCR_CCS                        (1UL << 30)  /* Card Capacity Status */
#define SD_OCR_BUSY                       (1UL << 31)  /* Power-up busy */

/* Block size */
#define MMCSD_BLOCK_SIZE                  512U

/* SDIO clock source (PLL48CLK) */
#define SDIO_CLOCK_SRC_HZ                 48000000UL

/* DMA stream IDs for SDIO */
#define SDIO_DMA_RX_STREAM                3U    /* DMA2 Stream3 */
#define SDIO_DMA_TX_STREAM                6U    /* DMA2 Stream6 */
#define SDIO_DMA_CHANNEL                  4U    /* Channel 4 */

/* Power pin (PG7 = VDD_3V3_SD_CARD_EN) */
#ifndef HAL_GPIO_SD_CARD_PWR_PIN
#ifdef HAL_GPIO_VDD_3V3_SD_CARD_EN_PIN
#define HAL_GPIO_SD_CARD_PWR_PIN HAL_GPIO_VDD_3V3_SD_CARD_EN_PIN
#else
#define HAL_GPIO_SD_CARD_PWR_PIN GPIO_PIN_7
#endif
#endif

/* GPIOG register base (port 6 = GPIOG, 0x40021800) */
#ifndef GPIOG_BASE
#define GPIOG_BASE                        0x40021800UL
#endif
#define GPIOG_MODER                       (*(volatile uint32_t *)(GPIOG_BASE + 0x00))
#define GPIOG_OTYPER                      (*(volatile uint32_t *)(GPIOG_BASE + 0x04))
#define GPIOG_OSPEEDR                     (*(volatile uint32_t *)(GPIOG_BASE + 0x08))
#define GPIOG_PUPDR                       (*(volatile uint32_t *)(GPIOG_BASE + 0x0C))
#define GPIOG_ODR                         (*(volatile uint32_t *)(GPIOG_BASE + 0x14))

/* GPIOG register base (port 6 = GPIOG, 0x40021800) */
#define SDMMC_GPIO_AF                     12U

/* ---------------------------------------------------------------------------
 * Local state
 * -------------------------------------------------------------------------*/

static bool _sdcard_running;
static volatile bool _card_is_sd;
static volatile uint32_t _card_capacity_kb;
static volatile uint32_t _card_last_status;

/* SD card mode flags (bitfield, mirrors ChibiOS sdcp->cardmode) */
static uint32_t _card_mode;
#define SDC_MODE_HIGH_CAPACITY            (1U << 0)
#define SDC_MODE_1BIT                     (1U << 1)
#define SDC_MODE_4BIT                     (1U << 2)

/* Card RCA */
static uint32_t _card_rca;

/* Delay in microseconds using DWT CYCCNT (no RTOS dependency, no yield) */
static inline void _delay_us(uint32_t us)
{
    const uint32_t cycles = us * (SystemCoreClock / 1000000U);
    const uint32_t start = DWT->CYCCNT;
    while ((DWT->CYCCNT - start) < cycles) {
        __DSB();
    }
}

/* Delay in milliseconds using DWT (for short < 10ms waits) */
static inline void _delay_ms(uint32_t ms)
{
    _delay_us(ms * 1000UL);
}

/* ---------------------------------------------------------------------------
 * GPIO helper — direct register access, D-Cache safe
 * -------------------------------------------------------------------------*/

/*
 * Set PG7 to output HIGH to power the SD card slot.
 * Uses direct volatile register access + DSB to avoid D-Cache RMW corruption
 * on STM32F7 (ref: ardupilot-rtt-architecture §D-Cache).
 *
 * ChibiOS Reference: hwdef/fmuv5/hwdef.dat:107 "PG7 VDD_3V3_SD_CARD_EN OUTPUT HIGH"
 */
static void _sd_power_on(void)
{
    /* Set MODER bits [15:14] = 01 (output) */
    GPIOG_MODER = (GPIOG_MODER & ~(0x3UL << 14)) | (0x1UL << 14);
    __DSB();
    /* Set OSPEEDR to 50MHz (bits [15:14] = 11) */
    GPIOG_OSPEEDR = (GPIOG_OSPEEDR & ~(0x3UL << 14)) | (0x3UL << 14);
    __DSB();
    /* Set OTYPER to push-pull (bit 7 = 0, default) */
    GPIOG_OTYPER &= ~(0x1UL << 7);
    __DSB();
    /* Set PUPDR to no-pull (bits [15:14] = 00) */
    GPIOG_PUPDR &= ~(0x3UL << 14);
    __DSB();
    /* Drive HIGH */
    GPIOG_ODR |= (0x1UL << 7);
    __DSB();
}

/*
 * Drive PG7 LOW to power off the SD card slot.
 */
static void _sd_power_off(void)
{
    GPIOG_ODR &= ~(0x1UL << 7);
    __DSB();
}

/* ---------------------------------------------------------------------------
 * RCC clock enable for SDIO peripheral
 * -------------------------------------------------------------------------*/

/*
 * Enable SDMMC1 clock in RCC and configure GPIO pins to AF12.
 *
 * ChibiOS Reference: hal_sdc_lld.c:455  rccEnableSDIO(true)
 */
static void _sdio_clock_enable(void)
{
    /* Enable SDMMC1 clock (RCC_APB2ENR bit 11 = SDMMC1EN) */
    RCC->APB2ENR |= RCC_APB2ENR_SDMMC1EN;
    __DSB();

    /* Enable GPIO clocks for Port C (PC8-PC12) and Port D (PD2) */
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOCEN | RCC_AHB1ENR_GPIODEN;
    __DSB();

    _delay_us(10);  /* wait for clock stable */

    /* Configure SDMMC1 pins:
     *   PC8  — D0  (AF12)
     *   PC9  — D1  (AF12)
     *   PC10 — D2  (AF12)
     *   PC11 — D3  (AF12)
     *   PC12 — CK  (AF12)
     *   PD2  — CMD (AF12)
     *
     * ChibiOS Reference: hal_sdc_lld.c no explicit pin config — done via
     *   chibios_hwdef.py which generates PAL setup in board.c from hwdef.dat.
     *   On RTT we do it here since the RTT BSP may not pre-configure them.
     */
    /* PC8 (D0): AFR[1] bits[3:0] = AF12 = 0b1100
     * PC9 (D1): AFR[1] bits[7:4] = 0b1100
     * PC10 (D2): AFR[1] bits[11:8] = 0b1100
     * PC11 (D3): AFR[1] bits[15:12] = 0b1100
     * PC12 (CK): AFR[1] bits[19:16] = 0b1100 */
    const uint32_t af12_pins_low  = (SDMMC_GPIO_AF << 0) |   /* PC8  */
                                    (SDMMC_GPIO_AF << 4) |   /* PC9  */
                                    (SDMMC_GPIO_AF << 8) |   /* PC10 */
                                    (SDMMC_GPIO_AF << 12);   /* PC11 */
    const uint32_t af12_pins_high = (SDMMC_GPIO_AF << 16);   /* PC12 */
    volatile uint32_t *gpio_afrl   = (volatile uint32_t *)(GPIOC_BASE + 0x20);
    volatile uint32_t *gpio_afrh   = (volatile uint32_t *)(GPIOC_BASE + 0x24);
    volatile uint32_t *gpio_moder  = (volatile uint32_t *)(GPIOC_BASE + 0x00);
    volatile uint32_t *gpio_ospeedr = (volatile uint32_t *)(GPIOC_BASE + 0x08);
    volatile uint32_t *gpio_pupdr  = (volatile uint32_t *)(GPIOC_BASE + 0x0C);

    /* Set MODER to AF mode (bits [1:0]*2 = 10) for PC8-PC12 */
    for (uint32_t pin = 8; pin <= 12; pin++) {
        *gpio_moder = (*gpio_moder & ~(0x3UL << (pin * 2))) | (0x2UL << (pin * 2));
    }
    __DSB();

    /* PC8-PC11: AFRL[31:0], PC12: AFRH[3:0] */
    *gpio_afrl = (*gpio_afrl & 0x0000FFFFUL) | af12_pins_low;
    *gpio_afrh = (*gpio_afrh & 0xFFF0FFFFUL) | af12_pins_high;
    __DSB();

    /* Set OSPEEDR to 50MHz (bits [1:0]*2 = 11) for PC8-PC12 */
    for (uint32_t pin = 8; pin <= 12; pin++) {
        *gpio_ospeedr = (*gpio_ospeedr & ~(0x3UL << (pin * 2))) | (0x3UL << (pin * 2));
    }
    __DSB();

    /* Set PUPDR = 00 (no pull) for PC8-PC12 */
    for (uint32_t pin = 8; pin <= 12; pin++) {
        *gpio_pupdr &= ~(0x3UL << (pin * 2));
    }
    __DSB();

    /* PD2 (CMD): GPIO Port D */
    volatile uint32_t *gpiod_moder   = (volatile uint32_t *)(GPIOD_BASE + 0x00);
    volatile uint32_t *gpiod_afrl    = (volatile uint32_t *)(GPIOD_BASE + 0x20);
    volatile uint32_t *gpiod_ospeedr = (volatile uint32_t *)(GPIOD_BASE + 0x08);
    volatile uint32_t *gpiod_pupdr   = (volatile uint32_t *)(GPIOD_BASE + 0x0C);

    /* PD2 MODER = AF (bits[5:4] = 10) */
    *gpiod_moder = (*gpiod_moder & ~(0x3UL << 4)) | (0x2UL << 4);
    __DSB();
    /* PD2 AFR[0] bits[11:8] = AF12 = 0b1100 */
    *gpiod_afrl = (*gpiod_afrl & ~(0xFUL << 8)) | (SDMMC_GPIO_AF << 8);
    __DSB();
    /* PD2 OSPEEDR = 50MHz */
    *gpiod_ospeedr = (*gpiod_ospeedr & ~(0x3UL << 4)) | (0x3UL << 4);
    __DSB();
    /* PD2 PUPDR = 00 (no pull-up for CMD — card drives it) */
    *gpiod_pupdr &= ~(0x3UL << 4);
    __DSB();
}

/* ---------------------------------------------------------------------------
 * SDIO register-level helpers
 * -------------------------------------------------------------------------*/

/*
 * Wait for the SDIO to be in transfer-ready state (no active command or data).
 *
 * ChibiOS Reference: hal_sdc_lld.c _sdc_wait_for_transfer_state()
 *   Polls STA until CMDACT=0, TXACT=0, RXACT=0.
 */
static bool _sdio_wait_transfer_idle(void)
{
    uint32_t timeout = 1000000;  /* ~1s at 216MHz */
    while ((SDIO->STA & (SDIO_STA_CMDACT | SDIO_STA_TXACT | SDIO_STA_RXACT)) != 0) {
        if (--timeout == 0) {
            return true;  /* timeout */
        }
        __DSB();
    }
    return false;  /* idle */
}

/*
 * Send a command with no response expected.
 *
 * ChibiOS Reference: hal_sdc_lld.c:588-595  sdc_lld_send_cmd_none()
 *   SDIO->ARG = arg;
 *   SDIO->CMD = cmd | CPSMEN;
 *   wait CMDSENT;
 *   ICR = CMDSENTC;
 */
static bool _sdio_send_cmd_none(uint8_t cmd, uint32_t arg)
{
    uint32_t timeout;

    if (_sdio_wait_transfer_idle()) {
        return true;  /* timeout */
    }

    SDIO->ARG = arg;
    __DSB();
    SDIO->CMD = (uint32_t)cmd | SDIO_CMD_CPSMEN;
    __DSB();

    timeout = 1000000;
    while ((SDIO->STA & SDIO_STA_CMDSENT) == 0) {
        if (--timeout == 0) {
            SDIO->ICR = SDIO_ICR_ALL_FLAGS;
            return true;
        }
        __DSB();
    }

    SDIO->ICR = SDIO_ICR_CMDSENTC;
    return false;
}

/*
 * Send a command expecting short response with CRC.
 *
 * ChibiOS Reference: hal_sdc_lld.c:645-661  sdc_lld_send_cmd_short_crc()
 *   Same as short, but also flags CCRCFAIL as error.
 */
static bool _sdio_send_cmd_short_crc(uint8_t cmd, uint32_t arg, uint32_t *resp)
{
    uint32_t timeout;
    uint32_t sta;

    if (_sdio_wait_transfer_idle()) {
        return true;
    }

    SDIO->ARG = arg;
    __DSB();
    SDIO->CMD = (uint32_t)cmd | SDIO_CMD_WAITRESP_0 | SDIO_CMD_CPSMEN;
    __DSB();

    timeout = 1000000;
    while (((sta = SDIO->STA) & (SDIO_STA_CMDREND | SDIO_STA_CTIMEOUT | SDIO_STA_CCRCFAIL)) == 0) {
        if (--timeout == 0) {
            SDIO->ICR = SDIO_ICR_ALL_FLAGS;
            return true;
        }
        __DSB();
    }

    SDIO->ICR = sta & (SDIO_STA_CMDREND | SDIO_STA_CTIMEOUT | SDIO_STA_CCRCFAIL);

    if ((sta & (SDIO_STA_CTIMEOUT | SDIO_STA_CCRCFAIL)) != 0) {
        return true;
    }

    *resp = SDIO->RESP1;
    return false;
}

/*
 * Send a command expecting a long response (136-bit, with CRC).
 *
 * ChibiOS Reference: hal_sdc_lld.c:677-701  sdc_lld_send_cmd_long_crc()
 *   CMD = cmd | WAITRESP_0 | WAITRESP_1 | CPSMEN;
 *   wait CMDREND | CTIMEOUT | CCRCFAIL;
 *   RESP4..RESP1 in reverse order.
 */
static bool _sdio_send_cmd_long_crc(uint8_t cmd, uint32_t arg, uint32_t *resp)
{
    uint32_t timeout;
    uint32_t sta;

    if (_sdio_wait_transfer_idle()) {
        return true;
    }

    SDIO->ARG = arg;
    __DSB();
    SDIO->CMD = (uint32_t)cmd | SDIO_CMD_WAITRESP_0 | SDIO_CMD_WAITRESP_1 | SDIO_CMD_CPSMEN;
    __DSB();

    timeout = 1000000;
    while (((sta = SDIO->STA) & (SDIO_STA_CMDREND | SDIO_STA_CTIMEOUT | SDIO_STA_CCRCFAIL)) == 0) {
        if (--timeout == 0) {
            SDIO->ICR = SDIO_ICR_ALL_FLAGS;
            return true;
        }
        __DSB();
    }

    SDIO->ICR = sta & (SDIO_STA_CMDREND | SDIO_STA_CTIMEOUT | SDIO_STA_CCRCFAIL);

    if ((sta & SDIO_STA_ERROR_MASK) != 0) {
        return true;
    }

    /* Long response: MSB comes first in RESP4, then RESP3, RESP2, RESP1 */
    resp[0] = SDIO->RESP4;
    resp[1] = SDIO->RESP3;
    resp[2] = SDIO->RESP2;
    resp[3] = SDIO->RESP1;
    return false;
}

/*
 * Send an application-specific command (CMD55 + ACMD).
 * CMD55 signals that the next command is an ACMD.
 */
static bool _sdio_send_acmd(uint8_t cmd, uint32_t arg, uint32_t *resp)
{
    uint32_t r1;
    /* CMD55 with card RCA as argument (or 0 before RCA assigned) */
    if (_sdio_send_cmd_short_crc(MMCSD_CMD_APP_CMD, _card_rca, &r1)) {
        return true;
    }
    /* Now send the actual ACMD */
    return _sdio_send_cmd_short_crc(cmd, arg, resp);
}

/*
 * Calculate clock divider value for a target frequency.
 * SDIO_CLK = SDIO_CLOCK_SRC_HZ / (div + 2) where div = CLKCR[7:0].
 *
 * ChibiOS Reference: hal_sdc_lld.c:100-116  sdc_lld_clkdiv()
 *   div = slowdown + (48000000 + f - 1) / f;
 *   if (div == 1) return BYPASS;
 *   return div - 2;
 */
static uint32_t _sdio_clkdiv(uint32_t target_hz)
{
    if (target_hz >= SDIO_CLOCK_SRC_HZ) {
        return SDIO_CLKCR_BYPASS;
    }
    uint32_t div = (SDIO_CLOCK_SRC_HZ + target_hz - 1) / target_hz;
    if (div == 1) {
        return SDIO_CLKCR_BYPASS;
    }
    return div - 2;
}

/* ---------------------------------------------------------------------------
 * SDIO peripheral init / start / stop
 * -------------------------------------------------------------------------*/

/*
 * Initialize and start the SDIO peripheral.
 *
 * ChibiOS Reference: hal_sdc_lld.c:424-464  sdc_lld_start()
 *   Configures DMA mode bits, allocates stream, enables RCC clock,
 *   resets POWER, CLKCR, DCTRL, DTIMER, clears ICR.
 */
static void _sdio_start(void)
{
    /* Reset registers — ChibiOS hal_sdc_lld.c:459-463 */
    SDIO->POWER  = 0;
    SDIO->CLKCR  = 0;
    SDIO->DCTRL  = 0;
    SDIO->DTIMER = 0;
    SDIO->ICR    = SDIO_ICR_ALL_FLAGS;
    __DSB();
}

/*
 * Start the SDIO clock at 400 kHz (identification mode).
 *
 * ChibiOS Reference: hal_sdc_lld.c:499-508  sdc_lld_start_clk()
 *   CLKCR = clkdiv(400kHz)
 *   POWER |= PWRCTRL_0 | PWRCTRL_1
 *   CLKCR |= CLKEN
 *   delay(STM32_SDC_CLOCK_ACTIVATION_DELAY)  — 10ms
 */
static void _sdio_start_clk_400k(void)
{
    SDIO->CLKCR = _sdio_clkdiv(400000);
    __DSB();

    SDIO->POWER |= SDIO_POWER_PWRCTRL_0 | SDIO_POWER_PWRCTRL_1;
    __DSB();

    SDIO->CLKCR |= SDIO_CLKCR_CLKEN;
    __DSB();

    _delay_ms(10);  /* clock activation delay — ChibiOS: STM32_SDC_CLOCK_ACTIVATION_DELAY=10ms */
}

/*
 * Set the SDIO clock to data mode (25 MHz).
 *
 * ChibiOS Reference: hal_sdc_lld.c:518-540  sdc_lld_set_data_clk()
 *   CLKCR = (CLKCR & ~(BYPASS|PWRSAV|CLKDIV)) | clkdiv(25000000) [| PWRSAV]
 */
static void _sdio_set_clk_25mhz(void)
{
    uint32_t clkcr = SDIO->CLKCR & ~(SDIO_CLKCR_BYPASS | SDIO_CLKCR_PWRSAV | SDIO_CLKCR_CLKDIV_Msk);
    SDIO->CLKCR = clkcr | _sdio_clkdiv(25000000);
    __DSB();
}

/*
 * Set bus width (1-bit or 4-bit).
 *
 * ChibiOS Reference: hal_sdc_lld.c:563-577  sdc_lld_set_bus_mode()
 */
static void _sdio_set_bus_width(bool wide_4bit)
{
    uint32_t clkcr = SDIO->CLKCR & ~SDIO_CLKCR_WIDBUS;
    if (wide_4bit) {
        SDIO->CLKCR = clkcr | SDIO_CLKCR_WIDBUS_0;
    } else {
        SDIO->CLKCR = clkcr;
    }
    __DSB();
}

/*
 * Stop the SDIO clock and power.
 *
 * ChibiOS Reference: hal_sdc_lld.c:549-553  sdc_lld_stop_clk()
 *   CLKCR = 0; POWER = 0;
 */
static void _sdio_stop_clk(void)
{
    SDIO->CLKCR = 0;
    SDIO->POWER = 0;
    __DSB();
}

/* ---------------------------------------------------------------------------
 * SD card initialization sequence
 *
 * Reference: SD Association Physical Layer Simplified Spec v3.01 §7
 * ChibiOS: implemented across MMCSD block device layer (mmcsd.c) + SDC LLD
 *   The RTT implementation here combines both layers for building a standalone
 *   driver, since we don't have the ChibiOS SDC/MMCSD framework.
 * -------------------------------------------------------------------------*/

/*
 * GO_IDLE_STATE (CMD0): Reset the card to idle state.
 */
static bool _sd_cmd_go_idle(void)
{
    return _sdio_send_cmd_none(MMCSD_CMD_GO_IDLE_STATE, 0);
}

/*
 * SEND_IF_COND (CMD8): Check SD card interface condition.
 * SDHC cards respond with the same voltage+pattern. SDSC cards don't respond
 * (timeout = not SDHC).
 *
 * Returns: false if the card supports the voltage range (SDHC or SDXC),
 *          true if no response (SDSC or not SD card).
 */
static bool _sd_cmd_send_if_cond(uint32_t *resp)
{
    uint32_t arg = (SD_CMD8_VOLTAGE_27_36) | SD_CMD8_PATTERN;
    return _sdio_send_cmd_short_crc(MMCSD_CMD_SEND_IF_COND, arg, resp);
}

/*
 * ACMD41 (SD_SEND_OP_COND): Initiate card initialization and get OCR.
 * Polls until card exits busy state.
 * Sends HCS bit for SDHC/SDXC support.
 */
static bool _sd_acmd_send_op_cond(bool hcs, uint32_t *ocr)
{
    uint32_t arg = hcs ? SD_ACMD41_HCS : 0;
    uint32_t resp;
    uint32_t retries = 1000;  /* max ~1s at 1ms per retry */

    do {
        if (_sdio_send_acmd(MMCSD_ACMD_SD_SEND_OP_COND, arg, &resp)) {
            /* ACMD41 failed — may be MMIO card, not SD */
            return true;
        }
        if (resp & SD_OCR_BUSY) {
            *ocr = resp;
            return false;  /* success: card powered up */
        }
        _delay_ms(1);
    } while (--retries > 0);

    return true;  /* timeout */
}

/*
 * ALL_SEND_CID (CMD2): Get card identification (CID) register.
 */
static bool _sd_cmd_all_send_cid(uint32_t *cid)
{
    return _sdio_send_cmd_long_crc(MMCSD_CMD_ALL_SEND_CID, 0, cid);
}

/*
 * SET_RELATIVE_ADDR (CMD3): Assign a relative card address (RCA).
 * For SD cards, the card returns its RCA in the response.
 */
static bool _sd_cmd_set_relative_addr(uint32_t *rca)
{
    return _sdio_send_cmd_short_crc(MMCSD_CMD_SET_RELATIVE_ADDR, 0, rca);
}

/*
 * SELECT_CARD (CMD7): Select/deselect a card by RCA.
 */
static bool _sd_cmd_select_card(uint32_t rca, uint32_t *resp)
{
    return _sdio_send_cmd_short_crc(MMCSD_CMD_SELECT_CARD, rca, resp);
}

/*
 * SEND_CSD (CMD9): Get card-specific data register.
 */
static bool _sd_cmd_send_csd(uint32_t *csd)
{
    return _sdio_send_cmd_long_crc(MMCSD_CMD_SEND_CSD, _card_rca, csd);
}

/*
 * SEND_STATUS (CMD13): Get card status register.
 */
static bool _sd_cmd_send_status(uint32_t *status)
{
    return _sdio_send_cmd_short_crc(MMCSD_CMD_SEND_STATUS, _card_rca, status);
}

/*
 * SET_BLOCKLEN (CMD16): Set block length.
 * Only valid for SDSC cards (SDHC/SDXC use fixed 512-byte blocks).
 */
static bool _sd_cmd_set_blocklen(uint32_t blocklen)
{
    uint32_t resp;
    return _sdio_send_cmd_short_crc(MMCSD_CMD_SET_BLOCKLEN, blocklen, &resp);
}

/*
 * ACMD6: Set the bus width.  arg=2 for 4-bit, arg=0 for 1-bit.
 */
static bool _sd_acmd_set_bus_width(uint32_t width)
{
    uint32_t resp;
    if (_sdio_send_acmd(MMCSD_ACMD_SET_BUS_WIDTH, width, &resp)) {
        return true;
    }
    return MMCSD_R1_ERROR(resp) != 0U;
}

/* ---------------------------------------------------------------------------
 * Card mode detection from CSD register (CMD9 response)
 * -------------------------------------------------------------------------*/

/*
 * Parse CSD register to extract card capacity info.
 * CSD structure: 128-bit (4 × 32-bit words), RESP4..RESP1
 *   CSD[127:96] = RESP4 (MSB, first received)
 *   CSD[95:64]  = RESP3
 *   CSD[63:32]  = RESP2
 *   CSD[31:0]   = RESP1
 *
 * CSD_STRUCTURE field at CSD[127:126] indicates version:
 *   0 = CSD version 1.0 (SDSC, max 2GB)
 *   1 = CSD version 2.0 (SDHC/SDXC, >= 2GB)
 */
#define CSD_STRUCTURE(resp4)  (((resp4) >> 30) & 0x3UL)

/* For CSD v2.0: C_SIZE at [69:48], C_SIZE_MULT = 0, BLOCK_LEN = 9 (512B) */
#define CSD_V2_C_SIZE(resp2, resp1)  ((((resp2) & 0x3FUL) << 16) | (((resp1) >> 16) & 0xFFFFUL))

static uint32_t _sd_parse_csd(const uint32_t *csd)
{
    /* CSD[127:96] = csd[0] = RESP4 */
    uint32_t csd_struct = CSD_STRUCTURE(csd[0]);

    if (csd_struct == 1) {
        /* CSD v2.0: HC/XC card */
        uint32_t c_size = CSD_V2_C_SIZE(csd[2], csd[3]);
        /* Capacity = (c_size + 1) * 512K bytes */
        return (c_size + 1) * 512;  /* in KB */
    }

    /* CSD v1.0: standard capacity */
    /* C_SIZE at [73:62], C_SIZE_MULT at [49:47], READ_BL_LEN at [83:80] */
    uint32_t c_size = ((csd[1] >> 6) & 0x3FFUL) | ((csd[2] << 4) & 0x3FC00UL);
    uint32_t c_size_mult = (csd[2] >> 7) & 0x7UL;
    uint32_t read_bl_len = (csd[2] >> 12) & 0xFUL;
    uint32_t blocknr = (c_size + 1) * (1UL << (c_size_mult + 2));
    return (blocknr * (1UL << read_bl_len)) / 1024;
}

/* ---------------------------------------------------------------------------
 * Public API
 * -------------------------------------------------------------------------*/

/*
 * Initialise microSD card if available.
 *
 * Uses direct SDIO register access for power-on, clock config, and card
 * detection/init.  Filesystem mount is handled by board init (RTT DFS).
 *
 * Returns true if the SD card was found and successfully initialized,
 * or if already running / pre-mounted.
 *
 * ChibiOS Reference: libraries/AP_HAL_ChibiOS/sdcard.cpp:55-156
 *   ChibiOS path: sdcStart → sdcConnect → f_mount
 *   RTT path:     direct register init → card detection → check mount
 */
bool sdcard_init(void)
{
    if (_sdcard_running) {
        return true;
    }

    /* Enable DWT cycle counter for _delay_us() timing */
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

    /* Check whether the SD card filesystem is already mounted (by board init).
     * This is the typical RTT scenario: sd_card_mount_sync() in rt_board_init.c
     * mounts the card at "/" during boot, and AP_Filesystem uses POSIX backend. */
    struct stat st;
    if (stat("/APM", &st) == 0 && S_ISDIR(st.st_mode)) {
        _sdcard_running = true;
        return true;
    }

    /* Power on the SD card slot using direct GPIO register access */
    _sd_power_on();
    _delay_ms(10);   /* power-on settle */

    /* Enable SDIO peripheral clock and configure GPIO pins */
    _sdio_clock_enable();

    /* Initialize SDIO registers (reset state) */
    _sdio_start();

    /* Start 400 kHz init clock */
    _sdio_start_clk_400k();

    /* ---- SD card initialization sequence ---- */
    const uint8_t tries = 3;
    for (uint8_t try_num = 0; try_num < tries; try_num++) {
        if (try_num > 0) {
            /* Cycle power between retries */
            _sd_power_off();
            _delay_ms(100);
            _sd_power_on();
            _delay_ms(10);
            _sdio_start();
            _sdio_start_clk_400k();
        }

        /* Step 1: CMD0 — GO_IDLE_STATE */
        if (_sd_cmd_go_idle()) {
            continue;
        }
        _delay_ms(2);

        /* Step 2: CMD8 — SEND_IF_COND to detect SDHC/SDXC */
        uint32_t cmd8_resp;
        bool no_cmd8 = _sd_cmd_send_if_cond(&cmd8_resp);

        bool is_hcs = false;
        bool is_sd = false;

        if (!no_cmd8 && (cmd8_resp & SD_CMD8_MASK) == SD_CMD8_PATTERN) {
            /* CMD8 responded with correct pattern → SDHC/SDXC card */
            is_sd = true;
            is_hcs = true;
        } else if (!no_cmd8) {
            /* CMD8 responded but pattern mismatch → not an SD card */
            continue;
        }
        /* CMD8 timeout → SDSC card, proceed with ACMD41 */

        /* Step 3: ACMD41 — SD_SEND_OP_COND */
        uint32_t ocr;
        if (_sd_acmd_send_op_cond(is_hcs, &ocr)) {
            /* ACMD41 failed after retries */
            continue;
        }

        /* Check OCR for CCS (Card Capacity Status) — only valid if HCS was set */
        if (is_hcs && (ocr & SD_OCR_CCS)) {
            is_hcs = true;
        } else {
            is_hcs = false;
        }
        is_sd = true;

        /* Step 4: CMD2 — ALL_SEND_CID */
        uint32_t cid[4];
        if (_sd_cmd_all_send_cid(cid)) {
            continue;
        }

        /* Step 5: CMD3 — SET_RELATIVE_ADDR */
        uint32_t rca;
        if (_sd_cmd_set_relative_addr(&rca)) {
            continue;
        }
        _card_rca = rca & 0xFFFF0000UL;

        /* Step 6: CMD9 — SEND_CSD to get card capacity */
        uint32_t csd[4];
        if (_sd_cmd_send_csd(csd)) {
            continue;
        }
        _card_capacity_kb = _sd_parse_csd(csd);

        /* Step 7: CMD7 — SELECT_CARD */
        uint32_t status;
        if (_sd_cmd_select_card(_card_rca, &status)) {
            continue;
        }
        _card_last_status = status;
        if (MMCSD_R1_ERROR(status) != 0U) {
            continue;
        }
        if (_sd_cmd_send_status(&status)) {
            continue;
        }
        _card_last_status = status;
        if ((MMCSD_R1_ERROR(status) != 0U) || (MMCSD_R1_READY(status) == 0U)) {
            continue;
        }

        /* Step 8: switch to 4-bit bus width (ACMD6) */
        _card_mode = is_hcs ? SDC_MODE_HIGH_CAPACITY : 0U;
        if (!_sd_acmd_set_bus_width(2)) {   /* arg=2 for 4-bit */
            _sdio_set_bus_width(true);
            _card_mode |= SDC_MODE_4BIT;
        } else {
            _sdio_set_bus_width(false);
            _card_mode |= SDC_MODE_1BIT;
        }

        /* Step 9: set block length (CMD16) for SDSC */
        if (!is_hcs) {
            if (_sd_cmd_set_blocklen(MMCSD_BLOCK_SIZE)) {
                continue;
            }
        }

        /* Step 10: switch to high-speed (25MHz) data clock */
        _sdio_set_clk_25mhz();

        _card_is_sd = is_sd;

        /* Card initialized successfully */
        _sdcard_running = true;

        /* Check if filesystem is mounted (board init may have done it) */
        if (stat("/APM", &st) == 0 && S_ISDIR(st.st_mode)) {
            return true;
        }

        /* Filesystem may need to be mounted by board init or POSIX backend.
         * The card is powered and ready — return success so the caller
         * knows the hardware is ready. */
        return true;
    }

    /* All retries exhausted */
    _sdio_stop_clk();
    _sd_power_off();
    _sdcard_running = false;
    _card_is_sd = false;
    _card_capacity_kb = 0U;
    _card_last_status = 0U;
    return false;
}

/*
 * Stop the SD card interface — unmount and power down.
 *
 * ChibiOS Reference: sdcard.cpp:161-185
 *   sdcDisconnect → sdcStop
 */
void sdcard_stop(void)
{
    if (!_sdcard_running) {
        return;
    }

    /* Stop SDIO clock and power */
    _sdio_stop_clk();

    /* Power down the SD card slot */
    _sd_power_off();

    _sdcard_running = false;
}

/*
 * Retry SD card init.
 *
 * Called periodically from the main loop when the SD card was not
 * previously mounted.
 *
 * ChibiOS Reference: sdcard.cpp:187-201
 */
bool sdcard_retry(void)
{
    if (!_sdcard_running) {
        return sdcard_init();
    }
    return true;
}
