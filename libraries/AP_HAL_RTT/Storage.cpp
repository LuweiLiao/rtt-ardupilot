/*
 * AP_HAL_RTT — Storage driver
 * Backend order: RAMTRON(FRAM) -> Flash -> RAM stub(last resort).
 */

#include "Storage.h"
#include <AP_HAL/AP_HAL.h>
#include <cstring>
#include <stdio.h>

extern volatile uint32_t rtt_dbg_setup_stage;

extern const AP_HAL::HAL& hal;

namespace RTT
{

void Storage::_storage_open(void)
{
    if (_initialisedType != StorageBackend::None) {
        return;
    }

    rtt_dbg_setup_stage = 500;  // _storage_open entered

    _dirty_mask.clearall();

#if HAL_WITH_RAMTRON
    rtt_dbg_setup_stage = 501;  // trying FRAM
    if (_fram.init() && _fram.read(0, _buffer, RTT_STORAGE_SIZE)) {
        // Verify FRAM data integrity by reading back first line and comparing
        uint8_t verify_buf[RTT_STORAGE_LINE_SIZE];
        bool verified = false;
        for (int attempt = 0; attempt < 3; attempt++) {
            if (_fram.read(0, verify_buf, RTT_STORAGE_LINE_SIZE) &&
                memcmp(verify_buf, _buffer, RTT_STORAGE_LINE_SIZE) == 0) {
                verified = true;
                break;
            }
            ::printf("RTT Storage: FRAM verify attempt %d failed, retrying...\n", attempt);
            hal.scheduler->delay(1);
        }
        if (verified) {
            _initialisedType = StorageBackend::FRAM;
            ::printf("RTT Storage: FRAM backend\n");
            return;
        } else {
            ::printf("RTT Storage: FRAM data inconsistent, falling back\n");
            // FRAM data is unreliable, fall through to next backend
        }
    }
#endif

#ifdef STORAGE_FLASH_PAGE
    rtt_dbg_setup_stage = 502;  // trying Flash
    _flash_load();
    if (_initialisedType == StorageBackend::Flash) {
        ::printf("RTT Storage: Flash backend page=%u\n", (unsigned)_flash_page);
        return;
    }
#endif

    rtt_dbg_setup_stage = 503;  // using stub
    memset(_buffer, 0xFF, RTT_STORAGE_SIZE);
    _initialisedType = StorageBackend::Stub;
    ::printf("RTT Storage: STUB backend (volatile)\n");
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
    _sem.take_blocking();
    if (memcmp(src, &_buffer[dst], n) != 0) {
        memcpy(&_buffer[dst], src, n);
        _mark_dirty(dst, n);
    }
    _sem.give();
}

bool Storage::erase()
{
    _storage_open();
#ifdef STORAGE_FLASH_PAGE
    if (_initialisedType == StorageBackend::Flash) {
        return _flash.erase();
    }
#endif
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
        _last_empty_ms = AP_HAL::millis();
        return;
    }

    uint16_t i;
    for (i = 0; i < RTT_STORAGE_NUM_LINES; i++) {
        if (_dirty_mask.get(i)) {
            break;
        }
    }
    if (i == RTT_STORAGE_NUM_LINES) {
        return;
    }

    _sem.take_blocking();
    memcpy(_tmpline, &_buffer[RTT_STORAGE_LINE_SIZE * i], RTT_STORAGE_LINE_SIZE);
    _sem.give();

    bool write_ok = false;

#if HAL_WITH_RAMTRON
    if (_initialisedType == StorageBackend::FRAM) {
        write_ok = _fram.write(RTT_STORAGE_LINE_SIZE * i, _tmpline, RTT_STORAGE_LINE_SIZE);
        if (write_ok) {
            // Read back to verify
            uint8_t verify_buf[RTT_STORAGE_LINE_SIZE];
            write_ok = _fram.read(RTT_STORAGE_LINE_SIZE * i, verify_buf, RTT_STORAGE_LINE_SIZE) &&
                       memcmp(verify_buf, _tmpline, RTT_STORAGE_LINE_SIZE) == 0;
        }
    }
#endif

#ifdef STORAGE_FLASH_PAGE
    if (_initialisedType == StorageBackend::Flash) {
        write_ok = _flash_write(i);
    }
#endif

    if (write_ok) {
        _sem.take_blocking();
        if (memcmp(_tmpline, &_buffer[RTT_STORAGE_LINE_SIZE * i], RTT_STORAGE_LINE_SIZE) == 0) {
            _dirty_mask.clear(i);
        }
        _sem.give();
    }
}

bool Storage::healthy()
{
    return ((_initialisedType != StorageBackend::None) &&
            (AP_HAL::millis() - _last_empty_ms < 2000U));
}

void Storage::_flash_load(void)
{
#ifdef STORAGE_FLASH_PAGE
    _flash_page = STORAGE_FLASH_PAGE;
    if (_flash.init()) {
        _initialisedType = StorageBackend::Flash;
    } else {
        memset(_buffer, 0xFF, RTT_STORAGE_SIZE);
        _initialisedType = StorageBackend::Stub;
        ::printf("RTT Storage: flash init failed\n");
    }
#endif
}

bool Storage::_flash_write(uint16_t line)
{
#ifdef STORAGE_FLASH_PAGE
    EXPECT_DELAY_MS(1);
    return _flash.write(line * RTT_STORAGE_LINE_SIZE, RTT_STORAGE_LINE_SIZE);
#else
    (void)line;
    return false;
#endif
}

bool Storage::_flash_write_data(uint8_t sector, uint32_t offset, const uint8_t *data, uint16_t length)
{
#ifdef STORAGE_FLASH_PAGE
    const uint32_t base_address = hal.flash->getpageaddr(_flash_page + sector);
    EXPECT_DELAY_MS(1);
    return hal.flash->write(base_address + offset, data, length);
#else
    (void)sector;
    (void)offset;
    (void)data;
    (void)length;
    return false;
#endif
}

bool Storage::_flash_read_data(uint8_t sector, uint32_t offset, uint8_t *data, uint16_t length)
{
#ifdef STORAGE_FLASH_PAGE
    const uint32_t base_address = hal.flash->getpageaddr(_flash_page + sector);
    memcpy(data, ((const uint8_t *)base_address) + offset, length);
    return true;
#else
    (void)sector;
    (void)offset;
    (void)data;
    (void)length;
    return false;
#endif
}

bool Storage::_flash_erase_sector(uint8_t sector)
{
#ifdef STORAGE_FLASH_PAGE
    EXPECT_DELAY_MS(1000);
    return hal.flash->erasepage(_flash_page + sector);
#else
    (void)sector;
    return false;
#endif
}

bool Storage::_flash_erase_ok(void)
{
    return !hal.util->get_soft_armed();
}

bool Storage::get_storage_ptr(void *&ptr, size_t &size)
{
    if (_initialisedType == StorageBackend::None) {
        _storage_open();
    }
    ptr = _buffer;
    size = RTT_STORAGE_SIZE;
    return true;
}

} // namespace RTT
