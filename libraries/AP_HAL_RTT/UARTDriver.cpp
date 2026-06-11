/*
 * ArduPilot + RT-Thread HAL - UARTDriver (T2-1, T2-2: _begin, wait_timeout, rx_indicate)
 *
 * Boundary vs ChibiOS UARTDriver.cpp:
 *   - USART DMA: per-instance tables in hwdef; use RTT::Shared_DMA when sharing streams.
 *   - SERIAL0 USB: native (hal_usb_lld_rtt) or CherryUSB (hal_usb_cherryusb_shim.c),
 *     selected by RTT_USB_BACKEND — not ChibiOS USB stack.
 * ChibiOS reference: libraries/AP_HAL_ChibiOS/UARTDriver.cpp
 */

#include "UARTDriver.h"
#include <AP_HAL/AP_HAL.h>
#include <AP_Common/ExpandingString.h>
#include <AP_Math/AP_Math.h>
#include <cstring>
#include "hwdef.h"

#if defined(SOC_SERIES_STM32F7)
#include <stm32f7xx.h>
extern "C" {
#include "drv_usart_ll.h"
}
#endif

#include "hal_usb_lld_rtt.h"

#if CONFIG_HAL_BOARD == HAL_BOARD_RTT
#define RTT_DBG_DTCM_BSS __attribute__((section(".dtcm_bss.rtt_dbg"), used))
extern volatile uint32_t rtt_dbg_mav_send_lock_depth_total __attribute__((weak));

static bool rtt_uart_mavlink_send_locked()
{
    return (&rtt_dbg_mav_send_lock_depth_total != nullptr) &&
           (rtt_dbg_mav_send_lock_depth_total > 0U);
}
#endif

/* USB debug counters from DWC2 driver (non-invasive monitoring) */
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

#if defined(SOC_SERIES_STM32F7)
/*
 * Baud-rate register calculation helper for STM32F7.
 * Returns the BRR value to achieve the desired baud rate given the
 * peripheral clock.  Uses 16x oversampling (OVER8=0).
 *
 * For 16x oversampling:
 *   USARTDIV = pclk / (16 * baud)
 *   BRR[15:4] = DIV_Mantissa (integer part of USARTDIV)
 *   BRR[3:0]  = DIV_Fraction (0-15, fractional part rounded to nearest 1/16)
 */
static uint32_t uart_brr_value(uint32_t pclk, uint32_t baud)
{
    uint32_t div = pclk / (16U * baud);       /* integer part */
    uint32_t rem = pclk % (16U * baud);       /* remainder */
    /* round fraction to nearest 1/16 */
    uint32_t frac = (rem * 16U + (8U * baud)) / (16U * baud);
    if (frac >= 16U) {
        div++;
        frac = 0;
    }
    return (div << 4U) | frac;
}

/*
 * Return the APB peripheral clock (Hz) for a given USART base address.
 * STM32F767: APB1 = 54 MHz, APB2 = 108 MHz (typical CUAV V5 config).
 * USART1, USART6 on APB2; all others on APB1.
 */
static uint32_t uart_pclk(USART_TypeDef *usart)
{
    (void)usart;
    /* On F767 with typical 216 MHz SYSCLK:
     *   PPRE1 = /4  → APB1 =  54 MHz
     *   PPRE2 = /2  → APB2 = 108 MHz
     * USART1,6 are on APB2; USART2,3 and UART4-8 are on APB1.
     */
    uintptr_t base = (uintptr_t)usart;
    if (base == 0x40011000UL || base == 0x40011400UL) {
        return 108000000UL;  /* APB2 */
    }
    return 54000000UL;       /* APB1 */
}
#endif /* SOC_SERIES_STM32F7 */

#ifndef HAL_RTT_SERIAL0_OTG
#define HAL_RTT_SERIAL0_OTG 0
#endif

extern const AP_HAL::HAL &hal;

// Global pointer to the USB console UARTDriver instance
static RTT::UARTDriver *_usb_console_driver = nullptr;

extern "C" void uart_usb_rx_bridge(const uint8_t *data, uint32_t len)
{
    if (_usb_console_driver != nullptr) {
        RTT::UARTDriver::usb_rx_bridge(data, len);
    }
}

using namespace RTT;

/*
 * CDC data receive callback — called from usb_lld_poll_rtt() context
 * when a bulk OUT packet arrives on EP2. Routes data to the USB UARTDriver
 * via the existing uart_usb_rx_bridge() C-linkage function.
 */
static void _usb_cdc_rx_cb(const uint8_t *data, uint32_t len, void *arg)
{
    (void)arg;
    /* usb_rx_bridge() writes to _readbuf and releases the RX semaphore */
    ::uart_usb_rx_bridge(data, len);
}

#if defined(SOC_SERIES_STM32F7)
/*
 * CMSIS register-level UART TX — polls TXE and TC directly.
 * Bypasses RTT serial V1 TX completion mechanism which deadlocks.
 */
static USART_TypeDef *uart_from_name(const char *name)
{
    int n = 0;
    if (name && (name[0] == 'u' || name[0] == 'U')) {
        const char *p = name;
        while (*p && (*p < '0' || *p > '9')) p++;
        if (*p) n = *p - '0';
    }
    switch (n) {
    case 1: return USART1;
    case 2: return USART2;
    case 3: return USART3;
    case 4: return UART4;
    case 5: return UART5;
    case 6: return USART6;
    case 7: return UART7;
    case 8: return UART8;
    default: return nullptr;
    }
}

/*
 * Polled UART TX: write len bytes from buf by polling TXE and TC.
 * Returns number of bytes actually written.
 */
static uint32_t uart_poll_write(USART_TypeDef *usart, const uint8_t *buf, uint32_t len, bool wait_tc)
{
    for (uint32_t i = 0; i < len; i++) {
        uint32_t timeout = 50000;
        while (!(usart->ISR & USART_ISR_TXE) && --timeout) { asm volatile("nop"); }
        if (timeout == 0) {
            return i;
        }
        usart->TDR = buf[i];
    }
    if (wait_tc) {
        uint32_t timeout = 50000;
        while (!(usart->ISR & USART_ISR_TC) && --timeout) { asm volatile("nop"); }
    }
    return len;
}

/*
 * Polled UART RX: read up to max_len bytes into buf by polling RXNE.
 * Returns number of bytes actually read.
 */
static uint32_t uart_poll_read(USART_TypeDef *usart, uint8_t *buf, uint32_t max_len)
{
    uint32_t n = 0;
    while (n < max_len && (usart->ISR & USART_ISR_RXNE)) {
        buf[n++] = (uint8_t)(usart->RDR & 0xFFU);
    }
    return n;
}

/*
 * DMA configuration tables for UART TX.
 * Indexed by usart_num (1-based: USART1=1, USART2=2, ...).
 * Stream 7 = DMA2 S7, Stream 6 = DMA1 S6, etc.
 */
struct uart_dma_info {
    uint32_t dma_base;   /* DMA peripheral base address */
    uint8_t  stream;     /* Stream index (0-7) */
    uint8_t  channel;    /* CHSEL value (0-7) */
};

static const uart_dma_info uart_dma_tx_info[] = {
    {0, 0, 0},                     /* 0 = placeholder */
    {0x40026400UL, 7, 4},         /* 1 = USART1: DMA2 S7 Ch4 (RM0410 Table 49) */
    {0x40026000UL, 6, 4},         /* 2 = USART2: DMA1 S6 Ch4 */
    {0x40026000UL, 3, 4},         /* 3 = USART3: DMA1 S3 Ch4 */
    {0x40026000UL, 4, 4},         /* 4 = UART4:  DMA1 S4 Ch4 */
    {0, 0, 0},                    /* 5 = UART5:  not used on CUAV V5 */
    {0x40026400UL, 6, 5},         /* 6 = USART6: DMA2 S6 Ch5 */
    {0x40026000UL, 1, 5},         /* 7 = UART7:  DMA1 S1 Ch5 */
    {0x40026000UL, 3, 5},         /* 8 = UART8:  DMA1 S3 Ch5 */
};

/*
 * DMA configuration tables for UART RX.
 * Indexed by usart_num (1-based: USART1=1, USART2=2, ...).
 */
static const uart_dma_info uart_dma_rx_info[] = {
    {0, 0, 0},                     /* 0 = placeholder */
    {0x40026400UL, 2, 4},         /* 1 = USART1: DMA2 S2 Ch4 */
    {0x40026000UL, 5, 4},         /* 2 = USART2: DMA1 S5 Ch4 */
    {0x40026000UL, 1, 4},         /* 3 = USART3: DMA1 S1 Ch4 */
    {0x40026000UL, 2, 4},         /* 4 = UART4:  DMA1 S2 Ch4 */
    {0, 0, 0},                    /* 5 = UART5:  not used on CUAV V5 */
    {0x40026400UL, 1, 5},         /* 6 = USART6: DMA2 S1 Ch5 */
    {0x40026000UL, 0, 5},         /* 7 = UART7:  DMA1 S0 Ch5 */
    {0x40026000UL, 6, 5},         /* 8 = UART8:  DMA1 S6 Ch5 (IOMCU RX, matches ChibiOS CUAV V5) */
};

/* DMA-safe bounce buffers (64 bytes each, 4-byte aligned for DMA access) */
static uint8_t dma_tx_bounce[64] __attribute__((aligned(4)));

/* Return the TCIF bit mask for a given DMA stream index (0-7) */
static inline uint32_t dma_tcif_bit(uint8_t stream)
{
    /* Stream0-3: bits in LISR; Stream4-7: bits in HISR */
    /* TCIF offset: S0=5, S1=11, S2=21, S3=27, S4=5, S5=11, S6=21, S7=27 */
    uint8_t rel = stream & 3U;  /* stream modulo 4 */
    return (1U << (5 + rel * 6));
}

/* Return pointer to the correct ISR register (LISR for stream 0-3, HISR for 4-7) */
static inline volatile uint32_t *dma_isr_reg(uint32_t dma_base, uint8_t stream)
{
    return (volatile uint32_t *)(dma_base + ((stream < 4) ? 0x00UL : 0x04UL));
}

/* Return pointer to the correct IFCR register (LIFCR for stream 0-3, HIFCR for 4-7) */
static inline volatile uint32_t *dma_ifcr_reg(uint32_t dma_base, uint8_t stream)
{
    return (volatile uint32_t *)(dma_base + ((stream < 4) ? 0x08UL : 0x0CUL));
}

static inline DMA_Stream_TypeDef *dma_stream_from_info(const uart_dma_info &info)
{
    if (info.dma_base == 0U) {
        return nullptr;
    }
    return (DMA_Stream_TypeDef *)(info.dma_base + 0x10UL + info.stream * 0x18UL);
}

static inline uint32_t dma_all_iflags_bit(uint8_t stream)
{
    static const uint32_t stream_flags[4] = {
        0x0000003DUL, /* stream 0/4: FE,DME,TE,HT,TC */
        0x00000F40UL, /* stream 1/5 */
        0x003D0000UL, /* stream 2/6 */
        0x0F400000UL, /* stream 3/7 */
    };
    return stream_flags[stream & 3U];
}

static inline uint32_t dma_error_iflags_bit(uint8_t stream)
{
    static const uint32_t stream_flags[4] = {
        0x0000000DUL, /* stream 0/4: FE,DME,TE */
        0x00000340UL, /* stream 1/5 */
        0x000D0000UL, /* stream 2/6 */
        0x03400000UL, /* stream 3/7 */
    };
    return stream_flags[stream & 3U];
}

static inline uint32_t dma_irq_status(uint32_t dma_base, uint8_t stream)
{
    volatile uint32_t *isr = dma_isr_reg(dma_base, stream);
    return *isr & dma_all_iflags_bit(stream);
}

static inline void dma_clear_all_iflags(uint32_t dma_base, uint8_t stream)
{
    volatile uint32_t *ifcr = dma_ifcr_reg(dma_base, stream);
    *ifcr = dma_all_iflags_bit(stream);
}

static inline void uart_clear_rx_flags(USART_TypeDef *uart)
{
    const uint32_t clear_flags = USART_ICR_ORECF | USART_ICR_NCF |
        USART_ICR_FECF | USART_ICR_PECF | USART_ICR_IDLECF;
    uart->ICR = clear_flags;
}

/* Local DMA stream control register bit defines (in case CMSIS lacks them) */
#ifndef DMA_SxCR_EN
#define DMA_SxCR_EN             (1U << 0)
#define DMA_SxCR_DBM            (1U << 1)
#define DMA_SxCR_TCIE           (1U << 4)
#define DMA_SxCR_HTIE           (1U << 5)
#define DMA_SxCR_DIR_M2P        (1U << 6)   /* DIR[1:0] = 01: memory-to-peripheral */
#define DMA_SxCR_DIR_P2M        (0U << 6)   /* DIR[1:0] = 00: peripheral-to-memory */
#define DMA_SxCR_CIRC           (1U << 8)
#define DMA_SxCR_PINC           (1U << 9)
#define DMA_SxCR_MINC           (1U << 10)
#define DMA_SxCR_CHSEL(n)       ((uint32_t)(n) << 25)
#endif
/* PSIZE/MSIZE raw bit positions (always defined, CMSIS may not have shorthand) */
#define DMA_CR_PSIZE_8BIT       (0U << 12)  /* 8-bit peripheral data size */
#define DMA_CR_MSIZE_8BIT       (0U << 14)  /* 8-bit memory data size */
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
#if defined(SOC_SERIES_STM32F7)
    , _uart_hw(nullptr)
#endif
{
    if (_port_num < RTT_UART_MAX_DRIVERS) {
        _drivers[_port_num] = this;
    }
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

    if (_port_num >= RTT_UART_MAX_DRIVERS ||
        _port_num >= ARRAY_SIZE(_device_names)) {
        return;
    }

    const char *name = _device_names[_port_num];
    const bool is_usb = (std::strncmp(name, "usb", 3) == 0);

    /* If already initialized, just change baud rate */
    if (_initialized && baud != 0) {
        _baudrate = baud;
        if (is_usb) {
            if (_dev != nullptr) {
                rt_device_control(_dev, 0x1000, &_baudrate);
            }
        } else {
#if defined(SOC_SERIES_STM32F7)
            if (_uart_hw != nullptr) {
                if ((_uart_hw->CR1 & USART_CR1_UE) == 0U) {
                    (void)usart_ll_init_for_instance(_uart_hw, baud);
                } else {
                    uint32_t pclk = uart_pclk(_uart_hw);
                    _uart_hw->BRR = uart_brr_value(pclk, baud);
                }
#ifdef HAL_UART_IOMCU_IDX
                if (_port_num == HAL_UART_IOMCU_IDX && _uart_hw == UART8 && !_rx_dma_active) {
                    (void)_start_iomcu_rx_dma();
                }
#endif
            }
#endif
        }
        return;
    }

    /* ================================
     * USB CDC path — direct DWC2 register access, no RT-Thread device
     * ================================ */
    if (is_usb) {
        /*
         * [Cybernetics Ch.4] Closed-loop: keep the AP-side USB queue close to
         * ChibiOS SerialUSB scale.  A very large pre-CDC queue hides endpoint
         * backpressure from GCS, so PARAM_VALUE can sit behind ordinary
         * telemetry even while DWC2/CherryUSB byte accounting is perfect.
         */
        if (txSpace < RTT_UART_USB_TX_BUF_SIZE) { txSpace = RTT_UART_USB_TX_BUF_SIZE; }
        if (rxSpace < RTT_UART_USB_RX_BUF_SIZE) { rxSpace = RTT_UART_USB_RX_BUF_SIZE; }

        /* No rt_device needed — USB is handled by hal_usb_lld_rtt.c directly.
         * The DWC2 init is done by usb_lld_init_rtt() in HAL_RTT_Class.cpp */

        _baudrate = baud;
        _is_usb = true;
        /*
         * [Cybernetics Ch.4] Closed-loop: USB CDC has no host-visible hardware
         * flow-control line.  ChibiOS leaves USB flow control disabled; doing
         * the same here keeps MAVFTP burst pacing active instead of letting
         * large ReadFile ACKs outrun CherryUSB/DWC2 completion feedback.
         */
        _flow_control = FLOW_CONTROL_DISABLE;
        _usb_console_driver = this;
        _deferred_open = false;

        if (_rx_sem == nullptr) {
            char sem_name[RT_NAME_MAX];
            rt_snprintf(sem_name, sizeof(sem_name), "urx%u", (unsigned)_port_num);
            _rx_sem = rt_sem_create(sem_name, 0, RT_IPC_FLAG_FIFO);
            if (_rx_sem == nullptr) {
                return;
            }
        }

        uint16_t rxS = rxSpace < 512 ? 512 : rxSpace;
        uint16_t txS = txSpace < 512 ? 512 : txSpace;
        if (_readbuf.get_size() != rxS) { _readbuf.set_size(rxS); }
        if (_writebuf.get_size() != txS) { _writebuf.set_size(txS); }

        /* Register CDC data RX callback */
        usb_lld_set_rx_callback(_usb_cdc_rx_cb, this);

        _initialized = true;

        /* Disable console output if this port is the RTT console */
        {
            rt_device_t console_dev = rt_console_get_device();
            if (console_dev) {
                rt_console_output_set_enabled(RT_FALSE);
            }
        }
        return;
    }

    /* ================================
     * UART (non-USB) path — CMSIS register-level access
     * ================================ */
#if defined(SOC_SERIES_STM32F7)
    USART_TypeDef *usart = uart_from_name(name);
    if (usart == nullptr) {
        // rt_kprintf("[UART%u] unknown UART '%s'\n", (unsigned)_port_num, name);
        return;
    }

    _uart_hw = usart;
    _is_usb = false;
    _deferred_open = false;
    _baudrate = baud;

    uint16_t rxS = rxSpace < 512 ? 512 : rxSpace;
    uint16_t txS = txSpace < 512 ? 512 : txSpace;
    if (_readbuf.get_size() != rxS) { _readbuf.set_size(rxS); }
    if (_writebuf.get_size() != txS) { _writebuf.set_size(txS); }

    {
        /* RTT console UART7 is brought up in rtt_ctl_uart_hw_init(); do not reset it. */
        rt_device_t console_dev = rt_console_get_device();
        bool is_console = false;
        if (console_dev) {
            const char *cons_name = console_dev->parent.name;
            is_console = (cons_name && std::strcmp(name, cons_name) == 0);
        }
        const bool skip_full_hw_init = is_console && (std::strcmp(name, "uart7") == 0);

        if (!skip_full_hw_init) {
            /* GPIO AF + clock + UE/RE/TE/BRR — required for GPS (USART1) and other AP ports */
            if (usart_ll_init_for_instance(usart, _baudrate) != 0) {
                uint32_t pclk = uart_pclk(usart);
                uint32_t brr = (_baudrate > 0U)
                    ? uart_brr_value(pclk, _baudrate)
                    : ((pclk + 57600U / 2U) / 57600U);
                usart->CR3 &= ~(USART_CR3_DMAR | USART_CR3_DMAT);
                usart->CR1 = USART_CR1_UE | USART_CR1_RE | USART_CR1_TE;
                usart->BRR = brr;
            }
        } else if (_baudrate > 0U) {
            uint32_t pclk = uart_pclk(usart);
            usart->BRR = uart_brr_value(pclk, _baudrate);
        }
    }

#ifdef HAL_UART_IOMCU_IDX
    if (_port_num == HAL_UART_IOMCU_IDX && _uart_hw == UART8) {
        (void)_start_iomcu_rx_dma();
    }
#endif

    _initialized = true;

    /* If this port is the RTT console device, disable console output */
    {
        rt_device_t console_dev = rt_console_get_device();
        if (console_dev) {
            const char *cons_name = console_dev->parent.name;
            if (cons_name && std::strcmp(name, cons_name) == 0) {
                rt_console_output_set_enabled(RT_FALSE);
            }
        }
    }
#else
    (void)baud;
    (void)rxSpace;
    (void)txSpace;
    (void)name;
    /* Non-STM32F7: CMSIS register access not available */
#endif /* SOC_SERIES_STM32F7 */
}

void UARTDriver::_end()
{
    if (!_initialized) {
        return;
    }
    _initialized = false;

    if (_is_usb) {
        /* USB path: close RT-Thread device and delete semaphore */
        if (_dev != nullptr) {
            rt_device_close(_dev);
            _dev = nullptr;
        }
        if (_rx_sem != nullptr) {
            rt_sem_delete(_rx_sem);
            _rx_sem = nullptr;
        }
    } else {
        /* UART path: disable USART via CMSIS */
#if defined(SOC_SERIES_STM32F7)
        if (_rx_dma_active) {
            _stop_rx_dma();
        }
        if (_uart_hw != nullptr) {
            _uart_hw->CR1 &= ~(USART_CR1_UE | USART_CR1_RE | USART_CR1_TE);
            _uart_hw = nullptr;
        }
#endif
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

extern volatile uint32_t rtt_uart_dbg_rx_dma_starts;
extern volatile uint32_t rtt_uart_dbg_rx_dma_start_fail;
extern volatile uint32_t rtt_uart_dbg_rx_dma_drains;
extern volatile uint32_t rtt_uart_dbg_rx_dma_bytes;
extern volatile uint32_t rtt_uart_dbg_rx_dma_drops;
extern volatile uint32_t rtt_uart_dbg_rx_dma_errors;
extern volatile uint32_t rtt_uart_dbg_rx_dma_head;
extern volatile uint32_t rtt_uart_dbg_rx_dma_tail;
extern volatile uint32_t rtt_uart_dbg_rx_dma_ndtr;

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

#if defined(SOC_SERIES_STM32F7)
bool UARTDriver::_start_iomcu_rx_dma()
{
    if (_uart_hw != UART8) {
        return false;
    }
    if (_rx_dma_active) {
        return true;
    }

    const uart_dma_info &info = uart_dma_rx_info[8];
    DMA_Stream_TypeDef *stream = dma_stream_from_info(info);
    if (stream == nullptr) {
        rtt_uart_dbg_rx_dma_start_fail++;
        return false;
    }

    if (_rx_dma_buf == nullptr) {
        _rx_dma_buf = (uint8_t *)rt_malloc_align(RTT_UART_RX_DMA_BUF_SIZE, 32);
        if (_rx_dma_buf == nullptr) {
            rtt_uart_dbg_rx_dma_start_fail++;
            return false;
        }
        memset(_rx_dma_buf, 0, RTT_UART_RX_DMA_BUF_SIZE);
        _rx_dma_buf_size = RTT_UART_RX_DMA_BUF_SIZE;
    }

    if (info.dma_base == DMA1_BASE) {
        RCC->AHB1ENR |= RCC_AHB1ENR_DMA1EN;
    } else if (info.dma_base == DMA2_BASE) {
        RCC->AHB1ENR |= RCC_AHB1ENR_DMA2EN;
    }
    __DSB();

    stream->CR &= ~DMA_SxCR_EN;
    uint32_t wait = 100000;
    while ((stream->CR & DMA_SxCR_EN) != 0U && --wait) {
        asm volatile("nop");
    }
    if ((stream->CR & DMA_SxCR_EN) != 0U) {
        rtt_uart_dbg_rx_dma_start_fail++;
        return false;
    }

    dma_clear_all_iflags(info.dma_base, info.stream);

    for (uint8_t i = 0; i < 16 && (_uart_hw->ISR & USART_ISR_RXNE) != 0U; i++) {
        (void)_uart_hw->RDR;
    }
    uart_clear_rx_flags(_uart_hw);

    stream->PAR = (uint32_t)(uintptr_t)&_uart_hw->RDR;
    stream->M0AR = (uint32_t)(uintptr_t)_rx_dma_buf;
    stream->M1AR = 0;
    stream->NDTR = _rx_dma_buf_size;
    stream->FCR = 0;
    stream->CR = ((uint32_t)info.channel << DMA_SxCR_CHSEL_Pos) |
                 DMA_SxCR_MINC |
                 DMA_SxCR_CIRC |
                 DMA_SxCR_PL_1;

    _rx_dma_tail = 0;
    _rx_dma_stream = stream;
    _uart_hw->CR3 |= USART_CR3_DMAR;
    __DSB();
    stream->CR |= DMA_SxCR_EN;

    _rx_dma_active = true;
    rtt_uart_dbg_rx_dma_starts++;
    rtt_uart_dbg_rx_dma_head = 0;
    rtt_uart_dbg_rx_dma_tail = 0;
    rtt_uart_dbg_rx_dma_ndtr = _rx_dma_buf_size;
    return true;
}

void UARTDriver::_stop_rx_dma()
{
    if (_rx_dma_stream != nullptr) {
        _rx_dma_stream->CR &= ~DMA_SxCR_EN;
        uint32_t wait = 100000;
        while ((_rx_dma_stream->CR & DMA_SxCR_EN) != 0U && --wait) {
            asm volatile("nop");
        }
    }

    if (_uart_hw != nullptr) {
        _uart_hw->CR3 &= ~USART_CR3_DMAR;
    }

    const uart_dma_info &info = uart_dma_rx_info[8];
    dma_clear_all_iflags(info.dma_base, info.stream);

    _rx_dma_active = false;
    _rx_dma_stream = nullptr;
    _rx_dma_tail = 0;
    _rx_dma_buf_size = 0;
    if (_rx_dma_buf != nullptr) {
        rt_free_align(_rx_dma_buf);
        _rx_dma_buf = nullptr;
    }
}

void UARTDriver::_drain_rx_dma_to_readbuf()
{
    if (!_rx_dma_active || _rx_dma_stream == nullptr ||
        _rx_dma_buf == nullptr || _rx_dma_buf_size == 0) {
        return;
    }

    const uart_dma_info &info = uart_dma_rx_info[8];
    const uint32_t dma_status = dma_irq_status(info.dma_base, info.stream);
    if ((dma_status & dma_error_iflags_bit(info.stream)) != 0U) {
        rtt_uart_dbg_rx_dma_errors++;
    }
    if (dma_status != 0U) {
        dma_clear_all_iflags(info.dma_base, info.stream);
    }

    const uint32_t uart_status = _uart_hw->ISR;
    if ((uart_status & (USART_ISR_ORE | USART_ISR_FE | USART_ISR_NE |
                        USART_ISR_PE | USART_ISR_IDLE)) != 0U) {
        if ((uart_status & (USART_ISR_ORE | USART_ISR_FE | USART_ISR_NE | USART_ISR_PE)) != 0U) {
            rtt_uart_dbg_rx_dma_errors++;
        }
        uart_clear_rx_flags(_uart_hw);
    }

    uint32_t ndtr = _rx_dma_stream->NDTR;
    if (ndtr > _rx_dma_buf_size) {
        rtt_uart_dbg_rx_dma_errors++;
        return;
    }

    uint16_t head = (uint16_t)(_rx_dma_buf_size - ndtr);
    if (head == _rx_dma_buf_size) {
        head = 0;
    }

    rtt_uart_dbg_rx_dma_head = head;
    rtt_uart_dbg_rx_dma_tail = _rx_dma_tail;
    rtt_uart_dbg_rx_dma_ndtr = ndtr;

    if (head == _rx_dma_tail) {
        return;
    }

    auto write_segment = [this](uint16_t ofs, uint16_t len) {
        if (len == 0) {
            return;
        }
        const uint32_t written = _readbuf.write(&_rx_dma_buf[ofs], len);
        _rx_stats_bytes += written;
        rtt_uart_dbg_rx_dma_bytes += written;
        if (written < len) {
            rtt_uart_dbg_rx_dma_drops += len - written;
        }
    };

    if (head > _rx_dma_tail) {
        write_segment(_rx_dma_tail, head - _rx_dma_tail);
    } else {
        write_segment(_rx_dma_tail, _rx_dma_buf_size - _rx_dma_tail);
        write_segment(0, head);
    }

    _rx_dma_tail = head;
    rtt_uart_dbg_rx_dma_tail = _rx_dma_tail;
    rtt_uart_dbg_rx_dma_drains++;
}
#endif

void UARTDriver::_drain_rx_to_readbuf()
{
    if (_is_usb) {
        /* USB path: data is received via callback in usb_lld_poll_rtt()
         * which writes directly to _readbuf via uart_usb_rx_bridge().
         * Nothing to do here — the callback handles everything. */
    } else {
        /* UART path: poll RXNE and read RDR directly */
#if defined(SOC_SERIES_STM32F7)
        if (_uart_hw == nullptr) {
            return;
        }
        if (_rx_dma_active) {
            _drain_rx_dma_to_readbuf();
            return;
        }
        uint32_t n = uart_poll_read(_uart_hw, _rx_bounce, sizeof(_rx_bounce));
        if (n > 0) {
            const uint32_t written = _readbuf.write(_rx_bounce, n);
            _rx_stats_bytes += written;
        }
#else
        (void)0;
#endif
    }
}

/* Debug counters for UART drain path — read via GDB */
volatile uint32_t rtt_uart_dbg_drain_calls RTT_DBG_DTCM_BSS = 0;
volatile uint32_t rtt_uart_dbg_drain_writes RTT_DBG_DTCM_BSS = 0;
volatile uint32_t rtt_uart_dbg_drain_zero RTT_DBG_DTCM_BSS = 0;
volatile uint32_t rtt_uart_dbg_drain_bytes RTT_DBG_DTCM_BSS = 0;
volatile uint32_t rtt_uart_dbg_drain_usb_full RTT_DBG_DTCM_BSS = 0;
volatile uint32_t rtt_uart_dbg_drain_usb_reentry RTT_DBG_DTCM_BSS = 0;
volatile uint32_t rtt_uart_dbg_drain_usb_rounds RTT_DBG_DTCM_BSS = 0;
volatile uint32_t rtt_uart_dbg_drain_usb_round_limit RTT_DBG_DTCM_BSS = 0;
volatile uint32_t rtt_uart_dbg_drain_usb_pending_limit RTT_DBG_DTCM_BSS = 0;
volatile uint32_t rtt_uart_dbg_drain_usb_last_pending RTT_DBG_DTCM_BSS = 0;
volatile uint32_t rtt_uart_dbg_usb_txspace_calls RTT_DBG_DTCM_BSS = 0;
volatile uint32_t rtt_uart_dbg_usb_txspace_last_writebuf_space RTT_DBG_DTCM_BSS = 0;
volatile uint32_t rtt_uart_dbg_usb_txspace_last_writebuf_available RTT_DBG_DTCM_BSS = 0;
volatile uint32_t rtt_uart_dbg_usb_txspace_last_lld_space RTT_DBG_DTCM_BSS = 0;
volatile uint32_t rtt_uart_dbg_usb_txspace_min_writebuf_space RTT_DBG_DTCM_BSS = 0;
volatile uint32_t rtt_uart_dbg_usb_txspace_min_lld_space RTT_DBG_DTCM_BSS = 0;
volatile uint32_t rtt_uart_dbg_usb_txspace_last_return RTT_DBG_DTCM_BSS = 0;
volatile uint32_t rtt_uart_dbg_usb_txspace_last_backlog_allowance RTT_DBG_DTCM_BSS = 0;
volatile uint32_t rtt_uart_dbg_usb_write_calls RTT_DBG_DTCM_BSS = 0;
volatile uint32_t rtt_uart_dbg_usb_write_short RTT_DBG_DTCM_BSS = 0;
volatile uint32_t rtt_uart_dbg_usb_write_drain_loops RTT_DBG_DTCM_BSS = 0;
volatile uint32_t rtt_uart_dbg_usb_write_last_size RTT_DBG_DTCM_BSS = 0;
volatile uint32_t rtt_uart_dbg_usb_write_last_written RTT_DBG_DTCM_BSS = 0;
volatile uint32_t rtt_uart_dbg_usb_write_last_space_before RTT_DBG_DTCM_BSS = 0;
volatile uint32_t rtt_uart_dbg_usb_write_last_space_after_drain RTT_DBG_DTCM_BSS = 0;
volatile uint32_t rtt_uart_dbg_usb_write_last_available_after_drain RTT_DBG_DTCM_BSS = 0;
volatile uint32_t rtt_uart_dbg_usb_write_post_drain_calls RTT_DBG_DTCM_BSS = 0;
volatile uint32_t rtt_uart_dbg_usb_write_no_space RTT_DBG_DTCM_BSS = 0;
volatile uint32_t rtt_uart_dbg_usb_write_wait_ms RTT_DBG_DTCM_BSS = 0;
volatile uint32_t rtt_uart_dbg_usb_write_max_wait_ms RTT_DBG_DTCM_BSS = 0;
volatile uint32_t rtt_uart_dbg_poll_drain_calls RTT_DBG_DTCM_BSS = 0;
volatile uint32_t rtt_uart_dbg_poll_drain_limited RTT_DBG_DTCM_BSS = 0;
volatile uint32_t rtt_uart_dbg_poll_drain_bytes RTT_DBG_DTCM_BSS = 0;
volatile uint32_t rtt_uart_dbg_poll_drain_zero RTT_DBG_DTCM_BSS = 0;
volatile uint32_t rtt_uart_dbg_poll_drain_last_port RTT_DBG_DTCM_BSS = 0;
volatile uint32_t rtt_uart_dbg_poll_drain_last_len RTT_DBG_DTCM_BSS = 0;
volatile uint32_t rtt_uart_dbg_poll_drain_last_written RTT_DBG_DTCM_BSS = 0;
volatile uint32_t rtt_uart_dbg_poll_drain_last_us RTT_DBG_DTCM_BSS = 0;
volatile uint32_t rtt_uart_dbg_poll_drain_max_us RTT_DBG_DTCM_BSS = 0;
#define RTT_UART_DBG_USB_TRACE_DEPTH 64U
volatile uint32_t rtt_uart_dbg_usb_trace_head RTT_DBG_DTCM_BSS = 0;
volatile uint32_t rtt_uart_dbg_usb_trace_total RTT_DBG_DTCM_BSS = 0;
volatile uint32_t rtt_uart_dbg_usb_trace_kind[RTT_UART_DBG_USB_TRACE_DEPTH] RTT_DBG_DTCM_BSS = {0};
volatile uint32_t rtt_uart_dbg_usb_trace_len[RTT_UART_DBG_USB_TRACE_DEPTH] RTT_DBG_DTCM_BSS = {0};
volatile uint32_t rtt_uart_dbg_usb_trace_available[RTT_UART_DBG_USB_TRACE_DEPTH] RTT_DBG_DTCM_BSS = {0};
volatile uint32_t rtt_uart_dbg_usb_trace_lld_space[RTT_UART_DBG_USB_TRACE_DEPTH] RTT_DBG_DTCM_BSS = {0};
volatile uint32_t rtt_uart_dbg_usb_trace_w0[RTT_UART_DBG_USB_TRACE_DEPTH] RTT_DBG_DTCM_BSS = {0};
volatile uint32_t rtt_uart_dbg_usb_trace_w1[RTT_UART_DBG_USB_TRACE_DEPTH] RTT_DBG_DTCM_BSS = {0};
volatile uint32_t rtt_uart_dbg_usb_trace_w2[RTT_UART_DBG_USB_TRACE_DEPTH] RTT_DBG_DTCM_BSS = {0};
volatile uint32_t rtt_uart_dbg_usb_trace_w3[RTT_UART_DBG_USB_TRACE_DEPTH] RTT_DBG_DTCM_BSS = {0};
volatile uint32_t rtt_uart_dbg_rx_dma_starts = 0;
volatile uint32_t rtt_uart_dbg_rx_dma_start_fail = 0;
volatile uint32_t rtt_uart_dbg_rx_dma_drains = 0;
volatile uint32_t rtt_uart_dbg_rx_dma_bytes = 0;
volatile uint32_t rtt_uart_dbg_rx_dma_drops = 0;
volatile uint32_t rtt_uart_dbg_rx_dma_errors = 0;
volatile uint32_t rtt_uart_dbg_rx_dma_head = 0;
volatile uint32_t rtt_uart_dbg_rx_dma_tail = 0;
volatile uint32_t rtt_uart_dbg_rx_dma_ndtr = 0;

static uint32_t rtt_uart_dbg_pack4(const uint8_t *buf, uint32_t len, uint32_t ofs)
{
    uint32_t v = 0;
    for (uint32_t i = 0; i < 4; i++) {
        const uint32_t p = ofs + i;
        if (buf != nullptr && p < len) {
            v |= uint32_t(buf[p]) << (i * 8U);
        }
    }
    return v;
}

static void rtt_uart_dbg_usb_trace(uint32_t kind, const uint8_t *buf, uint32_t len,
                                   uint32_t available, uint32_t lld_space)
{
    const uint32_t idx = rtt_uart_dbg_usb_trace_head & (RTT_UART_DBG_USB_TRACE_DEPTH - 1U);
    rtt_uart_dbg_usb_trace_kind[idx] = kind;
    rtt_uart_dbg_usb_trace_len[idx] = len;
    rtt_uart_dbg_usb_trace_available[idx] = available;
    rtt_uart_dbg_usb_trace_lld_space[idx] = lld_space;
    rtt_uart_dbg_usb_trace_w0[idx] = rtt_uart_dbg_pack4(buf, len, 0);
    rtt_uart_dbg_usb_trace_w1[idx] = rtt_uart_dbg_pack4(buf, len, 4);
    rtt_uart_dbg_usb_trace_w2[idx] = rtt_uart_dbg_pack4(buf, len, 8);
    rtt_uart_dbg_usb_trace_w3[idx] = rtt_uart_dbg_pack4(buf, len, 12);
    rtt_uart_dbg_usb_trace_head++;
    rtt_uart_dbg_usb_trace_total++;
}

void UARTDriver::_drain_writebuf_to_dev()
{
    if (_is_usb) {
        /* USB path: write directly to DWC2 EP1 TX FIFO */
        /* Send data in FS bulk packets (max 64 bytes each) */
        rt_base_t level = rt_hw_interrupt_disable();
        if (_tx_drain_active) {
            rt_hw_interrupt_enable(level);
            rtt_uart_dbg_drain_calls++;
            rtt_uart_dbg_drain_usb_reentry++;
            return;
        }
        _tx_drain_active = true;
        rt_hw_interrupt_enable(level);

        const uint32_t lld_space = usb_lld_txspace_rtt(1);
        if (lld_space == 0) {
            level = rt_hw_interrupt_disable();
            _tx_drain_active = false;
            rt_hw_interrupt_enable(level);
            rtt_uart_dbg_drain_calls++;
            rtt_uart_dbg_drain_usb_full++;
            rtt_uart_dbg_drain_zero++;
            _last_drain_wrote = false;
            return;
        }

        level = rt_hw_interrupt_disable();
        uint32_t n = _writebuf.peekbytes(_tx_bounce, MIN((uint32_t)sizeof(_tx_bounce), lld_space));
        rt_hw_interrupt_enable(level);
        if (n == 0) {
            level = rt_hw_interrupt_disable();
            _tx_drain_active = false;
            rt_hw_interrupt_enable(level);
            _last_drain_wrote = true;
            return;
        }
        level = rt_hw_interrupt_disable();
        const uint32_t available_before_send = _writebuf.available();
        rt_hw_interrupt_enable(level);
        uint32_t sent = 0;
        while (sent < n) {
            uint32_t chunk = (n - sent);
            if (chunk > 64) {
                chunk = 64;
            }
            if (usb_lld_send_rtt(1, _tx_bounce + sent, chunk)) {
                rtt_uart_dbg_usb_trace(2U, _tx_bounce + sent, chunk, available_before_send, lld_space);
                sent += chunk;
                rtt_uart_dbg_drain_bytes += chunk;
                rtt_uart_dbg_drain_writes++;
                rtt_uart_dbg_drain_usb_rounds++;
            } else {
                /* FIFO full or timeout — stop and retry next tick */
                rtt_uart_dbg_usb_trace(3U, _tx_bounce + sent, chunk, available_before_send, lld_space);
                rtt_uart_dbg_drain_usb_full++;
                break;
            }
        }
        rtt_uart_dbg_drain_calls++;
        if (sent > 0) {
            level = rt_hw_interrupt_disable();
            const uint32_t available = _writebuf.available();
            if (sent >= sizeof(_tx_bounce) && available > sent) {
                rtt_uart_dbg_drain_usb_round_limit++;
            }
            _writebuf.advance(sent);
            _tx_drain_active = false;
            rt_hw_interrupt_enable(level);
            _last_drain_wrote = true;
        } else {
            level = rt_hw_interrupt_disable();
            _tx_drain_active = false;
            rt_hw_interrupt_enable(level);
            rtt_uart_dbg_drain_zero++;
            _last_drain_wrote = false;
        }
    } else {
        /* UART path: try DMA first, fall back to CMSIS polling */
#if defined(SOC_SERIES_STM32F7)
        if (_uart_hw == nullptr) {
            return;
        }

        /* Determine usart_num from device name */
        const char *name = _device_names[_port_num];
        int usart_num = 0;
        if (name) {
            const char *p = name;
            while (*p && (*p < '0' || *p > '9')) p++;
            if (*p) usart_num = *p - '0';
        }

        /* --- DMA TX path --- */
        if (0 && usart_num > 0 && (size_t)usart_num < sizeof(uart_dma_tx_info)/sizeof(uart_dma_tx_info[0]) &&
            uart_dma_tx_info[usart_num].stream != 0) {

            uint32_t n = _writebuf.peekbytes(dma_tx_bounce, sizeof(dma_tx_bounce));
            if (n > 0) {
                const uart_dma_info &info = uart_dma_tx_info[usart_num];
                uint32_t stream_base = info.dma_base + 0x10UL + info.stream * 0x18UL;

                volatile uint32_t *cr   = (volatile uint32_t *)(stream_base + 0x00UL);
                volatile uint32_t *ndtr = (volatile uint32_t *)(stream_base + 0x04UL);
                volatile uint32_t *par  = (volatile uint32_t *)(stream_base + 0x08UL);
                volatile uint32_t *m0ar = (volatile uint32_t *)(stream_base + 0x0CUL);
                volatile uint32_t *fcr  = (volatile uint32_t *)(stream_base + 0x18UL);

                /* Disable stream first (clear any stale state) */
                *cr = 0;
                uint32_t wait = 10000;
                while ((*cr & DMA_SxCR_EN) && --wait) { asm volatile("nop"); }

                /* Configure peripheral address (USART TDR) and memory buffer */
                *par  = (uint32_t)(uintptr_t)(&(_uart_hw->TDR));
                *m0ar = (uint32_t)(uintptr_t)dma_tx_bounce;
                *ndtr = n;

                /* CR: memory-to-peripheral, 8-bit data size, CHSEL, TCIE, MINC */
                *cr = (1U << 6)              /* DIR[1:0] = 01: memory-to-peripheral */
                    | DMA_SxCR_TCIE          /* Enable TC interrupt */
                    | DMA_SxCR_MINC          /* increment memory address */
                    | ((uint32_t)(info.channel) << 25)  /* CHSEL[2:0] */
                    | DMA_CR_PSIZE_8BIT
                    | DMA_CR_MSIZE_8BIT;

                /* FCR: direct mode (FIFO disabled, threshold=0) */
                *fcr = 0;

                /* Clear any stale TCIF flag before starting */
                volatile uint32_t *ifcr = dma_ifcr_reg(info.dma_base, info.stream);
                *ifcr = dma_tcif_bit(info.stream);

                /* Ensure USART DMAT is still set (it was set in _begin) */
                _uart_hw->CR3 |= USART_CR3_DMAT;

                /* Enable DMA stream */
                _tx_dma_active = true;
                *cr |= DMA_SxCR_EN;

                /* Poll for TCIF (transfer complete) */
                volatile uint32_t *isr = dma_isr_reg(info.dma_base, info.stream);
                uint32_t tcif_mask = dma_tcif_bit(info.stream);
                uint32_t timeout = 100000;
                while (!(*isr & tcif_mask) && --timeout) { asm volatile("nop"); }

                /* Disable DMA stream */
                *cr = 0;
                _tx_dma_active = false;

                /* Clear TCIF flag */
                *ifcr = tcif_mask;

                /* Advance write buffer and update stats */
                _writebuf.advance(n);
                rtt_uart_dbg_drain_calls++;
                rtt_uart_dbg_drain_writes++;
                rtt_uart_dbg_drain_bytes += n;
                _last_drain_wrote = true;
                return;
            }
        }

        /* --- Polling fallback --- */
        /*
         * [Cybernetics Ch.4] Closed-loop: ChibiOS queues UART output below
         * MAVLink, while this RTT fallback busy-waits in the caller context.
         * Keep each polling drain short so non-USB telemetry cannot steal a
         * long continuous slot from USB PARAM_VALUE production.
         */
        constexpr uint32_t poll_drain_limit = 16U;
        uint32_t pending = _writebuf.available();
        uint32_t n = _writebuf.peekbytes(_tx_bounce, MIN((uint32_t)sizeof(_tx_bounce), poll_drain_limit));
        if (n == 0) {
            _last_drain_wrote = true;
            return;
        }
        if (pending > n) {
            rtt_uart_dbg_poll_drain_limited++;
        }
        const uint32_t poll_tstart_us = AP_HAL::micros();
        uint32_t w = uart_poll_write(_uart_hw, _tx_bounce, n, false);
        const uint32_t poll_elapsed_us = AP_HAL::micros() - poll_tstart_us;
        rtt_uart_dbg_poll_drain_calls++;
        rtt_uart_dbg_poll_drain_last_port = _port_num;
        rtt_uart_dbg_poll_drain_last_len = n;
        rtt_uart_dbg_poll_drain_last_written = w;
        rtt_uart_dbg_poll_drain_last_us = poll_elapsed_us;
        if (poll_elapsed_us > rtt_uart_dbg_poll_drain_max_us) {
            rtt_uart_dbg_poll_drain_max_us = poll_elapsed_us;
        }
        rtt_uart_dbg_drain_calls++;
        if (w > 0) {
            rtt_uart_dbg_drain_writes++;
            rtt_uart_dbg_drain_bytes += w;
            rtt_uart_dbg_poll_drain_bytes += w;
            _writebuf.advance(w);
            _last_drain_wrote = (w == n);
        } else {
            rtt_uart_dbg_drain_zero++;
            rtt_uart_dbg_poll_drain_zero++;
            _last_drain_wrote = false;
        }
#else
        (void)0;
#endif
    }
}

bool UARTDriver::wait_timeout(uint16_t n, uint32_t timeout_ms)
{
    if (!_initialized) {
        return false;
    }

    if (_is_usb) {
        /* USB path: use semaphore-based notification */
        if (_rx_sem == nullptr) {
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
                return false;
            }
            uint32_t elapsed = AP_HAL::millis() - t0;
            if (elapsed >= timeout_ms) {
                return false;
            }
            tick = rt_tick_from_millisecond(timeout_ms - elapsed);
        }
        return true;
    } else {
        /* UART path: poll hardware, yield like ChibiOS chEvtWaitAnyTimeout() */
        uint32_t t0 = AP_HAL::millis();
        while (_readbuf.available() < n) {
            _drain_rx_to_readbuf();
            if (_readbuf.available() >= n) {
                return true;
            }
            uint32_t elapsed = AP_HAL::millis() - t0;
            if (elapsed >= timeout_ms) {
                return false;
            }
            rt_thread_mdelay(1);
        }
        return true;
    }
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
    if (_unbuffered_writes && !_is_usb) {
#if defined(SOC_SERIES_STM32F7)
        if (_uart_hw != nullptr) {
            // For IOMCU and other unbuffered UART users: write directly to
            // hardware via CMSIS polling, bypassing the ring buffer.
            return uart_poll_write(_uart_hw, buffer, size, true);
        }
#endif
        // Fallback if no CMSIS UART available
        return 0;
    }
    /*
     * If the writebuf is full, drain it to the device to make room.
     * For USB, flush aggressively — writebuf may be full of stream data
     * that needs to be pushed to the CDC ringbuffer before we can
     * enqueue the caller's data (e.g. a PARAM_VALUE response).
     */
#if HAL_RTT_SERIAL0_OTG
    if (_is_usb) {
        rtt_uart_dbg_usb_write_calls++;
        rtt_uart_dbg_usb_write_last_size = size;
        rt_base_t level = rt_hw_interrupt_disable();
        rtt_uart_dbg_usb_write_last_space_before = _writebuf.space();
        rt_hw_interrupt_enable(level);
    }
#endif
    rt_base_t level = rt_hw_interrupt_disable();
    bool need_space = _writebuf.space() < size;
    rt_hw_interrupt_enable(level);
    if (need_space) {
        if (_is_usb) {
            uint32_t waited_ms = 0;
            for (int i = 0; i < 100; i++) {
                level = rt_hw_interrupt_disable();
                need_space = _writebuf.space() < size;
                rt_hw_interrupt_enable(level);
                if (!need_space) {
                    break;
                }
                _drain_writebuf_to_dev();
#if HAL_RTT_SERIAL0_OTG
                rtt_uart_dbg_usb_write_drain_loops++;
#endif
                level = rt_hw_interrupt_disable();
                need_space = _writebuf.space() < size;
                const bool drain_active = _tx_drain_active;
                rt_hw_interrupt_enable(level);
                if (need_space && (drain_active || usb_lld_txspace_rtt(1) == 0U)) {
                    /*
                     * [Cybernetics Ch.4] Closed-loop: give the active drain or
                     * USB IN completion chain one scheduler tick to free queue
                     * space.  Falling through immediately can turn a MAVLink
                     * fragment into a partial ByteBuffer write.
                     */
                    rt_thread_mdelay(1);
                    waited_ms++;
                }
            }
#if HAL_RTT_SERIAL0_OTG
            if (waited_ms > 0U) {
                rtt_uart_dbg_usb_write_wait_ms += waited_ms;
                if (waited_ms > rtt_uart_dbg_usb_write_max_wait_ms) {
                    rtt_uart_dbg_usb_write_max_wait_ms = waited_ms;
                }
            }
#endif
        } else {
            _drain_writebuf_to_dev();
        }
    }
    level = rt_hw_interrupt_disable();
    need_space = _writebuf.space() < size;
    rt_hw_interrupt_enable(level);
    if (_is_usb && need_space) {
#if HAL_RTT_SERIAL0_OTG
        rtt_uart_dbg_usb_write_no_space++;
        rtt_uart_dbg_usb_write_short++;
        rtt_uart_dbg_usb_write_last_written = 0;
#endif
        return 0;
    }
    level = rt_hw_interrupt_disable();
    const size_t written = _writebuf.write(buffer, size);
    const uint32_t available_after_write = _writebuf.available();
    const uint32_t space_after_write = _writebuf.space();
    rt_hw_interrupt_enable(level);
#if HAL_RTT_SERIAL0_OTG
    if (_is_usb) {
        rtt_uart_dbg_usb_trace(1U, buffer, written, available_after_write, usb_lld_txspace_rtt(1));
        rtt_uart_dbg_usb_write_last_written = written;
        rtt_uart_dbg_usb_write_last_space_after_drain = space_after_write;
        rtt_uart_dbg_usb_write_last_available_after_drain = available_after_write;
        if (written < size) {
            rtt_uart_dbg_usb_write_short++;
        }
    }
#endif
    if (_is_usb && written > 0 && !rtt_uart_mavlink_send_locked()) {
        /*
         * [Cybernetics Ch.4] Closed-loop: mirror ChibiOS SerialUSB obnotify().
         * Newly queued MAVLink bytes should be offered to the CDC ring now,
         * not wait up to one ap_uart 1 kHz tick before CherryUSB can start the
         * next IN transfer.  The drain is bounded by usb_lld_txspace_rtt().
         */
#if HAL_RTT_SERIAL0_OTG
        rtt_uart_dbg_usb_write_post_drain_calls++;
#endif
        _drain_writebuf_to_dev();
    }
    return written;
}

bool UARTDriver::_discard_input()
{
    if (!_initialized) {
        return false;
    }
    _readbuf.clear();
#if defined(SOC_SERIES_STM32F7)
    if (_rx_dma_active && _rx_dma_stream != nullptr && _rx_dma_buf_size > 0) {
        uint32_t ndtr = _rx_dma_stream->NDTR;
        if (ndtr <= _rx_dma_buf_size) {
            uint16_t head = (uint16_t)(_rx_dma_buf_size - ndtr);
            if (head == _rx_dma_buf_size) {
                head = 0;
            }
            _rx_dma_tail = head;
            rtt_uart_dbg_rx_dma_head = head;
            rtt_uart_dbg_rx_dma_tail = head;
            rtt_uart_dbg_rx_dma_ndtr = ndtr;
        }
        if (_uart_hw != nullptr) {
            uart_clear_rx_flags(_uart_hw);
        }
    }
#endif
    return true;
}

bool UARTDriver::is_initialized()
{
    return _initialized;
}

bool UARTDriver::tx_pending()
{
    rt_base_t level = rt_hw_interrupt_disable();
    const bool pending = _writebuf.available() > 0;
    rt_hw_interrupt_enable(level);
    return pending;
}

uint32_t UARTDriver::txspace()
{
    if (!_initialized) {
        return 0;
    }
    rt_base_t level = rt_hw_interrupt_disable();
    const uint32_t space = _writebuf.space();
    const uint32_t pending = _writebuf.available();
    rt_hw_interrupt_enable(level);
    if (_is_usb) {
        const uint32_t lld_space = usb_lld_txspace_rtt(1);
        rtt_uart_dbg_usb_txspace_calls++;
        rtt_uart_dbg_usb_txspace_last_writebuf_space = space;
        rtt_uart_dbg_usb_txspace_last_writebuf_available = pending;
        rtt_uart_dbg_usb_txspace_last_lld_space = lld_space;
        if (rtt_uart_dbg_usb_txspace_calls == 1U || space < rtt_uart_dbg_usb_txspace_min_writebuf_space) {
            rtt_uart_dbg_usb_txspace_min_writebuf_space = space;
        }
        if (rtt_uart_dbg_usb_txspace_calls == 1U || lld_space < rtt_uart_dbg_usb_txspace_min_lld_space) {
            rtt_uart_dbg_usb_txspace_min_lld_space = lld_space;
        }
        /*
         * [Cybernetics Ch.15] Extremum seeking: match ChibiOS' scheduling
         * contract.  ChibiOS SerialUSB reports the AP write-buffer space here;
         * endpoint ownership is enforced later by obnotify()/SOF/completion.
         * Keep the same split in RTT: MAVLink/PARAM scheduling sees the AP
         * queue, while _drain_writebuf_to_dev() and CherryUSB/DWC2 enforce the
         * real EP1 ring/EPENA/XFRC backpressure before bytes leave _writebuf.
         */
        const uint32_t backlog_allowance = space;
        const uint32_t effective = space;
        rtt_uart_dbg_usb_txspace_last_backlog_allowance = backlog_allowance;
        rtt_uart_dbg_usb_txspace_last_return = effective;
        return effective;
    }
    return space;
}

bool UARTDriver::_check_usb_connected() const
{
    return usb_lld_is_configured_rtt();
}

volatile uint32_t rtt_uart_dbg_tick_calls = 0;
/* Per-port tick counter — identifies which port's _timer_tick crashes the thread */
volatile uint32_t rtt_uart_dbg_port_ticks[10] = {};
volatile uint32_t rtt_uart_dbg_crash_port = 0xFFFFFFFF;  /* set to port_num on crash entry */

/* USB TX backpressure diagnostics (GDB / ctl telemetry; cumulative) */
volatile uint32_t rtt_uart_usb_diag_clears = 0;
volatile uint32_t rtt_uart_usb_diag_clear_deferred = 0;
volatile uint32_t rtt_uart_usb_diag_write_fails = 0;
volatile uint16_t rtt_uart_usb_diag_fail_streak = 0;

void UARTDriver::_timer_tick(void)
{
    rtt_uart_dbg_tick_calls++;
    rtt_uart_dbg_crash_port = _port_num;
    rtt_uart_dbg_port_ticks[_port_num < 10 ? _port_num : 0]++;
    if (!_initialized) {
        /* Only USB ports have deferred_open (UART ports access registers directly) */
        if (_is_usb && _deferred_open && _port_num < ARRAY_SIZE(_device_names)) {
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
                    if (_readbuf.get_size() < RTT_UART_USB_RX_BUF_SIZE) {
                        _readbuf.set_size(RTT_UART_USB_RX_BUF_SIZE);
                    }
                    if (_writebuf.get_size() < RTT_UART_USB_TX_BUF_SIZE) {
                        _writebuf.set_size(RTT_UART_USB_TX_BUF_SIZE);
                    }
                    _flow_control = FLOW_CONTROL_DISABLE;
                    _initialized = true;
                    _deferred_open = false;
                }
            }
        }
        return;
    }

    if (_is_usb && !_check_usb_connected()) {
        /* Drop queued TX only when the link is truly down — not on brief
         * de-configure during bus reset while still connected. */
        rt_base_t level = rt_hw_interrupt_disable();
        const bool have_pending_tx = _writebuf.available() > 0;
        const bool drain_active = _tx_drain_active;
        rt_hw_interrupt_enable(level);
        if (!usb_lld_get_connected_rtt() && have_pending_tx) {
            level = rt_hw_interrupt_disable();
            if (!_tx_drain_active) {
                _writebuf.clear();
            } else {
                rtt_uart_usb_diag_clear_deferred++;
            }
            rt_hw_interrupt_enable(level);
            if (!drain_active) {
                _usb_write_fail_count = 0;
                rtt_uart_usb_diag_fail_streak = 0;
                rtt_uart_usb_diag_clears++;
            }
        }
        return;
    }

    _drain_rx_to_readbuf();
    _drain_writebuf_to_dev();

    if (_is_usb) {
        /* Track consecutive write failures (endpoint backpressure). Never discard
         * queued MAVLink while the link is up — CherryUSB 64B packets can stall
         * for hundreds of ticks without indicating disconnect. */
        rt_base_t level = rt_hw_interrupt_disable();
        const bool writebuf_empty = _writebuf.available() == 0;
        rt_hw_interrupt_enable(level);
        if (writebuf_empty) {
            _usb_write_fail_count = 0;
            rtt_uart_usb_diag_fail_streak = 0;
        } else if (!_last_drain_wrote) {
            /* drain attempted but wrote 0 bytes → endpoint full/stuck */
            _usb_write_fail_count++;
            rtt_uart_usb_diag_write_fails++;
            if (_usb_write_fail_count > rtt_uart_usb_diag_fail_streak) {
                rtt_uart_usb_diag_fail_streak = _usb_write_fail_count;
            }
            /* milestone backpressure relief: if the endpoint makes no progress
             * for >500 ticks (~0.5s @ 1kHz), discard the queued TX even while
             * still connected.  Without this the writebuf grows monotonically
             * under bursty load (MAVFTP), pushing end-to-end latency past the
             * client timeout and causing successive-round degradation. */
            if (!usb_lld_get_connected_rtt()) {
                rt_base_t level = rt_hw_interrupt_disable();
                if (!_tx_drain_active) {
                    _writebuf.clear();
                } else {
                    rtt_uart_usb_diag_clear_deferred++;
                }
                const bool cleared = !_tx_drain_active;
                rt_hw_interrupt_enable(level);
                if (cleared) {
                    _usb_write_fail_count = 0;
                    rtt_uart_usb_diag_fail_streak = 0;
                    rtt_uart_usb_diag_clears++;
                }
            }
        } else {
            _usb_write_fail_count = 0;
            rtt_uart_usb_diag_fail_streak = 0;
        }
    } else {
        _usb_write_fail_count = 0;
    }
}

void UARTDriver::set_flow_control(enum flow_control flow)
{
    if (_is_usb) {
        return;
    }
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
    /* Align with ChibiOS UARTDriver.cpp:1527 — subtract transport time to
     * estimate the EARLIEST arrival time of a multi-byte packet (constraint,
     * not exact time). Do not estimate for USB (no meaningful baudrate). */
    if (_baudrate > 0 && !_is_usb) {
        const uint32_t transport_time_us = (1000000UL * 10UL / _baudrate) * (nbytes + _readbuf.available());
        last_receive_us -= transport_time_us;
    }
    return last_receive_us;
}

void RTT::UARTDriver::usb_rx_bridge(const uint8_t *data, size_t len)
{
    if (::_usb_console_driver != nullptr) {
        ::_usb_console_driver->_readbuf.write(data, len);
        /* Release the RX semaphore to wake wait_timeout() */
        if (::_usb_console_driver->_rx_sem != nullptr) {
            rt_sem_release(::_usb_console_driver->_rx_sem);
        }
    }
}
