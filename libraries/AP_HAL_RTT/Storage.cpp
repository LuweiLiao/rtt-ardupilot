/*
 * AP_HAL_RTT — Storage driver
 * RAM-based storage with optional RAMTRON backend.
 * Parameters are kept in RAM and survive reboot only with RAMTRON.
 * Flash backend requires BSP-level support (future work).
 */

#include "Storage.h"
#include <AP_HAL/AP_HAL.h>
#include <cstring>
#include <stdio.h>

extern const AP_HAL::HAL& hal;

namespace RTT
{

void Storage::_storage_open(void)
{
    if (_initialisedType != StorageBackend::None) {
        return;
    }

    _dirty_mask.clearall();

#if HAL_WITH_RAMTRON
    if (_fram.init() && _fram.read(0, _buffer, RTT_STORAGE_SIZE)) {
        _initialisedType = StorageBackend::FRAM;
        return;
    }
#endif

    memset(_buffer, 0xFF, RTT_STORAGE_SIZE);
    _initialisedType = StorageBackend::Stub;
}

void Storage::_mark_dirty(uint16_t loc, uint16_t length)
{
    if (length == 0) return;
    uint16_t end = loc + length - 1;
    for (uint16_t line = loc >> RTT_STORAGE_LINE_SHIFT;
         line <= (end >> RTT_STORAGE_LINE_SHIFT);
         line++) {
        _dirty_mask.set(line);
    }
}

void Storage::read_block(void *dst, uint16_t src, size_t n)
{
    if (dst == nullptr || n > RTT_STORAGE_SIZE || src > (RTT_STORAGE_SIZE - n)) {
        return;
    }
    _storage_open();
    memcpy(dst, &_buffer[src], n);
}

void Storage::write_block(uint16_t dst, const void* src, size_t n)
{
    if (src == nullptr || n > RTT_STORAGE_SIZE || dst > (RTT_STORAGE_SIZE - n)) {
        return;
    }
    _storage_open();
    if (memcmp(src, &_buffer[dst], n) != 0) {
        _sem.take_blocking();
        memcpy(&_buffer[dst], src, n);
        _mark_dirty(dst, n);
        _sem.give();
    }
}

bool Storage::erase()
{
    _storage_open();
    memset(_buffer, 0xFF, RTT_STORAGE_SIZE);
    _dirty_mask.clearall();
    return true;
}

void Storage::_timer_tick(void)
{
    if (_initialisedType == StorageBackend::None || _initialisedType == StorageBackend::Stub) {
        return;
    }
    if (_dirty_mask.empty()) {
        return;
    }

#if HAL_WITH_RAMTRON
    if (_initialisedType == StorageBackend::FRAM) {
        uint16_t i;
        for (i = 0; i < RTT_STORAGE_NUM_LINES; i++) {
            if (_dirty_mask.get(i)) break;
        }
        if (i == RTT_STORAGE_NUM_LINES) return;

        _sem.take_blocking();
        memcpy(_tmpline, &_buffer[RTT_STORAGE_LINE_SIZE * i], RTT_STORAGE_LINE_SIZE);
        _sem.give();

        if (_fram.write(RTT_STORAGE_LINE_SIZE * i, _tmpline, RTT_STORAGE_LINE_SIZE)) {
            _sem.take_blocking();
            if (memcmp(_tmpline, &_buffer[RTT_STORAGE_LINE_SIZE * i], RTT_STORAGE_LINE_SIZE) == 0) {
                _dirty_mask.clear(i);
            }
            _sem.give();
        }
    }
#endif
}

} // namespace RTT
