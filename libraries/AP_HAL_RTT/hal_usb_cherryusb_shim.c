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
#include <rtthread.h>

#include "rtt_dbg_bkp.h"

#define CHERRY_USB_OTG_FS_BASE  0x50000000UL

/* DWC2 device-mode register access (CDC bulk IN = physical ep 1). */
#define CHERRY_CDC_IN_EP_IDX    1U

#define CHERRY_OTG_REG32(off) \
    (*(volatile uint32_t *)(CHERRY_USB_OTG_FS_BASE + (off)))

#define CHERRY_DIEPCTL(ep)      CHERRY_OTG_REG32(0x900U + (uint32_t)(ep) * 0x20U)
#define CHERRY_DIEPINT(ep)      CHERRY_OTG_REG32(0x908U + (uint32_t)(ep) * 0x20U)
#define CHERRY_DIEPTSIZ(ep)     CHERRY_OTG_REG32(0x910U + (uint32_t)(ep) * 0x20U)
#define CHERRY_DTXFSTS(ep)      CHERRY_OTG_REG32(0x918U + (uint32_t)(ep) * 0x20U)
#define CHERRY_DIEPEMPMSK       CHERRY_OTG_REG32(0x834U)
#define CHERRY_GRSTCTL          CHERRY_OTG_REG32(0x010U)

#define CHERRY_DIEPCTL_EPENA    (1UL << 31)
#define CHERRY_DIEPCTL_EPDIS    (1UL << 30)
#define CHERRY_DIEPCTL_SNAK     (1UL << 27)
#define CHERRY_DIEPINT_XFRC     (1UL << 0)
#define CHERRY_DIEPINT_EPDISD   (1UL << 1)
#define CHERRY_GRSTCTL_TXFFLSH  (1UL << 5)
#define CHERRY_GRSTCTL_TXFNUM1  (1UL << 6)

#define CHERRY_TX_BUSY_TIMEOUT_MS  100U

#define CDC_IN_EP   0x81
#define CDC_OUT_EP  0x02
#define CDC_INT_EP  0x83

#define USBD_VID           0x1209
#define USBD_PID           0x5741
#define USBD_MAX_POWER     50
#define USBD_LANGID_STRING 0x0409

#define USB_CONFIG_SIZE (9 + CDC_ACM_DESCRIPTOR_LEN)
#define CDC_MAX_MPS     64
#define CDC_TX_CHUNK_MAX  64
#define CDC_RX_QUEUE_DEPTH 32
#define CDC_TX_RING_DEPTH  32

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
USB_NOCACHE_RAM_SECTION USB_MEM_ALIGNX static uint8_t cdc_tx_buf[CDC_TX_CHUNK_MAX];
static uint8_t cherry_rx_queue[CDC_RX_QUEUE_DEPTH][CDC_MAX_MPS];
static uint8_t cherry_tx_ring[CDC_TX_RING_DEPTH][CDC_MAX_MPS];
static volatile uint8_t cherry_tx_ring_len[CDC_TX_RING_DEPTH];
static volatile uint8_t cherry_tx_ring_head;
static volatile uint8_t cherry_tx_ring_tail;
static volatile uint8_t cherry_tx_ring_count;
static volatile uint8_t cherry_rx_len[CDC_RX_QUEUE_DEPTH];
static volatile uint8_t cherry_rx_head;
static volatile uint8_t cherry_rx_tail;
static volatile uint8_t cherry_rx_count;

static volatile bool cherry_configured;
static volatile bool cherry_dtr;
static volatile uint8_t cherry_tx_busy;

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
volatile uint32_t rtt_dbg_cherry_tx_ring_enqueued  = 0;
volatile uint32_t rtt_dbg_cherry_tx_ring_dropped   = 0;
volatile uint32_t rtt_dbg_cherry_tx_kick_calls     = 0;
volatile uint32_t rtt_dbg_cherry_epena_guard_hits    = 0;
volatile uint32_t rtt_dbg_cherry_epdis_recovery_count = 0;
volatile uint32_t rtt_dbg_cherry_tx_busy_max_ms      = 0;
volatile uint32_t rtt_dbg_cherry_recovery_busy_timeout_count = 0;
volatile uint32_t rtt_dbg_cherry_recovery_epena_stuck_count  = 0;
volatile uint32_t rtt_dbg_cherry_recovery_last_reason        = 0;
volatile uint32_t rtt_dbg_cherry_recovery_last_elapsed_ms    = 0;
volatile uint32_t rtt_dbg_cherry_recovery_max_busy_ms        = 0;
volatile uint32_t rtt_dbg_cherry_recovery_max_epena_ms       = 0;
volatile uint32_t rtt_dbg_cherry_recovery_last_diepctl       = 0;
volatile uint32_t rtt_dbg_cherry_recovery_last_diepint       = 0;
volatile uint32_t rtt_dbg_cherry_recovery_last_dieptsiz      = 0;
volatile uint32_t rtt_dbg_cherry_recovery_last_dtxfsts       = 0;
volatile uint32_t rtt_dbg_cherry_recovery_last_empmsk        = 0;
volatile uint32_t rtt_dbg_cherry_recovery_last_ring_count    = 0;
volatile uint32_t rtt_dbg_cherry_recovery_last_bulk_arm      = 0;
volatile uint32_t rtt_dbg_cherry_recovery_last_bulk_now      = 0;

static uint32_t cherry_tx_busy_since_ms;
static uint32_t cherry_tx_bulk_in_arm_gen;
static uint32_t cherry_epena_stuck_since_ms;

enum {
    RTT_DBG_CHERRY_EVT_NONE       = 0,
    RTT_DBG_CHERRY_EVT_CONFIGURED = 1,
    RTT_DBG_CHERRY_EVT_RESET      = 2,
    RTT_DBG_CHERRY_EVT_DEINIT     = 3,
};

enum {
    RTT_DBG_CHERRY_RECOVERY_NONE         = 0,
    RTT_DBG_CHERRY_RECOVERY_EPENA_STUCK  = 1,
    RTT_DBG_CHERRY_RECOVERY_BUSY_TIMEOUT = 2,
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

static inline uint32_t cherry_now_ms(void)
{
    return (uint32_t)((rt_tick_get() * 1000U) / RT_TICK_PER_SECOND);
}

static inline void cherry_tx_track_busy_max(uint32_t now_ms)
{
    if (cherry_tx_busy_since_ms == 0U) {
        return;
    }
    const uint32_t elapsed = now_ms - cherry_tx_busy_since_ms;
    if (elapsed > rtt_dbg_cherry_tx_busy_max_ms) {
        rtt_dbg_cherry_tx_busy_max_ms = elapsed;
    }
}

static void cherry_tx_epdis_recovery(uint32_t reason, uint32_t elapsed_ms);

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

static void cherry_tx_ring_reset(void)
{
    cherry_tx_ring_head = 0;
    cherry_tx_ring_tail = 0;
    cherry_tx_ring_count = 0;
}

static bool cherry_tx_ring_enqueue(const uint8_t *data, uint32_t len)
{
    if (data == NULL || len == 0 || len > CDC_MAX_MPS) {
        return false;
    }
    if (cherry_tx_ring_count >= CDC_TX_RING_DEPTH) {
        /* Backpressure: ring full, head packet retained (not a silent drop). */
        rtt_dbg_cherry_tx_ring_dropped++;
        return false;
    }

    const uint8_t slot = cherry_tx_ring_head;
    memcpy(cherry_tx_ring[slot], data, len);
    cherry_tx_ring_len[slot] = (uint8_t)len;
    cherry_tx_ring_head = (uint8_t)((slot + 1U) % CDC_TX_RING_DEPTH);
    cherry_tx_ring_count++;
    rtt_dbg_cherry_tx_ring_enqueued++;
    return true;
}

static void cherry_tx_ring_discard_head(void)
{
    if (cherry_tx_ring_count == 0) {
        return;
    }

    const uint8_t slot = cherry_tx_ring_tail;
    cherry_tx_ring_len[slot] = 0;
    cherry_tx_ring_tail = (uint8_t)((slot + 1U) % CDC_TX_RING_DEPTH);
    cherry_tx_ring_count--;
}

static bool cherry_tx_start_write(uint32_t len);

/*
 * Copy consecutive ring slots into cdc_tx_buf without modifying the ring.
 * Returns total bytes (<= max_len); sets *out_slots to slot count merged.
 */
static uint32_t cherry_tx_ring_drain_to_buf(uint32_t max_len, uint8_t *out_slots)
{
    if (out_slots == NULL || max_len == 0 || cherry_tx_ring_count == 0) {
        if (out_slots != NULL) {
            *out_slots = 0;
        }
        return 0;
    }

    uint32_t total = 0;
    uint8_t slots = 0;
    uint8_t idx = cherry_tx_ring_tail;
    uint8_t remaining = cherry_tx_ring_count;

    while (remaining > 0) {
        const uint8_t n = cherry_tx_ring_len[idx];
        if (n == 0 || n > CDC_MAX_MPS) {
            break;
        }
        if (total + (uint32_t)n > max_len) {
            break;
        }
        memcpy(cdc_tx_buf + total, cherry_tx_ring[idx], n);
        total += n;
        slots++;
        idx = (uint8_t)((idx + 1U) % CDC_TX_RING_DEPTH);
        remaining--;
    }

    *out_slots = slots;
    return total;
}

static void cherry_tx_ring_discard_n(uint8_t n)
{
    while (n > 0 && cherry_tx_ring_count > 0) {
        cherry_tx_ring_discard_head();
        n--;
    }
}

static void cherry_tx_kick(void)
{
    rtt_dbg_cherry_tx_kick_calls++;

    while (!cherry_tx_busy && cherry_tx_ring_count > 0) {
        uint8_t slots = 0;
        const uint32_t len = cherry_tx_ring_drain_to_buf(CDC_TX_CHUNK_MAX, &slots);
        if (slots == 0 || len == 0) {
            cherry_tx_ring_discard_head();
            continue;
        }
        if (!cherry_tx_start_write(len)) {
            break;
        }
        cherry_tx_ring_discard_n(slots);
        /* One IN transfer in flight; next chunk starts from bulk_in ISR. */
        break;
    }
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
    if (len == 0 || len > CDC_TX_CHUNK_MAX) {
        return false;
    }

    if (CHERRY_DIEPCTL(CHERRY_CDC_IN_EP_IDX) & CHERRY_DIEPCTL_EPENA) {
        rtt_dbg_cherry_epena_guard_hits++;
        return false;
    }

    cherry_tx_busy = 1;
    cherry_dbg_sync_tx_busy();
    cherry_tx_busy_since_ms = cherry_now_ms();
    cherry_tx_bulk_in_arm_gen = rtt_dbg_cherry_bulk_in_calls;

    if (usbd_ep_start_write(0, CDC_IN_EP, cdc_tx_buf, len) != 0) {
        cherry_tx_busy = 0;
        cherry_dbg_sync_tx_busy();
        cherry_tx_busy_since_ms = 0U;
        rtt_dbg_cherry_tx_start_fail++;
        return false;
    }
    rtt_dbg_cherry_tx_start_ok++;
    return true;
}

static void cherry_tx_epdis_recovery(uint32_t reason, uint32_t elapsed_ms)
{
    rtt_dbg_cherry_recovery_last_reason = reason;
    rtt_dbg_cherry_recovery_last_elapsed_ms = elapsed_ms;
    rtt_dbg_cherry_recovery_last_diepctl = CHERRY_DIEPCTL(CHERRY_CDC_IN_EP_IDX);
    rtt_dbg_cherry_recovery_last_diepint = CHERRY_DIEPINT(CHERRY_CDC_IN_EP_IDX);
    rtt_dbg_cherry_recovery_last_dieptsiz = CHERRY_DIEPTSIZ(CHERRY_CDC_IN_EP_IDX);
    rtt_dbg_cherry_recovery_last_dtxfsts = CHERRY_DTXFSTS(CHERRY_CDC_IN_EP_IDX);
    rtt_dbg_cherry_recovery_last_empmsk = CHERRY_DIEPEMPMSK;
    rtt_dbg_cherry_recovery_last_ring_count = cherry_tx_ring_count;
    rtt_dbg_cherry_recovery_last_bulk_arm = cherry_tx_bulk_in_arm_gen;
    rtt_dbg_cherry_recovery_last_bulk_now = rtt_dbg_cherry_bulk_in_calls;

    if (reason == RTT_DBG_CHERRY_RECOVERY_EPENA_STUCK) {
        rtt_dbg_cherry_recovery_epena_stuck_count++;
        if (elapsed_ms > rtt_dbg_cherry_recovery_max_epena_ms) {
            rtt_dbg_cherry_recovery_max_epena_ms = elapsed_ms;
        }
    } else if (reason == RTT_DBG_CHERRY_RECOVERY_BUSY_TIMEOUT) {
        rtt_dbg_cherry_recovery_busy_timeout_count++;
        if (elapsed_ms > rtt_dbg_cherry_recovery_max_busy_ms) {
            rtt_dbg_cherry_recovery_max_busy_ms = elapsed_ms;
        }
    }

    CHERRY_DIEPEMPMSK &= ~(1UL << CHERRY_CDC_IN_EP_IDX);
    CHERRY_DIEPCTL(CHERRY_CDC_IN_EP_IDX) |= (CHERRY_DIEPCTL_SNAK | CHERRY_DIEPCTL_EPDIS);

    uint32_t timeout = 50000U;
    while ((CHERRY_DIEPINT(CHERRY_CDC_IN_EP_IDX) & CHERRY_DIEPINT_EPDISD) == 0U) {
        if (--timeout == 0U) {
            break;
        }
    }

    CHERRY_DIEPINT(CHERRY_CDC_IN_EP_IDX) = (CHERRY_DIEPINT_EPDISD | CHERRY_DIEPINT_XFRC);
    CHERRY_GRSTCTL = CHERRY_GRSTCTL_TXFFLSH | CHERRY_GRSTCTL_TXFNUM1;
    timeout = 50000U;
    while ((CHERRY_GRSTCTL & CHERRY_GRSTCTL_TXFFLSH) != 0U) {
        if (--timeout == 0U) {
            break;
        }
    }

    cherry_tx_busy = 0;
    cherry_dbg_sync_tx_busy();
    cherry_tx_busy_since_ms = 0U;
    rtt_dbg_cherry_epdis_recovery_count++;
    cherry_tx_kick();
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
        cherry_tx_ring_reset();
        cherry_tx_busy = 0;
        cherry_dbg_sync_tx_busy();
        cherry_tx_busy_since_ms = 0U;
        cherry_epena_stuck_since_ms = 0U;
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
        cherry_tx_ring_reset();
        cherry_tx_busy = 0;
        cherry_dbg_sync_tx_busy();
        cherry_tx_busy_since_ms = 0U;
        cherry_epena_stuck_since_ms = 0U;
        break;
    case USBD_EVENT_DEINIT:
        rtt_dbg_cherry_last_event = RTT_DBG_CHERRY_EVT_DEINIT;
        cherry_configured = false;
        cherry_dbg_sync_configured();
        cherry_dtr = false;
        cherry_rx_queue_reset();
        cherry_tx_ring_reset();
        cherry_tx_busy = 0;
        cherry_dbg_sync_tx_busy();
        cherry_tx_busy_since_ms = 0U;
        cherry_epena_stuck_since_ms = 0U;
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
    cherry_tx_busy_since_ms = 0U;
    cherry_tx_kick();
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

    rt_base_t level = rt_hw_interrupt_disable();
    const uint32_t now_ms = cherry_now_ms();

    if (cherry_configured && !cherry_tx_busy &&
        cherry_tx_ring_count > 0U &&
        (CHERRY_DIEPCTL(CHERRY_CDC_IN_EP_IDX) & CHERRY_DIEPCTL_EPENA)) {
        if (cherry_epena_stuck_since_ms == 0U) {
            cherry_epena_stuck_since_ms = now_ms;
        } else if ((now_ms - cherry_epena_stuck_since_ms) > CHERRY_TX_BUSY_TIMEOUT_MS) {
            const uint32_t elapsed = now_ms - cherry_epena_stuck_since_ms;
            cherry_epena_stuck_since_ms = 0U;
            cherry_tx_epdis_recovery(RTT_DBG_CHERRY_RECOVERY_EPENA_STUCK, elapsed);
        }
    } else {
        cherry_epena_stuck_since_ms = 0U;
    }

    if (cherry_configured && cherry_tx_busy && cherry_tx_busy_since_ms != 0U) {
        cherry_tx_track_busy_max(now_ms);
        const uint32_t elapsed = now_ms - cherry_tx_busy_since_ms;
        if (elapsed > CHERRY_TX_BUSY_TIMEOUT_MS &&
            rtt_dbg_cherry_bulk_in_calls == cherry_tx_bulk_in_arm_gen) {
            cherry_tx_epdis_recovery(RTT_DBG_CHERRY_RECOVERY_BUSY_TIMEOUT, elapsed);
        }
    }

    if (cherry_tx_ring_count > 0 && !cherry_tx_busy) {
        cherry_tx_kick();
    }

    rt_hw_interrupt_enable(level);
}

bool usb_lld_send_rtt(uint8_t ep, const uint8_t *data, uint32_t len)
{
    if (!cherry_configured || ep != 1 || data == NULL || len == 0) {
        return false;
    }
    if (len > CDC_MAX_MPS) {
        len = CDC_MAX_MPS;
    }

    rt_base_t level = rt_hw_interrupt_disable();
    const bool ok = cherry_tx_ring_enqueue(data, len);
    if (ok) {
        cherry_tx_kick();
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
