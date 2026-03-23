/*
 * AP_HAL_RTT — Storage driver
 * Backends: RAMTRON (SPI FRAM), STM32 Flash, or RAM stub.
 */

#pragma once

#include <AP_HAL/AP_HAL.h>
#include <AP_Common/Bitmask.h>
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

namespace RTT
{

class Storage : public AP_HAL::Storage
{
public:
    void init() override {}
    bool healthy() override { return _initialisedType != StorageBackend::None; }
    void read_block(void *dst, uint16_t src, size_t n) override;
    void write_block(uint16_t dst, const void* src, size_t n) override;
    void _timer_tick(void) override;
    bool erase() override;

private:
    enum class StorageBackend : uint8_t {
        None,
        FRAM,
        Flash,
        Stub,
    };
    StorageBackend _initialisedType = StorageBackend::None;

    void _storage_open(void);
    void _mark_dirty(uint16_t loc, uint16_t length);
    void _flash_load(void);
    void _flash_write_sector(void);

    uint8_t _buffer[RTT_STORAGE_SIZE] __attribute__((aligned(4)));
    Bitmask<RTT_STORAGE_NUM_LINES> _dirty_mask;
    Semaphore _sem;
    uint8_t _tmpline[RTT_STORAGE_LINE_SIZE];
    bool _flash_write_pending = false;

#if HAL_WITH_RAMTRON
    AP_RAMTRON _fram;
#endif
};

} // namespace RTT
