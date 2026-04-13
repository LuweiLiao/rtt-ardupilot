/*
 * LL Flash driver for STM32F767.
 * Direct register access: unlock, erase sector, program word/byte, lock.
 */
#ifndef __DRV_FLASH_LL_H__
#define __DRV_FLASH_LL_H__

#include <stdint.h>

int  flash_ll_unlock(void);
void flash_ll_lock(void);
int  flash_ll_erase_sector(uint8_t sector);
int  flash_ll_program_word(uint32_t addr, uint32_t data);
int  flash_ll_program_bytes(uint32_t addr, const uint8_t *buf, uint32_t len);
int  flash_ll_read(uint32_t addr, uint8_t *buf, uint32_t len);

uint32_t flash_ll_sector_addr(uint8_t sector);
uint32_t flash_ll_sector_size(uint8_t sector);

#endif /* __DRV_FLASH_LL_H__ */
