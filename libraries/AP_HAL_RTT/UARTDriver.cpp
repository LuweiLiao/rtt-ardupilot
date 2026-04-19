/*
 * ArduPilot + RT-Thread HAL - UARTDriver (T2-1, T2-2: _begin, wait_timeout, rx_indicate)
 */

#include "UARTDriver.h"
#include <AP_HAL/AP_HAL.h>
#include <AP_Common/ExpandingString.h>
#include <AP_Math/AP_Math.h>
#include <cstring>
#include "hwdef.h"

extern "C" bool usb_device_is_configured(uint8_t busid);
extern "C" bool usb_cdc_dtr_active(void);

/* USB debug counters from DWC2 driver and usbd_serial (non-invasive monitoring) */
extern "C" {
extern volatile uint32_t dbg_iepint_calls;
extern volatile uint32_t dbg_iepint_ep1_xfrc;
extern volatile uint32_t dbg_txfe_ep1_calls;
extern volatile uint32_t dbg_txfe_ep1_wrote;
extern volatile uint32_t dbg_ep_busy_cnt;
extern volatile uint32_t dbg_ep_recover_cnt;
extern volatile uint32_t dbg_serial_bulkin_cnt;
extern volatile uint32_t dbg_serial_tx_kick;
extern volatile uint32_t dbg_serial_write_calls;
}

#ifndef HAL_RTT_SERIAL0_OTG
#define HAL_RTT_SERIAL0_OTG 0
#endif

extern const AP_HAL::HAL &hal;

using namespace RTT;

#if defined(SOC_SERIES_STM32F7)
/*
 * Direct register-level UART TX for STM32F7.
 * Bypasses RTT serial V1 TX completion mechanism which deadlocks:
 * _serial_int_tx() blocks on rt_completion_wait() when stm32_putc() returns -1,
 * but the TX-done ISR may never fire, permanently blocking the ap_uart thread.
 */
struct uart_hw { volatile uint32_t cr1; volatile uint32_t _r1[6]; volatile uint32_t isr; volatile uint32_t _r2[2]; volatile uint32_t tdr; };

static uart_hw *uart_from_name(const char *name)
{
    int n = 0;
    if (name && (name[0] == 'u' || name[0] == 'U')) {
        const char *p = name;
        while (*p && (*p < '0' || *p > '9')) p++;
        if (*p) n = *p - '0';
    }
    switch (n) {
    case 1: return (uart_hw*)0x40011000;
    case 2: return (uart_hw*)0x40004400;
    case 3: return (uart_hw*)0x40004800;
    case 4: return (uart_hw*)0x40004C00;
    case 5: return (uart_hw*)0x40005000;
    case 6: return (uart_hw*)0x40011400;
    case 7: return (uart_hw*)0x40007800;
    case 8: return (uart_hw*)0x40007C00;
    default: return nullptr;
    }
}

static bool uart_poll_tx(volatile uint32_t *isr, volatile uint32_t *tdr, const uint8_t *buf, uint32_t len)
{
    for (uint32_t i = 0; i < len; i++) {
        uint32_t timeout = 50000;
        while (!(*isr & (1U << 7)) && --timeout) { asm volatile("nop"); }
        if (timeout == 0) return false;
        *((volatile uint8_t*)tdr) = buf[i];
    }
    uint32_t timeout = 50000;
    while (!(*isr & (1U << 6)) && --timeout) { asm volatile("nop"); }
    return true;
}
#endif /* SOC_SERIES_STM32F7 */

/* 板级设备名表：未定义 HAL_RTT_UART_DEVICE_LIST 时使用占位 uart1, uart2, ... */
#ifndef HAL_RTT_UART_DEVICE_LIST
#define HAL_RTT_UART_DEVICE_LIST \
    "uart1", "uart2", "uart3", "uart4", "uart5", "uart6", "uart7", "uart8"
#endif

static const char *const _device_names[] = { HAL_RTT_UART_DEVICE_LIST };

UARTDriver *UARTDriver::_drivers[RTT_UART_MAX_DRIVERS] = {};

uint32_t UARTDriver::bw_in_bytes_per_second() const
{
    /* Match AP_HAL_ChibiOS: USB CDC is ~FS bulk; UART uses nominal byte rate/10 like ChibiOS */
#if HAL_RTT_SERIAL0_OTG
    /* hwdef: SERIAL_ORDER starts with OTG — GCS is on serial0; must not fall back to 5760 default */
    if (_port_num == 0) {
        return 200U * 1024U;
    }
#endif
    if (_port_num < ARRAY_SIZE(_device_names)) {
        const char *name = _device_names[_port_num];
        if (name != nullptr && std::strncmp(name, "usb", 3) == 0) {
            return 200U * 1024U;
        }
    }
    if (_baudrate > 0) {
        return _baudrate / 10U;
    }
    return 5760U;
}

UARTDriver::UARTDriver(uint8_t port_num)
    : AP_HAL::UARTDriver()
    , _port_num(port_num)
    , _dev(nullptr)
    , _rx_sem(nullptr)
    , _baudrate(0)
    , _initialized(false)
{
    if (_port_num < RTT_UART_MAX_DRIVERS) {
        _drivers[_port_num] = this;
    }
}

rt_err_t UARTDriver::_rx_indicate_cb(rt_device_t dev, rt_size_t size)
{
    (void)size;
    for (uint8_t i = 0; i < RTT_UART_MAX_DRIVERS; i++) {
        if (_drivers[i] != nullptr && _drivers[i]->_dev == dev && _drivers[i]->_rx_sem != nullptr) {
            rt_sem_release(_drivers[i]->_rx_sem);
            break;
        }
    }
    return RT_EOK;
}

void UARTDriver::_begin(uint32_t baud, uint16_t rxSpace, uint16_t txSpace)
{
    if (baud == 0 && rxSpace == 0 && txSpace == 0 && _initialized) {
        return;
    }

    /* Register this driver in the static array if not already done.
     * GCC with -ffunction-sections -gc-sections may skip static constructors
     * for objects it considers "trivially initialisable", so _port_num is set
     * by the declaration argument but _drivers[] was never populated. */
    if (_port_num < RTT_UART_MAX_DRIVERS && _drivers[_port_num] != this) {
        _drivers[_port_num] = this;
    }

    /* If already initialized with device open, just change baud rate */
    if (_initialized && _dev != nullptr && baud != 0) {
        _baudrate = baud;
        rt_device_control(_dev, 0x1000, &_baudrate);
        return;
    }

    if (_port_num >= RTT_UART_MAX_DRIVERS ||
        _port_num >= ARRAY_SIZE(_device_names)) {
        return;
    }

    const char *name = _device_names[_port_num];
    // rt_kprintf("[UART%u] _begin: looking for device '%s'\n", (unsigned)_port_num, name);
    rt_device_t dev = rt_device_find(name);
    /* No blocking retry — defer to _timer_tick instead.  The old 10×200ms
     * retry loop could block for 2 s per port; with 8 ports the cumulative
     * 10+ s stall triggered main_loop_stuck (Internal Error 0x8000). */
    if (dev == nullptr) {
        // rt_kprintf("[UART%u] device '%s' NOT found, deferring open\n", (unsigned)_port_num, name);
        _deferred_open = true;
        _baudrate = baud;
        uint16_t rxS = rxSpace < 512 ? 512 : rxSpace;
        uint16_t txS = txSpace < 512 ? 512 : txSpace;
        if (_readbuf.get_size() == 0) { _readbuf.set_size(rxS); }
        if (_writebuf.get_size() == 0) { _writebuf.set_size(txS); }
        return;
    }
    // rt_kprintf("[UART%u] device '%s' found at %p\n", (unsigned)_port_num, name, dev);

    /* USB CDC 端口扩大缓冲，8192 字节足以容纳多个 MAVLink LOG_DATA 包，
     * 避免高速日志下载时 txspace 频繁归零导致 HAVE_PAYLOAD_SPACE 拒绝发送. */
    const bool is_usb = (std::strncmp(name, "usb", 3) == 0);
    if (is_usb) {
        if (txSpace < 8192) { txSpace = 8192; }
        if (rxSpace < 2048) { rxSpace = 2048; }
    }

    /* RDWR + INT_RX 与延期打开路径一致；部分 CDC 字符设备仅 INT_RX 时写路径异常 */
    /* Try DMA TX first (non-blocking, truly async). Falls back to
     * INT_RX-only open for ports without DMA config (e.g. USB CDC). */
    rt_err_t err = rt_device_open(dev, RT_DEVICE_FLAG_RDWR | RT_DEVICE_FLAG_INT_RX | RT_DEVICE_FLAG_DMA_TX);
    if (err != RT_EOK) {
        err = rt_device_open(dev, RT_DEVICE_FLAG_RDWR | RT_DEVICE_FLAG_INT_RX);
    }
    if (err != RT_EOK) {
        rt_kprintf("[UART%u] INT_RX open failed (%d), trying DMA_RX\n", (unsigned)_port_num, (int)err);
        err = rt_device_open(dev, RT_DEVICE_FLAG_RDWR | RT_DEVICE_FLAG_DMA_RX);
    }
    // rt_kprintf("[UART%u] open result=%d, is_usb=%d\n", (unsigned)_port_num, (int)err, is_usb);
    if (err != RT_EOK) {
        return;
    }

    _dev = dev;
    _baudrate = baud;

#if defined(SOC_SERIES_STM32F7)
    /* Resolve UART hardware base for direct register-level TX polling.
     * This bypasses the RTT serial V1 TX completion mechanism which can
     * deadlock the ap_uart thread. */
    _uart_hw = uart_from_name(name);
#endif

    if (_baudrate != 0) {
        /* Skip baud rate change for the RTT console UART — it's already
         * configured by rt_hw_board_init and shared with rt_kprintf.
         * Reconfiguring it would break the console output. */
        rt_device_t console_dev = rt_console_get_device();
        bool is_console = (console_dev && dev == console_dev);
        if (!is_console) {
            rt_device_control(_dev, 0x1000, &_baudrate);
        } else {
            _baudrate = 115200;  // console is always 115200
        }
    }

    /* 每个端口独立 rx_sem */
    if (_rx_sem == nullptr) {
        char sem_name[RT_NAME_MAX];
        rt_snprintf(sem_name, sizeof(sem_name), "urx%u", (unsigned)_port_num);
        _rx_sem = rt_sem_create(sem_name, 0, RT_IPC_FLAG_FIFO);
        if (_rx_sem == nullptr) {
            rt_device_close(_dev);
            _dev = nullptr;
            return;
        }
    }

    rt_device_set_rx_indicate(_dev, _rx_indicate_cb);

    uint16_t rxS = rxSpace < 512 ? 512 : rxSpace;
    uint16_t txS = txSpace < 512 ? 512 : txSpace;
    if (_readbuf.get_size() != rxS) {
        _readbuf.set_size(rxS);
    }
    if (_writebuf.get_size() != txS) {
        _writebuf.set_size(txS);
    }

    _is_usb = is_usb;

    if (is_usb) {
        _flow_control = FLOW_CONTROL_ENABLE;
    }

    _initialized = true;

    /* If this port is the RTT console device, disable console output so
     * rt_kprintf text doesn't interleave with MAVLink binary frames. */
    if (!is_usb) {
        rt_device_t console_dev = rt_console_get_device();
        if (console_dev && dev == console_dev) {
            rt_console_output_set_enabled(RT_FALSE);
        }
    }
}

void UARTDriver::_end()
{
    if (!_initialized) {
        return;
    }
    _initialized = false;
    if (_dev != nullptr) {
        rt_device_set_rx_indicate(_dev, nullptr);
        rt_device_close(_dev);
        _dev = nullptr;
    }
    if (_rx_sem != nullptr) {
        rt_sem_delete(_rx_sem);
        _rx_sem = nullptr;
    }
    _readbuf.set_size(0);
    _writebuf.set_size(0);
    if (_port_num < RTT_UART_MAX_DRIVERS) {
        _drivers[_port_num] = nullptr;
    }
}

void UARTDriver::_flush()
{
    _drain_writebuf_to_dev();
}

uint32_t UARTDriver::_available()
{
    if (!_initialized) {
        return 0;
    }
    // Drain any pending hardware RX into the ring buffer so callers
    // (e.g. IOMCU thread) see up-to-date data without waiting for
    // the next timer tick.
    _drain_rx_to_readbuf();
    return _readbuf.available();
}

void UARTDriver::_drain_rx_to_readbuf()
{
    if (_dev == nullptr) {
        return;
    }
    rt_size_t n = rt_device_read(_dev, 0, _rx_bounce, sizeof(_rx_bounce));
    if (n > 0) {
        _readbuf.write(_rx_bounce, n);
    }
}

/* Debug counters for UART drain path — read via GDB */
volatile uint32_t rtt_uart_dbg_drain_calls = 0;
volatile uint32_t rtt_uart_dbg_drain_writes = 0;
volatile uint32_t rtt_uart_dbg_drain_zero = 0;
volatile uint32_t rtt_uart_dbg_drain_bytes = 0;

void UARTDriver::_drain_writebuf_to_dev()
{
    if (_dev == nullptr) {
        return;
    }

    /*
     * Drain as much as possible from _writebuf into the device.
     *
     * For UART with DMA TX we limit to one write per tick to avoid
     * bounce-buffer races (DMA stores the pointer, not the data).
     *
     * For USB CDC (and other non-DMA backends) the single-write limit
     * throttles throughput to 512 B/tick = 512 KB/s.  The CherryUSB
     * CDC ringbuffer is only 4096 B and USB-FS max-packet is 64 B, so
     * we need to keep it fed aggressively.  Loop until the device can't
     * accept more or our buffer is empty.
     */
    const bool is_usb = (_port_num == 0);  /* serial0 = USB ACM */

    for (;;) {
        uint32_t n = _writebuf.peekbytes(_tx_bounce, sizeof(_tx_bounce));
        if (n == 0) {
            _last_drain_wrote = true;
            return;
        }

        rt_size_t w = rt_device_write(_dev, 0, _tx_bounce, n);
        rtt_uart_dbg_drain_calls++;
        if (w > 0) {
            rtt_uart_dbg_drain_writes++;
            rtt_uart_dbg_drain_bytes += w;
            _writebuf.advance(w);
            _last_drain_wrote = true;
            if (is_usb) {
                continue;   /* keep feeding the USB ringbuffer */
            }
            /* UART DMA: one write per tick */
            return;
        } else {
            rtt_uart_dbg_drain_zero++;
            _last_drain_wrote = false;
            return;  /* device buffer full – retry next tick */
        }
    }
}

bool UARTDriver::wait_timeout(uint16_t n, uint32_t timeout_ms)
{
    if (!_initialized || _rx_sem == nullptr) {
        return false;
    }
    rt_tick_t tick = rt_tick_from_millisecond(timeout_ms);
    uint32_t t0 = AP_HAL::millis();
    while (_readbuf.available() < n) {
        _drain_rx_to_readbuf();
        if (_readbuf.available() >= n) {
            return true;
        }
        rt_err_t ret = rt_sem_take(_rx_sem, tick);
        if (ret == RT_EOK) {
            _drain_rx_to_readbuf();
        } else {
            /* 超时：按 AP_HAL 语义返回 false */
            return false;
        }
        uint32_t elapsed = AP_HAL::millis() - t0;
        if (elapsed >= timeout_ms) {
            return false;
        }
        tick = rt_tick_from_millisecond(timeout_ms - elapsed);
    }
    return true;
}

ssize_t UARTDriver::_read(uint8_t *buffer, uint16_t count)
{
    if (!_initialized) {
        return -1;
    }
    _drain_rx_to_readbuf();
    uint32_t n = _readbuf.read(buffer, count);
    return n > 0 ? (ssize_t)n : 0;
}

size_t UARTDriver::_write(const uint8_t *buffer, size_t size)
{
    if (!_initialized) {
        return 0;
    }
    if (_unbuffered_writes && !_is_usb && _dev != nullptr) {
        // For IOMCU and other unbuffered UART users: write directly to
        // hardware, bypassing the ring buffer and timer-tick drain path.
        // This ensures data is sent immediately rather than waiting for
        // the next 1kHz timer tick in the uart thread.
        return rt_device_write(_dev, 0, buffer, size);
    }
    return _writebuf.write(buffer, size);
}

bool UARTDriver::_discard_input()
{
    if (!_initialized) {
        return false;
    }
    _readbuf.clear();
    return true;
}

bool UARTDriver::is_initialized()
{
    return _initialized;
}

bool UARTDriver::tx_pending()
{
    return _writebuf.available() > 0;
}

uint32_t UARTDriver::txspace()
{
    if (!_initialized) {
        return 0;
    }
    if (_writebuf.available() > 0) {
        _drain_writebuf_to_dev();
    }
    return _writebuf.space();
}

bool UARTDriver::_check_usb_connected() const
{
    return usb_device_is_configured(0);
}

volatile uint32_t rtt_uart_dbg_tick_calls = 0;
/* Per-port tick counter — identifies which port's _timer_tick crashes the thread */
volatile uint32_t rtt_uart_dbg_port_ticks[10] = {};
volatile uint32_t rtt_uart_dbg_crash_port = 0xFFFFFFFF;  /* set to port_num on crash entry */

void UARTDriver::_timer_tick(void)
{
    rtt_uart_dbg_tick_calls++;
    rtt_uart_dbg_crash_port = _port_num;
    rtt_uart_dbg_port_ticks[_port_num < 10 ? _port_num : 0]++;
    if (!_initialized) {
        if (_deferred_open && _port_num < ARRAY_SIZE(_device_names)) {
            const char *name = _device_names[_port_num];
            rt_device_t dev = rt_device_find(name);
            if (dev != nullptr) {
                rt_err_t err = rt_device_open(dev, RT_DEVICE_FLAG_RDWR | RT_DEVICE_FLAG_INT_RX | RT_DEVICE_FLAG_DMA_TX);
                if (err != RT_EOK) {
                    err = rt_device_open(dev, RT_DEVICE_FLAG_RDWR | RT_DEVICE_FLAG_INT_RX);
                }
                if (err == RT_EOK) {
                    _dev = dev;
                    if (_rx_sem == nullptr) {
                        char sem_name[RT_NAME_MAX];
                        rt_snprintf(sem_name, sizeof(sem_name), "urx%u", (unsigned)_port_num);
                        _rx_sem = rt_sem_create(sem_name, 0, RT_IPC_FLAG_FIFO);
                    }
                    if (_rx_sem) {
                        rt_device_set_rx_indicate(_dev, _rx_indicate_cb);
                    }
                    const bool is_usb = (strncmp(name, "usb", 3) == 0);
                    _is_usb = is_usb;
                    uint16_t rxS = 512;
                    uint16_t txS = 512;
                    if (is_usb) {
                        rxS = 2048;
                        txS = 8192;
                        _flow_control = FLOW_CONTROL_ENABLE;
                    }
                    if (_readbuf.get_size() < rxS) { _readbuf.set_size(rxS); }
                    if (_writebuf.get_size() < txS) { _writebuf.set_size(txS); }
                    _initialized = true;
                    _deferred_open = false;
                }
            }
        }
        return;
    }

    if (_is_usb && !_check_usb_connected()) {
        /* Don't aggressively clear buffers — the USB configured check may
         * briefly return false during normal operation (e.g. USB bus reset),
         * causing all queued MAVLink data to be dropped. Instead, just skip
         * draining and let the write-fail counter handle true disconnections. */
        return;
    }

    _drain_rx_to_readbuf();
    _drain_writebuf_to_dev();

    if (_is_usb) {
        /* Track consecutive write failures: drain returned 0 bytes while data
         * was queued.  Only clear the write buffer if the USB endpoint is truly
         * stuck (no progress for 5 seconds at 1 kHz tick = 5000 ticks). */
        static uint32_t _diag_last_ms = 0;
        static uint32_t _diag_clears = 0;
        if (_writebuf.available() == 0) {
            _usb_write_fail_count = 0;
        } else if (!_last_drain_wrote) {
            /* drain was attempted but wrote 0 bytes → endpoint full/stuck */
            _usb_write_fail_count++;
            if (_usb_write_fail_count > 500) {
                _writebuf.clear();
                _usb_write_fail_count = 0;
                _diag_clears++;
            }
        } else {
            _usb_write_fail_count = 0;  /* write succeeded */
        }
        // Diagnostic: every 5s print USB write stats + DWC2 debug counters
        if (AP_HAL::millis() - _diag_last_ms > 5000) {
            // rt_kprintf("[USB%d] wb=%u fail=%u clr=%u iep=%u xfrc=%u txfe=%u/%u kick=%u bin=%u w=%u busy=%u rec=%u\n",
            //            (unsigned)_port_num,
            //            (unsigned)_writebuf.available(),
            //            (unsigned)_usb_write_fail_count,
            //            (unsigned)_diag_clears,
            //            (unsigned)dbg_iepint_calls,
            //            (unsigned)dbg_iepint_ep1_xfrc,
            //            (unsigned)dbg_txfe_ep1_calls,
            //            (unsigned)dbg_txfe_ep1_wrote,
            //            (unsigned)dbg_serial_tx_kick,
            //            (unsigned)dbg_serial_bulkin_cnt,
            //            (unsigned)dbg_serial_write_calls,
            //            (unsigned)dbg_ep_busy_cnt,
            //            (unsigned)dbg_ep_recover_cnt);
            _diag_clears = 0;
            _diag_last_ms = AP_HAL::millis();
        }
    } else {
        _usb_write_fail_count = 0;
    }
}

void UARTDriver::set_flow_control(enum flow_control flow)
{
    _flow_control = flow;
}

#if HAL_UART_STATS_ENABLED
void UARTDriver::uart_info(ExpandingString &str, StatsTracker &stats, const uint32_t dt_ms)
{
    const uint32_t tx_bytes = stats.tx.update(_tx_stats_bytes);
    const uint32_t rx_bytes = stats.rx.update(_rx_stats_bytes);

    if (_is_usb) {
        str.printf("CDC%u  ", (unsigned)_port_num);
    } else {
        str.printf("UART%u ", (unsigned)_port_num);
    }

    str.printf("TX =%8u RX =%8u TXBD=%6u RXBD=%6u FlowCtrl=%u\n",
               (unsigned)tx_bytes,
               (unsigned)rx_bytes,
               dt_ms ? (unsigned)((tx_bytes * 10000) / dt_ms) : 0u,
               dt_ms ? (unsigned)((rx_bytes * 10000) / dt_ms) : 0u,
               (unsigned)_flow_control);
}
#endif

uint32_t UARTDriver::get_usb_baud() const
{
    if (_is_usb) {
        return 921600;
    }
    return 0;
}

uint8_t UARTDriver::get_usb_parity() const
{
    return 0;
}

void UARTDriver::disable_rxtx(void) const
{
}

bool UARTDriver::set_options(uint16_t options)
{
    _last_options = options;
    return true;
}

uint16_t UARTDriver::get_options(void) const
{
    return _last_options;
}

bool UARTDriver::set_unbuffered_writes(bool on)
{
    _unbuffered_writes = on;
    return true;
}

void UARTDriver::configure_parity(uint8_t v)
{
    (void)v;
}

void UARTDriver::set_stop_bits(int n)
{
    (void)n;
}

bool UARTDriver::set_RTS_pin(bool high)
{
    (void)high;
    return false;
}

bool UARTDriver::set_CTS_pin(bool high)
{
    (void)high;
    return false;
}

uint64_t UARTDriver::receive_time_constraint_us(uint16_t nbytes)
{
    uint64_t last_receive_us = AP_HAL::micros64();
    if (_baudrate > 0) {
        last_receive_us += ((uint64_t)nbytes * 1000000ULL * 10) / _baudrate;
    }
    return last_receive_us;
}
