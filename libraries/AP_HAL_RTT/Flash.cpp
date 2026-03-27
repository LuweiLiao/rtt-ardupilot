/*
 * AP_HAL_RTT — STM32 Flash driver
 * Uses STM32 HAL directly (no ChibiOS dependencies).
 *
 * STM32F767 (2MB) flash layout:
 *   Pages 0-3:  32KB each
 *   Page 4:     128KB
 *   Pages 5-11: 256KB each
 *   Total: 4*32 + 128 + 7*256 = 2048KB
 */

#include "Flash.h"
#include <rtthread.h>
#include <string.h>

#ifdef STM32F767xx
#include <stm32f7xx_hal.h>
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
    HAL_FLASH_Unlock();

    __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_EOP | FLASH_FLAG_OPERR |
                           FLASH_FLAG_WRPERR | FLASH_FLAG_PGAERR |
                           FLASH_FLAG_PGPERR | FLASH_FLAG_ERSERR);

    FLASH_EraseInitTypeDef erase;
    erase.TypeErase = FLASH_TYPEERASE_SECTORS;
    erase.Sector = page;
    erase.NbSectors = 1;
    erase.VoltageRange = FLASH_VOLTAGE_RANGE_3;

    uint32_t error = 0;
    rt_base_t level = rt_hw_interrupt_disable();
    HAL_StatusTypeDef status = HAL_FLASHEx_Erase(&erase, &error);
    rt_hw_interrupt_enable(level);

    if (!_keep_unlocked) {
        HAL_FLASH_Lock();
    }
#else
    HAL_StatusTypeDef status = HAL_ERROR;
#endif

    _sem.give();
    return status == HAL_OK;
}

bool Flash::write(uint32_t addr, const void *buf, uint32_t count)
{
    if (count == 0) {
        return true;
    }

    _sem.take_blocking();

#ifdef STM32F767xx
    HAL_FLASH_Unlock();

    __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_EOP | FLASH_FLAG_OPERR |
                           FLASH_FLAG_WRPERR | FLASH_FLAG_PGAERR |
                           FLASH_FLAG_PGPERR | FLASH_FLAG_ERSERR);

    const uint8_t *b = (const uint8_t *)buf;
    HAL_StatusTypeDef status = HAL_OK;

    rt_base_t level = rt_hw_interrupt_disable();

    while (count > 0 && status == HAL_OK) {
        if ((addr & 3) == 0 && count >= 4) {
            uint32_t val;
            memcpy(&val, b, 4);
            status = HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, addr, val);
            addr += 4;
            b += 4;
            count -= 4;
        } else if ((addr & 1) == 0 && count >= 2) {
            uint16_t val;
            memcpy(&val, b, 2);
            status = HAL_FLASH_Program(FLASH_TYPEPROGRAM_HALFWORD, addr, val);
            addr += 2;
            b += 2;
            count -= 2;
        } else {
            status = HAL_FLASH_Program(FLASH_TYPEPROGRAM_BYTE, addr, *b);
            addr++;
            b++;
            count--;
        }
    }

    rt_hw_interrupt_enable(level);

    if (!_keep_unlocked) {
        HAL_FLASH_Lock();
    }
#else
    HAL_StatusTypeDef status = HAL_ERROR;
#endif

    _sem.give();
    return status == HAL_OK;
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
