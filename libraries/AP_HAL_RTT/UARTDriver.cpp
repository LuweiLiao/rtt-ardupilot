/*
 * ArduPilot + RT-Thread HAL - UARTDriver (T2-1, T2-2: _begin, wait_timeout, rx_indicate)
 */

#include "UARTDriver.h"
#include <AP_HAL/AP_HAL.h>
#include <AP_Common/ExpandingString.h>
#include <AP_Math/AP_Math.h>
#include <cstring>
#include "hwdef.h"

#ifndef HAL_RTT_SERIAL0_OTG
#define HAL_RTT_SERIAL0_OTG 0
#endif

extern const AP_HAL::HAL &hal;

using namespace RTT;

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

/* Per-port debug tracking */
volatile uint32_t rtt_dbg_uart_port_begin_count[10] = {0};  /* How many times begin() was called per port */
volatile uint32_t rtt_dbg_uart_port_dev_ptr[10] = {0};      /* _dev value after each begin() */
volatile uint8_t rtt_dbg_uart_port_init[10] = {0};          /* _initialized value after each begin() */

volatile int rtt_dbg_uart_begin_called = 0;
volatile int rtt_dbg_uart_begin_port = -1;
volatile int rtt_dbg_uart_begin_result = 0;
volatile uint32_t rtt_dbg_uart_begin_dev = 0;

void UARTDriver::_begin(uint32_t baud, uint16_t rxSpace, uint16_t txSpace)
{
    if (_port_num < 10) {
        rtt_dbg_uart_port_begin_count[_port_num]++;
    }

    rtt_dbg_uart_begin_called++;
    rtt_dbg_uart_begin_port = _port_num;

    if (baud == 0 && rxSpace == 0 && txSpace == 0 && _initialized) {
        rtt_dbg_uart_begin_result = 1;
        return;
    }

    if (_port_num >= RTT_UART_MAX_DRIVERS ||
        _port_num >= ARRAY_SIZE(_device_names)) {
        rtt_dbg_uart_begin_result = 2;
        return;
    }

    const char *name = _device_names[_port_num];
    rt_device_t dev = rt_device_find(name);
    if (dev == nullptr) {
        for (int retry = 0; retry < 10 && dev == nullptr; retry++) {
            rt_thread_mdelay(200);
            dev = rt_device_find(name);
        }
    }
    rtt_dbg_uart_begin_dev = (uint32_t)(uintptr_t)dev;
    if (_port_num < 10) {
        rtt_dbg_uart_port_dev_ptr[_port_num] = (uint32_t)(uintptr_t)dev;
    }
    if (dev == nullptr) {
        rtt_dbg_uart_begin_result = 3;
        _deferred_open = true;
        _baudrate = baud;
        uint16_t rxS = rxSpace < 512 ? 512 : rxSpace;
        uint16_t txS = txSpace < 512 ? 512 : txSpace;
        if (_readbuf.get_size() == 0) { _readbuf.set_size(rxS); }
        if (_writebuf.get_size() == 0) { _writebuf.set_size(txS); }
        return;
    }

    /* RDWR + INT_RX 与延期打开路径一致；部分 CDC 字符设备仅 INT_RX 时写路径异常 */
    rt_err_t err = rt_device_open(dev, RT_DEVICE_FLAG_RDWR | RT_DEVICE_FLAG_INT_RX);
    if (err != RT_EOK) {
        err = rt_device_open(dev, RT_DEVICE_FLAG_RDWR | RT_DEVICE_FLAG_DMA_RX);
    }
    if (err != RT_EOK) {
        return;
    }

    _dev = dev;
    _baudrate = baud;
#if defined(RT_DEVICE_CTRL_SET_BAUD_RATE)
    if (_baudrate != 0) {
        rt_device_control(_dev, RT_DEVICE_CTRL_SET_BAUD_RATE, &_baudrate);
    }
#endif

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

    _initialized = true;

    /* Track per-port state for debugging */
    if (_port_num < 10) {
        rtt_dbg_uart_port_dev_ptr[_port_num] = (uint32_t)(uintptr_t)_dev;
        rtt_dbg_uart_port_init[_port_num] = _initialized ? 1 : 0;
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

void UARTDriver::_drain_writebuf_to_dev()
{
    if (_dev == nullptr) {
        return;
    }
    /*
     * 同一 timer tick 内尽量多刷几包，避免发送环里积压时仍按「每 tick 一包」限流。
     * USB CDC 的 rt_device_write 会阻塞到 IN 完成，chunk 数不宜过大以免饿死同线程其它串口。
     */
    const unsigned max_chunks = 8;
    for (unsigned c = 0; c < max_chunks; c++) {
        uint32_t n = _writebuf.peekbytes(_tx_bounce, sizeof(_tx_bounce));
        if (n == 0) {
            break;
        }
        rt_size_t w = rt_device_write(_dev, 0, _tx_bounce, n);
        if (w == 0) {
            break;
        }
        _writebuf.advance(w);
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
    return _writebuf.space();
}

void UARTDriver::_timer_tick(void)
{
    if (!_initialized) {
        if (_deferred_open && _port_num < ARRAY_SIZE(_device_names)) {
            rt_device_t dev = rt_device_find(_device_names[_port_num]);
            if (dev != nullptr) {
                rt_err_t err = rt_device_open(dev, RT_DEVICE_FLAG_RDWR | RT_DEVICE_FLAG_INT_RX);
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
                    if (_readbuf.get_size() == 0) { _readbuf.set_size(512); }
                    if (_writebuf.get_size() == 0) { _writebuf.set_size(512); }
                    _initialized = true;
                    _deferred_open = false;
                }
            }
        }
        return;
    }
    _drain_rx_to_readbuf();
    _drain_writebuf_to_dev();
}

void UARTDriver::set_flow_control(enum flow_control flow)
{
    _flow_control = flow;
}

#if HAL_UART_STATS_ENABLED
void UARTDriver::uart_info(ExpandingString &str, StatsTracker &stats, const uint32_t dt_ms)
{
    (void)stats;
    (void)dt_ms;
    str.printf("RTT_UART%u baud=%lu\n", (unsigned)_port_num, (unsigned long)_baudrate);
}
#endif
