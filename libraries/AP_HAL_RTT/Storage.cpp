/*
 * AP_HAL_RTT — Storage driver
 * Backend order: RAMTRON(FRAM) -> Flash -> RAM stub(last resort).
 *
 * Flash low-level operations use CMSIS register direct access
 * (FLASH->CR/SR/KEYR) — no HAL_FLASH_* abstraction layer.
 */

#include "Storage.h"
#include "SPIDevice.h"
#include <AP_HAL/AP_HAL.h>
#include <cstring>
#include <stdio.h>

#ifdef STM32F767xx
#include <stm32f7xx.h>
#endif

extern volatile uint32_t rtt_dbg_setup_stage;
#ifndef AP_RTT_STORAGE_DEBUG
#define AP_RTT_STORAGE_DEBUG 1
#endif

#if AP_RTT_STORAGE_DEBUG
#define RTT_STORAGE_DBG_BSS __attribute__((section(".dtcm_bss.rtt_dbg"), used))
volatile uint32_t rtt_dbg_storage_backend RTT_STORAGE_DBG_BSS;
volatile uint32_t rtt_dbg_fram_probe RTT_STORAGE_DBG_BSS; /* 1=init ok, 2=read ok, 3=init fail, 4=read fail */
volatile uint32_t rtt_dbg_fram_tick_ok RTT_STORAGE_DBG_BSS;
volatile uint32_t rtt_dbg_fram_tick_fail RTT_STORAGE_DBG_BSS;
volatile uint32_t rtt_dbg_storage_erase_calls RTT_STORAGE_DBG_BSS;
volatile uint32_t rtt_dbg_storage_last_read_ofs RTT_STORAGE_DBG_BSS;
volatile uint32_t rtt_dbg_storage_last_read_len RTT_STORAGE_DBG_BSS;
volatile uint32_t rtt_dbg_storage_last_write_ofs RTT_STORAGE_DBG_BSS;
volatile uint32_t rtt_dbg_storage_last_write_len RTT_STORAGE_DBG_BSS;
volatile uint32_t rtt_dbg_storage_last_dirty_line RTT_STORAGE_DBG_BSS;
volatile uint32_t rtt_dbg_storage_last_tick_line RTT_STORAGE_DBG_BSS;
volatile uint32_t rtt_dbg_storage_dirty_empty_ms RTT_STORAGE_DBG_BSS;
volatile uint32_t rtt_dbg_storage_tick_calls RTT_STORAGE_DBG_BSS;
volatile uint32_t rtt_dbg_storage_empty_ticks RTT_STORAGE_DBG_BSS;
volatile uint32_t rtt_dbg_storage_last_tick_ms RTT_STORAGE_DBG_BSS;
volatile uint32_t rtt_dbg_storage_last_empty_ms RTT_STORAGE_DBG_BSS;
volatile uint32_t rtt_dbg_storage_dirty_lines RTT_STORAGE_DBG_BSS;
volatile uint32_t rtt_dbg_storage_healthy_state RTT_STORAGE_DBG_BSS;
#endif

extern const AP_HAL::HAL& hal;

#define STORAGE_FLASH_RETRIES 5
#define STM32_FLASH_BASE_ADDR  0x08000000U
#define KB(x) ((x) * 1024U)

/*
 * Flash page layout for internal STM32F76xxx (2 MiB, 12 pages).
 * Used to translate logical page -> physical address when bypassing
 * hal.flash-> in CMSIS-direct storage operations.
 */
#ifndef BOARD_FLASH_SIZE
#define BOARD_FLASH_SIZE 2048
#endif

#if BOARD_FLASH_SIZE == 2048
#define STORAGE_FLASH_NPAGES 12
static const uint32_t storage_flash_sizes[STORAGE_FLASH_NPAGES] = {
    KB(32), KB(32), KB(32), KB(32), KB(128),
    KB(256), KB(256), KB(256), KB(256), KB(256), KB(256), KB(256)
};
#elif BOARD_FLASH_SIZE == 1024
#define STORAGE_FLASH_NPAGES 8
static const uint32_t storage_flash_sizes[STORAGE_FLASH_NPAGES] = {
    KB(32), KB(32), KB(32), KB(32), KB(128), KB(256), KB(256), KB(256)
};
#else
#error "BOARD_FLASH_SIZE not supported"
#endif

/*
 * Return the physical flash address for a logical page number.
 */
static uint32_t storage_page_addr(uint32_t page)
{
    if (page >= STORAGE_FLASH_NPAGES) {
        return 0;
    }
    uint32_t addr = STM32_FLASH_BASE_ADDR;
    for (uint32_t i = 0; i < page; i++) {
        addr += storage_flash_sizes[i];
    }
    return addr;
}

/* ------------------------------------------------------------------
 * CMSIS flash register helpers
 * These mirror the low-level helpers in Flash.cpp but are kept local
 * so Storage.cpp has a self-contained CMSIS path.
 * --------------------------------------------------------------- */

#ifdef STM32F767xx

static inline int _storage_wait_bsy(uint32_t timeout_loops)
{
    while (FLASH->SR & FLASH_SR_BSY) {
        if (--timeout_loops == 0) return -1;
    }
    return 0;
}

static inline void _storage_clear_errors(void)
{
    FLASH->SR = FLASH_SR_EOP | FLASH_SR_OPERR | FLASH_SR_WRPERR |
                FLASH_SR_PGAERR | FLASH_SR_PGPERR | FLASH_SR_ERSERR;
}

static inline int _storage_flash_unlock(void)
{
    if (FLASH->CR & FLASH_CR_LOCK) {
        FLASH->KEYR = FLASH_KEY1;
        FLASH->KEYR = FLASH_KEY2;
    }
    return (FLASH->CR & FLASH_CR_LOCK) ? -1 : 0;
}

static inline void _storage_flash_lock(void)
{
    FLASH->CR |= FLASH_CR_LOCK;
}

#endif /* STM32F767xx */

namespace RTT
{

void Storage::_publish_backend_debug(void)
{
#if AP_RTT_STORAGE_DEBUG
    rtt_dbg_storage_backend = (uint32_t)_initialisedType;
#if HAL_WITH_RAMTRON
    if (_initialisedType == StorageBackend::FRAM && rtt_dbg_fram_probe == 0) {
        rtt_dbg_fram_probe = 2;
    }
#endif
#endif
}

void Storage::_storage_open(void)
{
    if (_initialisedType != StorageBackend::None) {
        _publish_backend_debug();
        return;
    }

    rtt_dbg_setup_stage = 500;  // _storage_open entered

    _dirty_mask.clearall();

#if HAL_WITH_RAMTRON
    rtt_dbg_setup_stage = 501;  // trying FRAM
    spi_cmsis_prepare_bus(2);
    if (_fram.init()) {
#if AP_RTT_STORAGE_DEBUG
        rtt_dbg_fram_probe = 1;
#endif
        if (_fram.read(0, _buffer, RTT_STORAGE_SIZE)) {
#if AP_RTT_STORAGE_DEBUG
            rtt_dbg_fram_probe = 2;
#endif
            _initialisedType = StorageBackend::FRAM;
            _publish_backend_debug();
            _last_empty_ms = AP_HAL::millis();
            rtt_dbg_setup_stage = 504;  // FRAM ok
            hal.console->printf("Initialised Storage type=%u\n",
                                (unsigned)_initialisedType);
            return;
        }
#if AP_RTT_STORAGE_DEBUG
        rtt_dbg_fram_probe = 4;
#endif
    } else {
#if AP_RTT_STORAGE_DEBUG
        rtt_dbg_fram_probe = 3;
#endif
    }
    rtt_dbg_setup_stage = 5011;  // FRAM init/read failed
#endif

#ifdef STORAGE_FLASH_PAGE
    rtt_dbg_setup_stage = 502;  // trying Flash
    _flash_load();
    if (_initialisedType == StorageBackend::Flash) {
        _publish_backend_debug();
        _last_empty_ms = AP_HAL::millis();
        rtt_dbg_setup_stage = 505;  // Flash ok
        hal.console->printf("Initialised Storage type=%u\n",
                            (unsigned)_initialisedType);
        return;
    }
#endif

    rtt_dbg_setup_stage = 503;  // using stub
    memset(_buffer, 0xFF, RTT_STORAGE_SIZE);
    _initialisedType = StorageBackend::Stub;
    _publish_backend_debug();
    hal.console->printf("Initialised Storage type=%u\n", (unsigned)_initialisedType);
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
#if AP_RTT_STORAGE_DEBUG
    rtt_dbg_storage_last_read_ofs = src;
    rtt_dbg_storage_last_read_len = n;
#endif
    _storage_open();
    memcpy(dst, &_buffer[src], n);
}

void Storage::write_block(uint16_t dst, const void* src, size_t n)
{
    if (src == nullptr || n > RTT_STORAGE_SIZE || dst > (RTT_STORAGE_SIZE - n)) {
        return;
    }
#if AP_RTT_STORAGE_DEBUG
    rtt_dbg_storage_last_write_ofs = dst;
    rtt_dbg_storage_last_write_len = n;
#endif
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
#if AP_RTT_STORAGE_DEBUG
    rtt_dbg_storage_erase_calls++;
#endif
#if HAL_WITH_RAMTRON
    if (_initialisedType == StorageBackend::FRAM) {
        return AP_HAL::Storage::erase();
    }
#endif
#ifdef STORAGE_FLASH_PAGE
    if (_initialisedType == StorageBackend::Flash) {
        return _flash.erase();
    }
#endif
    /* Stub: volatile RAM only */
    memset(_buffer, 0xFF, RTT_STORAGE_SIZE);
    _dirty_mask.clearall();
    return true;
}

void Storage::_timer_tick(void)
{
#if AP_RTT_STORAGE_DEBUG
    rtt_dbg_storage_tick_calls++;
    rtt_dbg_storage_last_tick_ms = AP_HAL::millis();
#endif
    if (_initialisedType == StorageBackend::None || _initialisedType == StorageBackend::Stub) {
        return;
    }
    if (_dirty_mask.empty()) {
        _last_empty_ms = AP_HAL::millis();
#if AP_RTT_STORAGE_DEBUG
        rtt_dbg_storage_empty_ticks++;
        rtt_dbg_storage_last_empty_ms = _last_empty_ms;
        rtt_dbg_storage_dirty_lines = 0;
#endif
        return;
    }

#if AP_RTT_STORAGE_DEBUG
    uint32_t dirty_count = 0;
    for (uint16_t line = 0; line < RTT_STORAGE_NUM_LINES; line++) {
        if (_dirty_mask.get(line)) {
            dirty_count++;
        }
    }
    rtt_dbg_storage_dirty_lines = dirty_count;
#endif

    const uint8_t max_lines =
#if HAL_WITH_RAMTRON
        (_initialisedType == StorageBackend::FRAM) ? 8U :
#endif
        1U;

    for (uint8_t written_lines = 0; written_lines < max_lines; written_lines++) {
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
#if AP_RTT_STORAGE_DEBUG
        rtt_dbg_storage_last_tick_line = i;
#endif

        bool write_ok = false;

#if HAL_WITH_RAMTRON
        if (_initialisedType == StorageBackend::FRAM) {
            /*
             * [Cybernetics Ch.4] Closed-loop: parameter saves often dirty
             * adjacent 8-byte lines (sentinel/data/header).  Flushing a short
             * FRAM dirty run in one scheduler tick reduces the reset window
             * where only part of one logical parameter update is persistent.
             */
            write_ok = _fram.write(RTT_STORAGE_LINE_SIZE * i, _tmpline, RTT_STORAGE_LINE_SIZE);
        }
#endif

#ifdef STORAGE_FLASH_PAGE
        if (_initialisedType == StorageBackend::Flash) {
            write_ok = _flash_write(i);
        }
#endif

        if (write_ok) {
#if HAL_WITH_RAMTRON
            if (_initialisedType == StorageBackend::FRAM) {
#if AP_RTT_STORAGE_DEBUG
                rtt_dbg_fram_tick_ok++;
#endif
            }
#endif
            _sem.take_blocking();
            if (memcmp(_tmpline, &_buffer[RTT_STORAGE_LINE_SIZE * i], RTT_STORAGE_LINE_SIZE) == 0) {
                _dirty_mask.clear(i);
#if AP_RTT_STORAGE_DEBUG
                rtt_dbg_storage_last_dirty_line = i;
                if (_dirty_mask.empty()) {
                    rtt_dbg_storage_dirty_empty_ms = AP_HAL::millis();
                    rtt_dbg_storage_dirty_lines = 0;
                }
#endif
            }
            _sem.give();
        } else {
#if HAL_WITH_RAMTRON
            if (_initialisedType == StorageBackend::FRAM) {
#if AP_RTT_STORAGE_DEBUG
                rtt_dbg_fram_tick_fail++;
#endif
            }
#endif
            break;
        }
    }
}

bool Storage::flush(uint32_t timeout_ms)
{
    const uint32_t start_ms = AP_HAL::millis();

    while (true) {
        _sem.take_blocking();
        const bool empty = _dirty_mask.empty();
        _sem.give();
        if (empty) {
            return true;
        }

        /*
         * [Cybernetics Ch.4] Closed-loop: PARAM_SET must not acknowledge a
         * durable save until the HAL dirty lines are actually written to FRAM.
         */
        _timer_tick();

        if (AP_HAL::millis() - start_ms >= timeout_ms) {
            return false;
        }
        hal.scheduler->delay_microseconds(500);
    }
}

bool Storage::healthy()
{
    if (_initialisedType != StorageBackend::None && !_dirty_mask.empty()) {
        /*
         * [Cybernetics Ch.4] Closed-loop: on RTT the low-priority storage
         * thread can be starved by a healthy 400 Hz main loop.  If arming is
         * checking storage while dirty data is pending, advance one bounded
         * flush step here so health reflects real persistence progress instead
         * of background-thread scheduling luck.
         */
        _timer_tick();
    }
    if (_initialisedType != StorageBackend::None && _dirty_mask.empty()) {
        /*
         * [Cybernetics Ch.4] Closed-loop: RT-Thread can keep the low-priority
         * storage thread off CPU while the 400 Hz main loop is healthy.  If no
         * dirty lines are pending, storage is already durable; refresh the
         * health timestamp here instead of requiring a background tick only to
         * rediscover an empty mask.  Dirty data still relies on _timer_tick()
         * progress and keeps the original timeout semantics.
         */
        _last_empty_ms = AP_HAL::millis();
    }
    const bool ok = ((_initialisedType != StorageBackend::None) &&
                     (AP_HAL::millis() - _last_empty_ms < 2000U));
#if AP_RTT_STORAGE_DEBUG
    rtt_dbg_storage_healthy_state = ok ? 1U : 0U;
    rtt_dbg_storage_last_empty_ms = _last_empty_ms;
#endif
    return ok;
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

/*
  Write data to flash via CMSIS register direct access.
  Uses FLASH_KEYR unlock, FLASH_CR_PG program, and FLASH_SR_BSY polling.
 */
bool Storage::_flash_write_data(uint8_t sector, uint32_t offset,
                                const uint8_t *data, uint16_t length)
{
#ifdef STORAGE_FLASH_PAGE
    uint32_t base_address = storage_page_addr(_flash_page + sector);
    if (base_address == 0) {
        return false;
    }
    uint32_t addr = base_address + offset;

#ifdef STM32F767xx
    for (uint8_t i = 0; i < STORAGE_FLASH_RETRIES; i++) {
        EXPECT_DELAY_MS(1);

        _storage_flash_unlock();
        _storage_clear_errors();

        bool ok = true;
        const uint8_t *b = data;
        uint32_t remaining = length;
        uint32_t wa = addr;

        while (remaining > 0 && ok) {
            if (_storage_wait_bsy(0xFFFFFFU)) { ok = false; break; }

            /* Aligned 32-bit write — preferred path */
            if ((wa & 3) == 0 && remaining >= 4) {
                uint32_t val;
                memcpy(&val, b, 4);
                FLASH->CR = FLASH_CR_PG | FLASH_CR_PSIZE_1;
                rt_base_t level = rt_hw_interrupt_disable();
                *(volatile uint32_t *)wa = val;
                __DSB();
                rt_hw_interrupt_enable(level);
                if (_storage_wait_bsy(0xFFFFFFU)) { ok = false; break; }
                FLASH->CR &= ~FLASH_CR_PG;
                if (*(volatile uint32_t *)wa != val) { ok = false; break; }
                wa += 4; b += 4; remaining -= 4;
            }
            /* Half-word */
            else if ((wa & 1) == 0 && remaining >= 2) {
                uint16_t val;
                memcpy(&val, b, 2);
                FLASH->CR = FLASH_CR_PG | FLASH_CR_PSIZE_0;
                rt_base_t level = rt_hw_interrupt_disable();
                *(volatile uint16_t *)wa = val;
                __DSB();
                rt_hw_interrupt_enable(level);
                if (_storage_wait_bsy(0xFFFFFFU)) { ok = false; break; }
                FLASH->CR &= ~FLASH_CR_PG;
                wa += 2; b += 2; remaining -= 2;
            }
            /* Byte */
            else {
                FLASH->CR = FLASH_CR_PG;
                rt_base_t level = rt_hw_interrupt_disable();
                *(volatile uint8_t *)wa = *b;
                __DSB();
                rt_hw_interrupt_enable(level);
                if (_storage_wait_bsy(0xFFFFFFU)) { ok = false; break; }
                FLASH->CR &= ~FLASH_CR_PG;
                wa++; b++; remaining--;
            }
        }

        if (ok) {
            return true;
        }

        hal.scheduler->delay(1);
    }

    if (_flash_erase_ok()) {
        uint32_t now = AP_HAL::millis();
        if (now - _last_re_init_ms > 5000) {
            _last_re_init_ms = now;
            bool re_ok = _flash.re_initialise();
            ::printf("RTT Storage: failed at %u:%u for %u - re-init %u\n",
                     (unsigned)sector, (unsigned)offset,
                     (unsigned)length, (unsigned)re_ok);
        }
    }
#else
    (void)addr;
#endif /* STM32F767xx */

    return false;
#else
    (void)sector;
    (void)offset;
    (void)data;
    (void)length;
    return false;
#endif /* STORAGE_FLASH_PAGE */
}

/*
  Read data from flash — just a memory-mapped read via the computed
  page address. No CMSIS register access required (reads are always
  available from the flash memory map).
 */
bool Storage::_flash_read_data(uint8_t sector, uint32_t offset,
                               uint8_t *data, uint16_t length)
{
#ifdef STORAGE_FLASH_PAGE
    const uint32_t base_address = storage_page_addr(_flash_page + sector);
    if (base_address == 0) {
        return false;
    }
    memcpy(data, (const uint8_t *)(base_address + offset), length);
    return true;
#else
    (void)sector;
    (void)offset;
    (void)data;
    (void)length;
    return false;
#endif
}

/*
  Erase a flash sector via CMSIS register direct access.
  Uses FLASH_CR_SER + SNB + STRT sequence with BSY polling.
 */
bool Storage::_flash_erase_sector(uint8_t sector)
{
#ifdef STORAGE_FLASH_PAGE
#ifdef STM32F767xx
    for (uint8_t i = 0; i < STORAGE_FLASH_RETRIES; i++) {
        EXPECT_DELAY_MS(1000);

        _storage_flash_unlock();
        if (_storage_wait_bsy(0xFFFFFFU)) {
            continue;
        }
        _storage_clear_errors();

        /* PSIZE=10 (32-bit parallelism, VDD >= 2.7V) */
        uint32_t cr = FLASH_CR_SER |
                      ((uint32_t)(_flash_page + sector) << FLASH_CR_SNB_Pos) |
                      FLASH_CR_PSIZE_1;
        FLASH->CR = cr;

        /* Only disable interrupts for the STRT register write itself.
         * Re-enable immediately so RTT scheduler stays alive during
         * the multi-second erase. */
        rt_base_t level = rt_hw_interrupt_disable();
        FLASH->CR = cr | FLASH_CR_STRT;
        rt_hw_interrupt_enable(level);

        /* Poll BSY, yielding periodically */
        uint32_t yield_counter = 0;
        while (FLASH->SR & FLASH_SR_BSY) {
            if (++yield_counter >= 10000) {
                rt_thread_yield();
                yield_counter = 0;
            }
        }

        FLASH->CR &= ~(FLASH_CR_SER | FLASH_CR_SNB_Msk);
        bool ok = !(FLASH->SR & (FLASH_SR_OPERR | FLASH_SR_WRPERR | FLASH_SR_ERSERR));

        if (ok) {
            return true;
        }

        hal.scheduler->delay(1);
    }
#endif /* STM32F767xx */
    return false;
#else
    (void)sector;
    return false;
#endif /* STORAGE_FLASH_PAGE */
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
