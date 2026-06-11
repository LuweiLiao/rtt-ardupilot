/*
 * AP_HAL_RTT — UART driver
 * Supports USB CDC + hardware UARTs via RT-Thread device framework.
 */

#pragma once

#include <AP_HAL/UARTDriver.h>
#include <AP_HAL/utility/RingBuffer.h>
#include "HAL_RTT_Namespace.h"

#include <rtthread.h>

#if defined(SOC_SERIES_STM32F7)
#include <stm32f7xx.h>
#endif

#define RTT_UART_MAX_DRIVERS 10
#define RTT_UART_RX_BOUNCE_SIZE 512
#define RTT_UART_TX_BOUNCE_SIZE 512
#define RTT_UART_RX_DMA_BUF_SIZE 2048
#define RTT_UART_USB_RX_BUF_SIZE 2048
#define RTT_UART_USB_TX_BUF_SIZE 2048

namespace RTT
{

class UARTDriver : public AP_HAL::UARTDriver
{
public:
    UARTDriver(uint8_t port_num);

    CLASS_NO_COPY(UARTDriver);

    bool is_initialized() override;
    bool tx_pending() override;
    uint32_t txspace() override;
    uint32_t get_baud_rate() const override { return _baudrate; }
    uint32_t bw_in_bytes_per_second() const override;
    uint32_t get_usb_baud() const override;
    uint8_t get_usb_parity() const override;

    void disable_rxtx(void) const override;
    bool set_options(uint16_t options) override;
    uint16_t get_options(void) const override;
    bool set_unbuffered_writes(bool on) override;
    void configure_parity(uint8_t v) override;
    void set_stop_bits(int n) override;
    bool set_RTS_pin(bool high) override;
    bool set_CTS_pin(bool high) override;
    uint64_t receive_time_constraint_us(uint16_t nbytes) override;

    bool wait_timeout(uint16_t n, uint32_t timeout_ms) override;

    /* Assign port number at runtime.  Needed because GCC --gc-sections may
     * skip the C++ static constructors that normally set _port_num. */
    void set_port_num(uint8_t n) {
        _port_num = n;
        if (n < RTT_UART_MAX_DRIVERS) { _drivers[n] = this; }
    }

    void _timer_tick(void) override;

    void set_flow_control(enum flow_control flow) override;
    enum flow_control get_flow_control() override { return _flow_control; }

    // Bridge for USB CDC bulk_out to write directly into readbuf
    void usb_rx_bridge(const uint8_t *data, size_t len);

#if HAL_UART_STATS_ENABLED
    void uart_info(ExpandingString &str, StatsTracker &stats, const uint32_t dt_ms) override;
    uint32_t get_total_tx_bytes() const override { return _tx_stats_bytes; }
    uint32_t get_total_rx_bytes() const override { return _rx_stats_bytes; }
#endif

protected:
    void _begin(uint32_t baud, uint16_t rxSpace, uint16_t txSpace) override;
    void _end() override;
    void _flush() override;
    uint32_t _available() override;
    ssize_t _read(uint8_t *buffer, uint16_t count) override WARN_IF_UNUSED;
    size_t _write(const uint8_t *buffer, size_t size) override;
    bool _discard_input() override;

private:
    uint8_t _port_num;
    rt_device_t _dev;
    rt_sem_t _rx_sem;
    uint32_t _baudrate;
    bool _initialized;
    bool _deferred_open;
    bool _is_usb{false};
    uint8_t _usb_index{0};
    uint8_t _usb_in_ep{1};

    bool _unbuffered_writes{false};
    uint16_t _usb_write_fail_count{0};
    enum flow_control _flow_control = FLOW_CONTROL_DISABLE;
    uint16_t _last_options = 0;
    uint32_t _tx_stats_bytes = 0;
    uint32_t _rx_stats_bytes = 0;

#if defined(SOC_SERIES_STM32F7)
    /* Direct UART register base for CMSIS register-level access.
     * NULL for USB ports. */
    USART_TypeDef *_uart_hw{nullptr};
#endif

    bool _last_drain_wrote{true};
    bool _tx_drain_active{false};
    bool _check_usb_connected() const;

    ByteBuffer _readbuf{0};
    ByteBuffer _writebuf{0};
    uint8_t _rx_bounce[RTT_UART_RX_BOUNCE_SIZE];
    uint8_t _tx_bounce[RTT_UART_TX_BOUNCE_SIZE];

    /* DMA state tracking */
    bool _tx_dma_active{false};
    bool _rx_dma_active{false};
#if defined(SOC_SERIES_STM32F7)
    DMA_Stream_TypeDef *_rx_dma_stream{nullptr};
#endif
    uint8_t *_rx_dma_buf{nullptr};
    uint16_t _rx_dma_buf_size{0};
    uint16_t _rx_dma_tail{0};

    void _drain_rx_to_readbuf();
    void _drain_writebuf_to_dev();
#if defined(SOC_SERIES_STM32F7)
    bool _start_iomcu_rx_dma();
    void _stop_rx_dma();
    void _drain_rx_dma_to_readbuf();
#endif

    static UARTDriver *_drivers[RTT_UART_MAX_DRIVERS];
};

} // namespace RTT
