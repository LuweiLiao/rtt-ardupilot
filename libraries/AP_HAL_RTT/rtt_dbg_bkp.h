/*
 * TAMP backup-register witness stamps + fault snapshot (STM32F7).
 * BKP0R: bootloader RTC_BOOT_FWOK (do not use for debug).
 * BKP1R: short witness stamps (CherryUSB init, SW reboot).
 * BKP2R..BKP10R: fault snapshot (survives IWDG reset).
 */
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(SOC_SERIES_STM32F7) || defined(STM32F767xx) || defined(STM32F7)
#include <stm32f7xx.h>
#define RTT_DBG_BKP_WITNESS_REG  ((volatile uint32_t *)0x40002854UL)
#else
#define RTT_DBG_BKP_WITNESS_REG  ((volatile uint32_t *)0UL)
#endif

#define RTT_DBG_BKP_MAGIC        0xC41E0000UL
#define RTT_DBG_BKP_FAULT_MAGIC  0xFA17C000UL

enum {
    RTT_DBG_BKP_TAG_CHERRY_INIT      = 0x0001U,
    RTT_DBG_BKP_TAG_CHERRY_INIT_SKIP = 0x0002U,
    RTT_DBG_BKP_TAG_SW_REBOOT        = 0x0003U,
};

enum {
    RTT_DBG_BKP_FAULT_HARD   = 1U,
    RTT_DBG_BKP_FAULT_BUS    = 2U,
    RTT_DBG_BKP_FAULT_USAGE  = 3U,
    RTT_DBG_BKP_FAULT_MEM    = 4U,
    RTT_DBG_BKP_FAULT_PANIC  = 5U,
};

/* BKP slot layout (STM32F7 RTC->BKPxR, index 0 = BKP0R @ RTC_BASE+0x50) */
enum {
    RTT_DBG_BKP_SLOT_HDR   = 2,  /* magic|type in low 16 bits */
    RTT_DBG_BKP_SLOT_PC    = 3,
    RTT_DBG_BKP_SLOT_LR    = 4,
    RTT_DBG_BKP_SLOT_CFSR  = 5,
    RTT_DBG_BKP_SLOT_HFSR  = 6,
    RTT_DBG_BKP_SLOT_BFAR  = 7,
    RTT_DBG_BKP_SLOT_MMFAR = 8,
    RTT_DBG_BKP_SLOT_XPSR  = 9,
    RTT_DBG_BKP_SLOT_THR   = 10, /* thread name first 4 bytes, big-endian chars */
};

void rtt_dbg_bkp_stamp(uint32_t tag);
void rtt_dbg_bkp_stamp_sw_reboot(void);
void rtt_dbg_bkp_enable_domain(void);

void rtt_dbg_bkp_fault_save(uint32_t fault_type, uint32_t pc, uint32_t lr, uint32_t xpsr);
void rtt_dbg_bkp_hardfault_from_asm(void);

void rtt_dbg_bkp_restore_prev_fault(void);
bool rtt_dbg_bkp_prev_fault_pending(void);
void rtt_dbg_bkp_format_prev_fault(char *buf, size_t len);
void rtt_dbg_bkp_consume_prev_fault(void);

/* Globals populated at boot from BKP (valid until consume) */
extern uint32_t rtt_last_fault_pc;
extern uint32_t rtt_last_fault_lr;
extern uint32_t rtt_last_fault_cfsr;
extern uint32_t rtt_last_fault_hfsr;
extern uint32_t rtt_last_fault_bfar;
extern uint32_t rtt_last_fault_mmfar;
extern uint32_t rtt_last_fault_xpsr;
extern uint32_t rtt_last_fault_type;
extern char rtt_last_fault_thr[5];

#ifdef __cplusplus
}
#endif
