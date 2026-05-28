/*
 * CherryUSB CDC ACM shim — implements hal_usb_lld_rtt.h compatibility API for UARTDriver.
 *
 * Single OTG_FS_IRQHandler lives in cherryusb_board/usb_dc_glue.c.
 * Production VID/PID 0x1209:0x5741 (matches usb_cdc_rtt.c). No MSC / composite.
 */
#include "hal_usb_lld_rtt.h"
#include "usb_dc_glue.h"

#include <string.h>
#include <stdbool.h>
#include <stdint.h>

#include "usbd_core.h"
#include "usbd_cdc_acm.h"

#include <rthw.h>

#include "rtt_dbg_bkp.h"

#define CHERRY_USB_OTG_FS_BASE  0x50000000UL

#define CDC_IN_EP   0x81
#define CDC_OUT_EP  0x02
#define CDC_INT_EP  0x83

#define USBD_VID           0x1209
#define USBD_PID           0x5741
#define USBD_MAX_POWER     50
#define USBD_LANGID_STRING 0x0409

#define USB_CONFIG_SIZE (9 + CDC_ACM_DESCRIPTOR_LEN)
#define CDC_MAX_MPS     64
#define CDC_RX_QUEUE_DEPTH 8

static const uint8_t cherry_cdc_descriptor[] = {
    USB_DEVICE_DESCRIPTOR_INIT(USB_2_0, 0xEF, 0x02, 0x01, USBD_VID, USBD_PID, 0x0200, 0x01),
    USB_CONFIG_DESCRIPTOR_INIT(USB_CONFIG_SIZE, 0x02, 0x01, USB_CONFIG_BUS_POWERED, USBD_MAX_POWER),
    CDC_ACM_DESCRIPTOR_INIT(0x00, CDC_INT_EP, CDC_OUT_EP, CDC_IN_EP, CDC_MAX_MPS, 0x02),
    USB_LANGID_INIT(USBD_LANGID_STRING),
    0x08, USB_DESCRIPTOR_TYPE_STRING,
    'A', 0x00, 'P', 0x00, 'M', 0x00,
    0x1C, USB_DESCRIPTOR_TYPE_STRING,
    'C', 0x00, 'U', 0x00, 'A', 0x00, 'V', 0x00, ' ', 0x00,
    'V', 0x00, '5', 0x00, ' ', 0x00, 'C', 0x00, 'D', 0x00, 'C', 0x00, ' ', 0x00, '1', 0x00,
    0x0C, USB_DESCRIPTOR_TYPE_STRING,
    '0', 0x00, '0', 0x00, '0', 0x00, '0', 0x00, '1', 0x00,
    0x00
};

USB_NOCACHE_RAM_SECTION USB_MEM_ALIGNX static uint8_t cdc_read_buf[CDC_MAX_MPS];
USB_NOCACHE_RAM_SECTION USB_MEM_ALIGNX static uint8_t cdc_tx_buf[CDC_MAX_MPS];
USB_NOCACHE_RAM_SECTION USB_MEM_ALIGNX static uint8_t cdc_tx_pending_buf[CDC_MAX_MPS];
static uint8_t cherry_rx_queue[CDC_RX_QUEUE_DEPTH][CDC_MAX_MPS];
static volatile uint8_t cherry_rx_len[CDC_RX_QUEUE_DEPTH];
static volatile uint8_t cherry_rx_head;
static volatile uint8_t cherry_rx_tail;
static volatile uint8_t cherry_rx_count;

static volatile bool cherry_configured;
static volatile bool cherry_dtr;
static volatile uint8_t cherry_tx_busy;
static volatile uint8_t cherry_tx_pending_len;
static volatile bool cherry_tx_pending_valid;

volatile uint32_t rtt_dbg_usb_init       = 0;
volatile uint32_t rtt_dbg_usb_usbrst     = 0;
volatile uint32_t rtt_dbg_usb_enumdne    = 0;
volatile uint32_t rtt_dbg_usb_setup_stup = 0;
volatile uint32_t rtt_dbg_cherry_rx_enqueued = 0;
volatile uint32_t rtt_dbg_cherry_rx_dropped  = 0;
volatile uint32_t rtt_dbg_cherry_rx_drained  = 0;
volatile uint32_t rtt_dbg_cherry_usb_reset   = 0;

/* CDC IN completion chain + link state (GDB / OpenOCD observation only). */
volatile uint32_t rtt_dbg_cherry_bulk_in_calls     = 0;
volatile uint32_t rtt_dbg_cherry_tx_start_ok       = 0;
volatile uint32_t rtt_dbg_cherry_tx_start_fail     = 0;
volatile uint32_t rtt_dbg_cherry_tx_busy_state     = 0;
volatile uint32_t rtt_dbg_cherry_configured_state  = 0;
volatile uint32_t rtt_dbg_cherry_last_event        = 0;
volatile uint32_t rtt_dbg_cherry_init_calls        = 0;
volatile uint32_t rtt_dbg_cherry_init_skipped      = 0;
volatile uint32_t rtt_dbg_cherry_bkp_witness       = 0;

enum {
    RTT_DBG_CHERRY_EVT_NONE       = 0,
    RTT_DBG_CHERRY_EVT_CONFIGURED = 1,
    RTT_DBG_CHERRY_EVT_RESET      = 2,
    RTT_DBG_CHERRY_EVT_DEINIT     = 3,
};

static bool cherry_usb_initialized;

static inline void cherry_dbg_sync_tx_busy(void)
{
    rtt_dbg_cherry_tx_busy_state = cherry_tx_busy;
}

static inline void cherry_dbg_sync_configured(void)
{
    rtt_dbg_cherry_configured_state = cherry_configured ? 1U : 0U;
}

static usb_rx_callback_t cherry_rx_cb;
static void *cherry_rx_arg;

static struct cdc_line_coding cherry_line_coding = {
    .dwDTERate = 921600,
    .bCharFormat = 0,
    .bParityType = 0,
    .bDataBits = 8,
};

static struct usbd_interface cherry_intf0;
static struct usbd_interface cherry_intf1;

static void cherry_rx_queue_reset(void)
{
    cherry_rx_head = 0;
    cherry_rx_tail = 0;
    cherry_rx_count = 0;
}

static void cherry_rx_enqueue_from_isr(const uint8_t *data, uint32_t len)
{
    if (len == 0 || len > CDC_MAX_MPS || data == NULL) {
        return;
    }

    if (cherry_rx_count >= CDC_RX_QUEUE_DEPTH) {
        rtt_dbg_cherry_rx_dropped++;
        return;
    }

    const uint8_t slot = cherry_rx_head;
    memcpy(cherry_rx_queue[slot], data, len);
    cherry_rx_len[slot] = (uint8_t)len;
    cherry_rx_head = (uint8_t)((slot + 1U) % CDC_RX_QUEUE_DEPTH);
    cherry_rx_count++;
    rtt_dbg_cherry_rx_enqueued++;
}

static bool cherry_rx_dequeue(uint8_t *data, uint32_t *len)
{
    if (data == NULL || len == NULL || cherry_rx_count == 0) {
        return false;
    }

    const uint8_t slot = cherry_rx_tail;
    const uint8_t n = cherry_rx_len[slot];
    if (n == 0 || n > CDC_MAX_MPS) {
        cherry_rx_tail = (uint8_t)((slot + 1U) % CDC_RX_QUEUE_DEPTH);
        cherry_rx_count--;
        *len = 0;
        return true;
    }

    memcpy(data, cherry_rx_queue[slot], n);
    cherry_rx_len[slot] = 0;
    cherry_rx_tail = (uint8_t)((slot + 1U) % CDC_RX_QUEUE_DEPTH);
    cherry_rx_count--;
    *len = n;
    rtt_dbg_cherry_rx_drained++;
    return true;
}

static bool cherry_tx_start_write(uint32_t len)
{
    if (len == 0 || len > CDC_MAX_MPS) {
        return false;
    }
    cherry_tx_busy = 1;
    cherry_dbg_sync_tx_busy();
    if (usbd_ep_start_write(0, CDC_IN_EP, cdc_tx_buf, len) != 0) {
        cherry_tx_busy = 0;
        cherry_dbg_sync_tx_busy();
        rtt_dbg_cherry_tx_start_fail++;
        return false;
    }
    rtt_dbg_cherry_tx_start_ok++;
    return true;
}

static void cherry_tx_flush_pending(void)
{
    if (!cherry_tx_pending_valid || cherry_tx_busy) {
        return;
    }

    const uint32_t len = cherry_tx_pending_len;
    if (len == 0 || len > CDC_MAX_MPS) {
        cherry_tx_pending_valid = false;
        cherry_tx_pending_len = 0;
        return;
    }

    memcpy(cdc_tx_buf, cdc_tx_pending_buf, len);
    cherry_tx_pending_valid = false;
    cherry_tx_pending_len = 0;
    (void)cherry_tx_start_write(len);
}

static void cherry_usbd_event_handler(uint8_t busid, uint8_t event)
{
    switch (event) {
    case USBD_EVENT_CONFIGURED:
        rtt_dbg_usb_enumdne++;
        rtt_dbg_cherry_last_event = RTT_DBG_CHERRY_EVT_CONFIGURED;
        cherry_configured = true;
        cherry_dbg_sync_configured();
        cherry_rx_queue_reset();
        cherry_tx_busy = 0;
        cherry_dbg_sync_tx_busy();
        cherry_tx_pending_valid = false;
        cherry_tx_pending_len = 0;
        usbd_ep_start_read(busid, CDC_OUT_EP, cdc_read_buf, sizeof(cdc_read_buf));
        break;
    case USBD_EVENT_RESET:
        rtt_dbg_usb_usbrst++;
        rtt_dbg_cherry_usb_reset++;
        rtt_dbg_cherry_last_event = RTT_DBG_CHERRY_EVT_RESET;
        cherry_configured = false;
        cherry_dbg_sync_configured();
        cherry_dtr = false;
        cherry_rx_queue_reset();
        cherry_tx_busy = 0;
        cherry_dbg_sync_tx_busy();
        cherry_tx_pending_valid = false;
        cherry_tx_pending_len = 0;
        break;
    case USBD_EVENT_DEINIT:
        rtt_dbg_cherry_last_event = RTT_DBG_CHERRY_EVT_DEINIT;
        cherry_configured = false;
        cherry_dbg_sync_configured();
        cherry_dtr = false;
        cherry_rx_queue_reset();
        cherry_tx_busy = 0;
        cherry_dbg_sync_tx_busy();
        cherry_tx_pending_valid = false;
        cherry_tx_pending_len = 0;
        break;
    default:
        break;
    }
}

void usbd_cdc_acm_bulk_out(uint8_t busid, uint8_t ep, uint32_t nbytes)
{
    (void)ep;

    /* Defer MAVLink parsing to usb_lld_poll_rtt() (main loop, IRQ masked there). */
    cherry_rx_enqueue_from_isr(cdc_read_buf, nbytes);
    usbd_ep_start_read(busid, CDC_OUT_EP, cdc_read_buf, sizeof(cdc_read_buf));
}

void usbd_cdc_acm_bulk_in(uint8_t busid, uint8_t ep, uint32_t nbytes)
{
    (void)busid;
    (void)ep;
    (void)nbytes;

    rtt_dbg_cherry_bulk_in_calls++;

    rt_base_t level = rt_hw_interrupt_disable();
    cherry_tx_busy = 0;
    cherry_dbg_sync_tx_busy();
    cherry_tx_flush_pending();
    rt_hw_interrupt_enable(level);
}

static struct usbd_endpoint cherry_ep_out = {
    .ep_addr = CDC_OUT_EP,
    .ep_cb = usbd_cdc_acm_bulk_out,
};

static struct usbd_endpoint cherry_ep_in = {
    .ep_addr = CDC_IN_EP,
    .ep_cb = usbd_cdc_acm_bulk_in,
};

void usbd_cdc_acm_set_line_coding(uint8_t busid, uint8_t intf, struct cdc_line_coding *line_coding)
{
    (void)busid;
    (void)intf;
    rtt_dbg_usb_setup_stup++;
    if (line_coding != NULL) {
        cherry_line_coding = *line_coding;
    }
}

void usbd_cdc_acm_get_line_coding(uint8_t busid, uint8_t intf, struct cdc_line_coding *line_coding)
{
    (void)busid;
    (void)intf;
    rtt_dbg_usb_setup_stup++;
    if (line_coding != NULL) {
        *line_coding = cherry_line_coding;
    }
}

void usbd_cdc_acm_set_dtr(uint8_t busid, uint8_t intf, bool dtr)
{
    (void)busid;
    (void)intf;
    rtt_dbg_usb_setup_stup++;
    cherry_dtr = dtr;
}

static void cherry_cdc_stack_init(void)
{
    usbd_desc_register(0, cherry_cdc_descriptor);
    usbd_add_interface(0, usbd_cdc_acm_init_intf(0, &cherry_intf0));
    usbd_add_interface(0, usbd_cdc_acm_init_intf(0, &cherry_intf1));
    usbd_add_endpoint(0, &cherry_ep_out);
    usbd_add_endpoint(0, &cherry_ep_in);
    usbd_initialize(0, CHERRY_USB_OTG_FS_BASE, cherry_usbd_event_handler);
}

/* -------------------------------------------------------------------------- */
/* hal_usb_lld_rtt.h compatibility API                                        */
/* -------------------------------------------------------------------------- */

bool usb_lld_init_rtt(void)
{
    rtt_dbg_usb_init++;
    rtt_dbg_cherry_init_calls++;
#if defined(STM32F7)
    rtt_dbg_cherry_bkp_witness = *RTT_DBG_BKP_WITNESS_REG;
#endif
    if (cherry_usb_initialized) {
        rtt_dbg_cherry_init_skipped++;
#if defined(STM32F7)
        rtt_dbg_bkp_stamp(RTT_DBG_BKP_TAG_CHERRY_INIT_SKIP);
#endif
        return true;
    }
    cherry_usb_initialized = true;
    l7_usb_hw_preinit();
    cherry_cdc_stack_init();
#if defined(STM32F7)
    rtt_dbg_bkp_stamp(RTT_DBG_BKP_TAG_CHERRY_INIT);
#endif
    return true;
}

void usb_lld_poll_rtt(void)
{
    while (cherry_rx_cb != NULL) {
        uint8_t local[CDC_MAX_MPS];
        uint32_t len = 0;

        rt_base_t level = rt_hw_interrupt_disable();
        const bool have_rx = cherry_rx_dequeue(local, &len);
        rt_hw_interrupt_enable(level);

        if (!have_rx) {
            break;
        }
        if (len > 0) {
            cherry_rx_cb(local, len, cherry_rx_arg);
        }
    }

    if (cherry_tx_pending_valid && !cherry_tx_busy) {
        rt_base_t level = rt_hw_interrupt_disable();
        cherry_tx_flush_pending();
        rt_hw_interrupt_enable(level);
    }
}

bool usb_lld_send_rtt(uint8_t ep, const uint8_t *data, uint32_t len)
{
    bool ok = false;

    if (!cherry_configured || ep != 1 || data == NULL || len == 0) {
        return false;
    }
    if (len > CDC_MAX_MPS) {
        len = CDC_MAX_MPS;
    }

    rt_base_t level = rt_hw_interrupt_disable();

    if (!cherry_tx_busy) {
        memcpy(cdc_tx_buf, data, len);
        ok = cherry_tx_start_write(len);
    } else if (!cherry_tx_pending_valid) {
        /* One 64B pending slot while IN transfer is in flight (no blocking). */
        memcpy(cdc_tx_pending_buf, data, len);
        cherry_tx_pending_len = (uint8_t)len;
        cherry_tx_pending_valid = true;
        ok = true;
    }

    rt_hw_interrupt_enable(level);
    return ok;
}

void usb_lld_set_rx_callback(usb_rx_callback_t cb, void *arg)
{
    cherry_rx_cb = cb;
    cherry_rx_arg = arg;
}

void usb_lld_rearm_cdc_out(void)
{
    if (cherry_configured) {
        usbd_ep_start_read(0, CDC_OUT_EP, cdc_read_buf, sizeof(cdc_read_buf));
    }
}

bool usb_lld_is_configured_rtt(void)
{
    return cherry_configured;
}

bool usb_lld_get_connected_rtt(void)
{
    /* Match native stack: enumerated/configured, not gated on DTR (DTR tracked for class requests). */
    return cherry_configured;
}

void usb_lld_set_address_rtt(uint8_t addr)
{
    (void)addr;
}

bool usb_lld_send_cdc_notification(uint16_t serial_state)
{
    (void)serial_state;
    return false;
}
