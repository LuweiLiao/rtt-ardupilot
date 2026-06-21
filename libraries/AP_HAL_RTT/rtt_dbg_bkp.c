/*
 * TAMP backup registers — witness stamps + fault snapshot (STM32F7).
 * BKP0R: bootloader FWOK; BKP1R: witness; BKP2R..10: fault (IWDG-safe).
 */
#include "rtt_dbg_bkp.h"

#include <stdio.h>
#include <string.h>

#if defined(SOC_SERIES_STM32F7) || defined(STM32F767xx) || defined(STM32F7)
#include <stm32f7xx.h>
#include <rtthread.h>
#endif

uint32_t rtt_last_fault_pc;
uint32_t rtt_last_fault_lr;
uint32_t rtt_last_fault_cfsr;
uint32_t rtt_last_fault_hfsr;
uint32_t rtt_last_fault_bfar;
uint32_t rtt_last_fault_mmfar;
uint32_t rtt_last_fault_xpsr;
uint32_t rtt_last_fault_type;
char rtt_last_fault_thr[5] = {0};

#if defined(SOC_SERIES_STM32F7) || defined(STM32F767xx) || defined(STM32F7)

#define RTT_DBG_BKP_REG(n) ((volatile uint32_t *)(0x40002850UL + (uint32_t)(n) * 4UL))

#define SCB_CFSR  (*(volatile uint32_t *)0xE000ED28U)
#define SCB_HFSR  (*(volatile uint32_t *)0xE000ED2CU)
#define SCB_BFAR  (*(volatile uint32_t *)0xE000ED38U)
#define SCB_MMFAR (*(volatile uint32_t *)0xE000ED34U)

static void bkp_enable_access(void)
{
    RCC->APB1ENR |= RCC_APB1ENR_PWREN | RCC_APB1ENR_RTCEN;
    (void)RCC->APB1ENR;
    PWR->CR1 |= PWR_CR1_DBP;
    __DSB();

    if ((RCC->BDCR & RCC_BDCR_RTCEN) == 0U) {
        RCC->BDCR |= RCC_BDCR_LSEON;
        for (volatile uint32_t i = 0; i < 500000U; i++) {
            if ((RCC->BDCR & RCC_BDCR_LSERDY) != 0U) {
                break;
            }
        }
        if ((RCC->BDCR & RCC_BDCR_LSERDY) != 0U) {
            RCC->BDCR &= ~RCC_BDCR_RTCSEL_Msk;
            RCC->BDCR |= RCC_BDCR_RTCSEL_0;
        } else {
            RCC->CSR |= RCC_CSR_LSION;
            for (volatile uint32_t i = 0; i < 500000U; i++) {
                if ((RCC->CSR & RCC_CSR_LSIRDY) != 0U) {
                    break;
                }
            }
            RCC->BDCR &= ~RCC_BDCR_RTCSEL_Msk;
            RCC->BDCR |= RCC_BDCR_RTCSEL_1;
        }
        RCC->BDCR |= RCC_BDCR_RTCEN;
    }
    __DSB();
}

static void bkp_write_slot(unsigned slot, uint32_t val)
{
    *RTT_DBG_BKP_REG(slot) = val;
    __DSB();
}

static uint32_t bkp_read_slot(unsigned slot)
{
    volatile uint32_t *reg = RTT_DBG_BKP_REG(slot);
    return *reg;
}

static uint32_t pack_thread_name(void)
{
    rt_thread_t th = rt_thread_self();
    if (th == RT_NULL || th->parent.name[0] == '\0') {
        return 0U;
    }
    const char *n = th->parent.name;
    return ((uint32_t)(uint8_t)n[0] << 24)
         | ((uint32_t)(uint8_t)n[1] << 16)
         | ((uint32_t)(uint8_t)n[2] << 8)
         | ((uint32_t)(uint8_t)n[3]);
}

static void unpack_thread_name(uint32_t packed, char *out, size_t out_len)
{
    if (out == NULL || out_len == 0U) {
        return;
    }
    if (out_len < 5U) {
        snprintf(out, out_len, "?");
        return;
    }
    out[0] = (char)((packed >> 24) & 0xFFU);
    out[1] = (char)((packed >> 16) & 0xFFU);
    out[2] = (char)((packed >> 8) & 0xFFU);
    out[3] = (char)(packed & 0xFFU);
    out[4] = '\0';
    if (out[0] == '\0') {
        snprintf(out, out_len, "?");
    }
}

#endif /* SOC_SERIES_STM32F7 || STM32F767xx */

void rtt_dbg_bkp_enable_domain(void)
{
#if defined(SOC_SERIES_STM32F7) || defined(STM32F767xx) || defined(STM32F7)
    bkp_enable_access();
#endif
}

void rtt_dbg_bkp_stamp(uint32_t tag)
{
#if defined(SOC_SERIES_STM32F7) || defined(STM32F767xx) || defined(STM32F7)
    bkp_enable_access();
    *RTT_DBG_BKP_WITNESS_REG = RTT_DBG_BKP_MAGIC | (tag & 0xFFFFU);
    __DSB();
#else
    (void)tag;
#endif
}

void rtt_dbg_bkp_stamp_sw_reboot(void)
{
    rtt_dbg_bkp_stamp(RTT_DBG_BKP_TAG_SW_REBOOT);
}

void rtt_dbg_bkp_fault_save(uint32_t fault_type, uint32_t pc, uint32_t lr, uint32_t xpsr)
{
#if defined(SOC_SERIES_STM32F7) || defined(STM32F767xx) || defined(STM32F7)
    bkp_enable_access();
    bkp_write_slot(RTT_DBG_BKP_SLOT_HDR,
                   RTT_DBG_BKP_FAULT_MAGIC | (fault_type & 0xFFFFU));
    bkp_write_slot(RTT_DBG_BKP_SLOT_PC, pc);
    bkp_write_slot(RTT_DBG_BKP_SLOT_LR, lr);
    bkp_write_slot(RTT_DBG_BKP_SLOT_CFSR, SCB_CFSR);
    bkp_write_slot(RTT_DBG_BKP_SLOT_HFSR, SCB_HFSR);
    bkp_write_slot(RTT_DBG_BKP_SLOT_BFAR, SCB_BFAR);
    bkp_write_slot(RTT_DBG_BKP_SLOT_MMFAR, SCB_MMFAR);
    bkp_write_slot(RTT_DBG_BKP_SLOT_XPSR, xpsr);
    bkp_write_slot(RTT_DBG_BKP_SLOT_THR, pack_thread_name());
#else
    (void)fault_type;
    (void)pc;
    (void)lr;
    (void)xpsr;
#endif
}

void rtt_dbg_bkp_hardfault_from_asm(void)
{
#if defined(SOC_SERIES_STM32F7) || defined(STM32F767xx) || defined(STM32F7)
    extern volatile rt_uint32_t rtt_dbg_hardfault_stack_pc;
    extern volatile rt_uint32_t rtt_dbg_hardfault_stack_lr;
    extern volatile rt_uint32_t rtt_dbg_hardfault_stack_xpsr;
    rtt_dbg_bkp_fault_save(RTT_DBG_BKP_FAULT_HARD,
                           (uint32_t)rtt_dbg_hardfault_stack_pc,
                           (uint32_t)rtt_dbg_hardfault_stack_lr,
                           (uint32_t)rtt_dbg_hardfault_stack_xpsr);
#endif
}

__attribute__((noinline))
void rtt_dbg_bkp_restore_prev_fault(void)
{
#if defined(SOC_SERIES_STM32F7) || defined(STM32F767xx) || defined(STM32F7)
    bkp_enable_access();
    const uint32_t hdr = bkp_read_slot(RTT_DBG_BKP_SLOT_HDR);
    if ((hdr & 0xFFFF0000U) != (RTT_DBG_BKP_FAULT_MAGIC & 0xFFFF0000U)) {
        return;
    }
    rtt_last_fault_type = hdr & 0xFFFFU;
    rtt_last_fault_pc = bkp_read_slot(RTT_DBG_BKP_SLOT_PC);
    rtt_last_fault_lr = bkp_read_slot(RTT_DBG_BKP_SLOT_LR);
    rtt_last_fault_cfsr = bkp_read_slot(RTT_DBG_BKP_SLOT_CFSR);
    rtt_last_fault_hfsr = bkp_read_slot(RTT_DBG_BKP_SLOT_HFSR);
    rtt_last_fault_bfar = bkp_read_slot(RTT_DBG_BKP_SLOT_BFAR);
    rtt_last_fault_mmfar = bkp_read_slot(RTT_DBG_BKP_SLOT_MMFAR);
    rtt_last_fault_xpsr = bkp_read_slot(RTT_DBG_BKP_SLOT_XPSR);
    unpack_thread_name(bkp_read_slot(RTT_DBG_BKP_SLOT_THR),
                       rtt_last_fault_thr,
                       sizeof(rtt_last_fault_thr));
#else
    rtt_last_fault_type = 0U;
#endif
}

bool rtt_dbg_bkp_prev_fault_pending(void)
{
    return rtt_last_fault_pc != 0U;
}

void rtt_dbg_bkp_format_prev_fault(char *buf, size_t len)
{
    if (buf == NULL || len == 0U) {
        return;
    }
    snprintf(buf, len,
             "PrevFault PC=%08lx CFSR=%08lx thr=%s",
             (unsigned long)rtt_last_fault_pc,
             (unsigned long)rtt_last_fault_cfsr,
             rtt_last_fault_thr[0] ? rtt_last_fault_thr : "?");
}

void rtt_dbg_bkp_consume_prev_fault(void)
{
#if defined(SOC_SERIES_STM32F7) || defined(STM32F767xx) || defined(STM32F7)
    bkp_enable_access();
    bkp_write_slot(RTT_DBG_BKP_SLOT_HDR, 0U);
#endif
    rtt_last_fault_pc = 0U;
    rtt_last_fault_lr = 0U;
    rtt_last_fault_cfsr = 0U;
    rtt_last_fault_hfsr = 0U;
    rtt_last_fault_bfar = 0U;
    rtt_last_fault_mmfar = 0U;
    rtt_last_fault_xpsr = 0U;
    rtt_last_fault_type = 0U;
    rtt_last_fault_thr[0] = '\0';
}
