/*
 * AP_HAL_RTT — STM32 Flash driver (direct register access, no HAL).
 *
 * STM32F767 (2MB) single-bank flash layout:
 *   Pages 0-3:  32KB each
 *   Page 4:     128KB
 *   Pages 5-11: 256KB each
 */

#include "Flash.h"
#include <rtthread.h>
#include <string.h>

#ifdef STM32F767xx
#include <stm32f7xx.h>
#endif

using namespace RTT;

#define STM32_FLASH_BASE_ADDR  0x08000000U
#define KB(x) ((x) * 1024U)

#ifndef BOARD_FLASH_SIZE
#define BOARD_FLASH_SIZE 2048
#endif

#if BOARD_FLASH_SIZE == 2048
#define STM32_FLASH_NPAGES 12
static const uint32_t flash_memmap[STM32_FLASH_NPAGES] = {
    KB(32), KB(32), KB(32), KB(32), KB(128),
    KB(256), KB(256), KB(256), KB(256), KB(256), KB(256), KB(256)
};
#elif BOARD_FLASH_SIZE == 1024
#define STM32_FLASH_NPAGES 8
static const uint32_t flash_memmap[STM32_FLASH_NPAGES] = {
    KB(32), KB(32), KB(32), KB(32), KB(128), KB(256), KB(256), KB(256)
};
#else
#error "BOARD_FLASH_SIZE not supported"
#endif

#ifdef STM32F767xx

static inline int _wait_bsy(uint32_t timeout_loops)
{
    while (FLASH->SR & FLASH_SR_BSY) {
        if (--timeout_loops == 0) return -1;
    }
    return 0;
}

static inline void _clear_errors(void)
{
    FLASH->SR = FLASH_SR_EOP | FLASH_SR_OPERR | FLASH_SR_WRPERR |
                FLASH_SR_PGAERR | FLASH_SR_PGPERR | FLASH_SR_ERSERR;
}

static inline int _flash_unlock(void)
{
    if (FLASH->CR & FLASH_CR_LOCK) {
        FLASH->KEYR = FLASH_KEY1;
        FLASH->KEYR = FLASH_KEY2;
    }
    return (FLASH->CR & FLASH_CR_LOCK) ? -1 : 0;
}

static inline void _flash_lock(void)
{
    FLASH->CR |= FLASH_CR_LOCK;
}

#endif /* STM32F767xx */

uint32_t Flash::getpageaddr(uint32_t page)
{
    if (page >= STM32_FLASH_NPAGES) {
        return 0;
    }
    uint32_t addr = STM32_FLASH_BASE_ADDR;
    for (uint32_t i = 0; i < page; i++) {
        addr += flash_memmap[i];
    }
    return addr;
}

uint32_t Flash::getpagesize(uint32_t page)
{
    if (page >= STM32_FLASH_NPAGES) {
        return 0;
    }
    return flash_memmap[page];
}

uint32_t Flash::getnumpages(void)
{
    return STM32_FLASH_NPAGES;
}

bool Flash::erasepage(uint32_t page)
{
    if (page >= STM32_FLASH_NPAGES) {
        return false;
    }

    _sem.take_blocking();

#ifdef STM32F767xx
    _flash_unlock();
    if (_wait_bsy(0xFFFFFFU)) {
        if (!_keep_unlocked) _flash_lock();
        _sem.give();
        return false;
    }
    _clear_errors();

    /* PSIZE=10 (32-bit parallelism, VDD >= 2.7V) */
    uint32_t cr = FLASH_CR_SER |
                  ((uint32_t)page << FLASH_CR_SNB_Pos) |
                  FLASH_CR_PSIZE_1;
    FLASH->CR = cr;

    rt_base_t level = rt_hw_interrupt_disable();
    FLASH->CR = cr | FLASH_CR_STRT;
    if (_wait_bsy(0xFFFFFFFFU)) {
        rt_hw_interrupt_enable(level);
        FLASH->CR &= ~(FLASH_CR_SER | FLASH_CR_SNB_Msk);
        if (!_keep_unlocked) _flash_lock();
        _sem.give();
        return false;
    }
    rt_hw_interrupt_enable(level);

    FLASH->CR &= ~(FLASH_CR_SER | FLASH_CR_SNB_Msk);
    bool ok = !(FLASH->SR & (FLASH_SR_OPERR | FLASH_SR_WRPERR | FLASH_SR_ERSERR));

    if (!_keep_unlocked) {
        _flash_lock();
    }
#else
    bool ok = false;
#endif

    _sem.give();
    return ok;
}

bool Flash::write(uint32_t addr, const void *buf, uint32_t count)
{
    if (count == 0) {
        return true;
    }

    _sem.take_blocking();

#ifdef STM32F767xx
    _flash_unlock();
    _clear_errors();

    const uint8_t *b = (const uint8_t *)buf;
    bool ok = true;

    rt_base_t level = rt_hw_interrupt_disable();

    while (count > 0 && ok) {
        if (_wait_bsy(0xFFFFFFU)) { ok = false; break; }

        if ((addr & 3) == 0 && count >= 4) {
            FLASH->CR = FLASH_CR_PG | FLASH_CR_PSIZE_1;
            uint32_t val;
            memcpy(&val, b, 4);
            *(volatile uint32_t *)addr = val;
            __DSB();
            if (_wait_bsy(0xFFFFFFU)) { ok = false; break; }
            FLASH->CR &= ~FLASH_CR_PG;
            if (*(volatile uint32_t *)addr != val) { ok = false; break; }
            addr += 4; b += 4; count -= 4;
        } else if ((addr & 1) == 0 && count >= 2) {
            FLASH->CR = FLASH_CR_PG | FLASH_CR_PSIZE_0;
            uint16_t val;
            memcpy(&val, b, 2);
            *(volatile uint16_t *)addr = val;
            __DSB();
            if (_wait_bsy(0xFFFFFFU)) { ok = false; break; }
            FLASH->CR &= ~FLASH_CR_PG;
            addr += 2; b += 2; count -= 2;
        } else {
            FLASH->CR = FLASH_CR_PG;
            *(volatile uint8_t *)addr = *b;
            __DSB();
            if (_wait_bsy(0xFFFFFFU)) { ok = false; break; }
            FLASH->CR &= ~FLASH_CR_PG;
            addr++; b++; count--;
        }
    }

    rt_hw_interrupt_enable(level);

    if (!_keep_unlocked) {
        _flash_lock();
    }
#else
    bool ok = false;
#endif

    _sem.give();
    return ok;
}

void Flash::keep_unlocked(bool set)
{
    _keep_unlocked = set;
}

bool Flash::ispageerased(uint32_t page)
{
    if (page >= STM32_FLASH_NPAGES) {
        return false;
    }
    uint32_t addr = getpageaddr(page);
    uint32_t size = getpagesize(page);
    const uint32_t *p = (const uint32_t *)addr;
    for (uint32_t i = 0; i < size / 4; i++) {
        if (p[i] != 0xFFFFFFFFU) {
            return false;
        }
    }
    return true;
}
