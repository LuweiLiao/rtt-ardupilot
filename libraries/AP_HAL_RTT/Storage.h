/*
 * AP_HAL_RTT — Storage driver
 * Backends: RAMTRON (SPI FRAM) with Flash fallback.
 */

#pragma once

#include <AP_HAL/AP_HAL.h>
#include <AP_Common/Bitmask.h>
#include <AP_FlashStorage/AP_FlashStorage.h>
#include "HAL_RTT_Namespace.h"
#include "Semaphores.h"

#ifndef HAL_STORAGE_SIZE
#define HAL_STORAGE_SIZE 16384
#endif

#define RTT_STORAGE_SIZE HAL_STORAGE_SIZE
#define RTT_STORAGE_LINE_SHIFT 3
#define RTT_STORAGE_LINE_SIZE (1 << RTT_STORAGE_LINE_SHIFT)
#define RTT_STORAGE_NUM_LINES (RTT_STORAGE_SIZE / RTT_STORAGE_LINE_SIZE)

static_assert(RTT_STORAGE_SIZE % RTT_STORAGE_LINE_SIZE == 0,
              "Storage is not multiple of line size");

#if HAL_WITH_RAMTRON
#include <AP_RAMTRON/AP_RAMTRON.h>
#endif

#ifdef STORAGE_FLASH_PAGE
#ifndef RTT_FLASH_SECTOR_SIZE
#define RTT_FLASH_SECTOR_SIZE (256U * 1024U)
#endif
#endif

namespace RTT
{

class Storage : public AP_HAL::Storage
{
public:
    void init() override {}
    bool healthy() override;
    void read_block(void *dst, uint16_t src, size_t n) override;
    void write_block(uint16_t dst, const void* src, size_t n) override;
    void _timer_tick(void) override;
    bool flush(uint32_t timeout_ms);
    bool erase() override;
    bool get_storage_ptr(void *&ptr, size_t &size) override;

private:
    enum class StorageBackend : uint8_t {
        None,
        FRAM,
        Flash,
        Stub,
    };
    StorageBackend _initialisedType = StorageBackend::None;

    void _storage_open(void);
    void _publish_backend_debug(void);
    void _mark_dirty(uint16_t loc, uint16_t length);
    void _flash_load(void);
    bool _flash_write(uint16_t line);
    bool _flash_write_data(uint8_t sector, uint32_t offset, const uint8_t *data, uint16_t length);
    bool _flash_read_data(uint8_t sector, uint32_t offset, uint8_t *data, uint16_t length);
    bool _flash_erase_sector(uint8_t sector);
    bool _flash_erase_ok(void);

    uint8_t _buffer[RTT_STORAGE_SIZE] __attribute__((aligned(4)));
    Bitmask<RTT_STORAGE_NUM_LINES> _dirty_mask;
    Semaphore _sem;
    uint8_t _tmpline[RTT_STORAGE_LINE_SIZE];
    uint16_t _flash_page = 0;
    uint32_t _last_empty_ms = 0;
    uint32_t _last_re_init_ms = 0;

#ifdef STORAGE_FLASH_PAGE
    AP_FlashStorage _flash{
        _buffer,
        RTT_FLASH_SECTOR_SIZE,
        FUNCTOR_BIND_MEMBER(&Storage::_flash_write_data, bool, uint8_t, uint32_t, const uint8_t *, uint16_t),
        FUNCTOR_BIND_MEMBER(&Storage::_flash_read_data, bool, uint8_t, uint32_t, uint8_t *, uint16_t),
        FUNCTOR_BIND_MEMBER(&Storage::_flash_erase_sector, bool, uint8_t),
        FUNCTOR_BIND_MEMBER(&Storage::_flash_erase_ok, bool)};
#endif

#if HAL_WITH_RAMTRON
    AP_RAMTRON _fram;
#endif
};

} // namespace RTT
