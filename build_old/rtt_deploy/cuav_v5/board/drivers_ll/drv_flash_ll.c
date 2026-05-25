/*
 * LL Flash driver for STM32F767, single-bank 2MB.
 * Direct FLASH register access — no HAL dependency.
 */

#include "drv_flash_ll.h"
#include "stm32f7xx.h"

#define FLASH_KEY1   0x45670123U
#define FLASH_KEY2   0xCDEF89ABU
#define FLASH_BASE_ADDR  0x08000000U

/* F767 single-bank sector sizes (12 sectors, total 2MB) */
static const uint32_t _sector_sizes[12] = {
    32*1024, 32*1024, 32*1024, 32*1024, 128*1024,
    256*1024, 256*1024, 256*1024, 256*1024, 256*1024, 256*1024, 256*1024
};

uint32_t flash_ll_sector_addr(uint8_t sector)
{
    if (sector >= 12) return 0;
    uint32_t addr = FLASH_BASE_ADDR;
    for (uint8_t i = 0; i < sector; i++)
        addr += _sector_sizes[i];
    return addr;
}

uint32_t flash_ll_sector_size(uint8_t sector)
{
    if (sector >= 12) return 0;
    return _sector_sizes[sector];
}

static int _wait_bsy(uint32_t timeout_loops)
{
    while (FLASH->SR & FLASH_SR_BSY) {
        if (--timeout_loops == 0) return -1;
    }
    return 0;
}

static void _clear_errors(void)
{
    FLASH->SR = FLASH_SR_EOP | FLASH_SR_OPERR | FLASH_SR_WRPERR |
                FLASH_SR_PGAERR | FLASH_SR_PGPERR | FLASH_SR_ERSERR;
}

int flash_ll_unlock(void)
{
    if (FLASH->CR & FLASH_CR_LOCK) {
        FLASH->KEYR = FLASH_KEY1;
        FLASH->KEYR = FLASH_KEY2;
    }
    return (FLASH->CR & FLASH_CR_LOCK) ? -1 : 0;
}

void flash_ll_lock(void)
{
    FLASH->CR |= FLASH_CR_LOCK;
}

int flash_ll_erase_sector(uint8_t sector)
{
    if (sector >= 12) return -1;
    if (_wait_bsy(0xFFFFFFU)) return -2;
    _clear_errors();

    /* Voltage range 3 (2.7-3.6V): PSIZE=10 (32-bit parallelism) */
    uint32_t cr = FLASH_CR_SER |
                  ((uint32_t)sector << FLASH_CR_SNB_Pos) |
                  FLASH_CR_PSIZE_1;
    FLASH->CR = cr;
    FLASH->CR = cr | FLASH_CR_STRT;

    if (_wait_bsy(0xFFFFFFFFU)) return -3;

    FLASH->CR &= ~(FLASH_CR_SER | FLASH_CR_SNB_Msk);
    return (FLASH->SR & (FLASH_SR_OPERR | FLASH_SR_WRPERR | FLASH_SR_ERSERR)) ? -4 : 0;
}

int flash_ll_program_word(uint32_t addr, uint32_t data)
{
    if (addr & 3) return -1;  /* must be 4-byte aligned */
    if (_wait_bsy(0xFFFFFFU)) return -2;
    _clear_errors();

    /* PSIZE=10 (32-bit), PG enable */
    FLASH->CR = FLASH_CR_PG | FLASH_CR_PSIZE_1;
    *(volatile uint32_t *)addr = data;
    __DSB();

    if (_wait_bsy(0xFFFFFFU)) return -3;
    FLASH->CR &= ~FLASH_CR_PG;
    return (*(volatile uint32_t *)addr == data) ? 0 : -4;
}

int flash_ll_program_bytes(uint32_t addr, const uint8_t *buf, uint32_t len)
{
    /* Word-aligned bulk write, byte fallback for unaligned head/tail */
    uint32_t pos = 0;

    /* Byte-by-byte for unaligned head */
    while (pos < len && (addr + pos) & 3) {
        if (_wait_bsy(0xFFFFFFU)) return -1;
        _clear_errors();
        FLASH->CR = FLASH_CR_PG;  /* PSIZE=00 (byte) */
        *(volatile uint8_t *)(addr + pos) = buf[pos];
        __DSB();
        if (_wait_bsy(0xFFFFFFU)) return -2;
        FLASH->CR &= ~FLASH_CR_PG;
        if (*(volatile uint8_t *)(addr + pos) != buf[pos]) return -3;
        pos++;
    }

    /* Word writes for aligned middle */
    while (pos + 4 <= len) {
        uint32_t word;
        memcpy(&word, buf + pos, 4);  /* safe for unaligned buf */
        int rc = flash_ll_program_word(addr + pos, word);
        if (rc) return rc;
        pos += 4;
    }

    /* Byte-by-byte for tail */
    while (pos < len) {
        if (_wait_bsy(0xFFFFFFU)) return -4;
        _clear_errors();
        FLASH->CR = FLASH_CR_PG;
        *(volatile uint8_t *)(addr + pos) = buf[pos];
        __DSB();
        if (_wait_bsy(0xFFFFFFU)) return -5;
        FLASH->CR &= ~FLASH_CR_PG;
        if (*(volatile uint8_t *)(addr + pos) != buf[pos]) return -6;
        pos++;
    }

    return 0;
}

int flash_ll_read(uint32_t addr, uint8_t *buf, uint32_t len)
{
    for (uint32_t i = 0; i < len; i++)
        buf[i] = *(volatile uint8_t *)(addr + i);
    return 0;
}
