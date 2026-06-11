/*
 * CherryUSB CDC ACM shim — implements hal_usb_lld_rtt.h compatibility API for UARTDriver.
 *
 * Single OTG_FS_IRQHandler lives in cherryusb_board/usb_dc_glue.c.
 * Production VID/PID 0x1209:0x5741 (matches usb_cdc_rtt.c). Dual CDC only.
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

#define RTT_DBG_DTCM_BSS __attribute__((section(".dtcm_bss.rtt_dbg"), used))

#define CHERRY_USB_OTG_FS_BASE  0x50000000UL

#define CHERRY_OTG_REG32(off) \
    (*(volatile uint32_t *)(CHERRY_USB_OTG_FS_BASE + (off)))

#define CHERRY_DIEPCTL(ep)      CHERRY_OTG_REG32(0x900U + (uint32_t)(ep) * 0x20U)
#define CHERRY_DIEPINT(ep)      CHERRY_OTG_REG32(0x908U + (uint32_t)(ep) * 0x20U)
#define CHERRY_DIEPTSIZ(ep)     CHERRY_OTG_REG32(0x910U + (uint32_t)(ep) * 0x20U)
#define CHERRY_DTXFSTS(ep)      CHERRY_OTG_REG32(0x918U + (uint32_t)(ep) * 0x20U)
#define CHERRY_DCTL             CHERRY_OTG_REG32(0x804U)
#define CHERRY_DSTS             CHERRY_OTG_REG32(0x808U)
#define CHERRY_GINTSTS          CHERRY_OTG_REG32(0x014U)
#define CHERRY_GINTMSK          CHERRY_OTG_REG32(0x018U)
#define CHERRY_DAINT            CHERRY_OTG_REG32(0x818U)
#define CHERRY_DIEPEMPMSK       CHERRY_OTG_REG32(0x834U)
#define CHERRY_GRSTCTL          CHERRY_OTG_REG32(0x010U)

#define CHERRY_DIEPCTL_EPENA    (1UL << 31)
#define CHERRY_DIEPCTL_EPDIS    (1UL << 30)
#define CHERRY_DIEPCTL_SNAK     (1UL << 27)
#define CHERRY_DIEPINT_XFRC     (1UL << 0)
#define CHERRY_DIEPINT_EPDISD   (1UL << 1)
#define CHERRY_DIEPINT_TXFE     (1UL << 7)
#define CHERRY_DIEPTSIZ_XFRSIZ  0x7FFFFUL
#define CHERRY_DIEPTSIZ_PKTCNT  (0x3FFUL << 19)
#define CHERRY_GRSTCTL_TXFFLSH  (1UL << 5)
#define CHERRY_GRSTCTL_TXFNUM(ep) (((uint32_t)(ep) & 0x1FU) << 6)

#define CHERRY_TX_BUSY_TIMEOUT_MS  73U

/* Match ChibiOS dual CDC endpoint layout: CDC0 int EP1/data EP2, CDC1 int EP3/data EP4. */
#define CDC_IN_EP   0x82
#define CDC_OUT_EP  0x02
#define CDC_INT_EP  0x81
#define CDC2_IN_EP  0x84
#define CDC2_OUT_EP 0x04
#define CDC2_INT_EP 0x83
#define CHERRY_CDC_LOGICAL_IN_EP   1U
#define CHERRY_CDC2_LOGICAL_IN_EP  4U
#define CHERRY_CDC_IN_EP_IDX       (CDC_IN_EP & 0x0FU)
#define CHERRY_CDC2_IN_EP_IDX      (CDC2_IN_EP & 0x0FU)

#define CDC_ACM_CHIBIOS_DESCRIPTOR_LEN CDC_ACM_DESCRIPTOR_LEN
#define CDC_ACM_CHIBIOS_DESCRIPTOR_INIT(bFirstInterface, int_ep, out_ep, in_ep, wMaxPacketSize) \
    0x08, USB_DESCRIPTOR_TYPE_INTERFACE_ASSOCIATION, (bFirstInterface), 0x02, \
    USB_DEVICE_CLASS_CDC, CDC_ABSTRACT_CONTROL_MODEL, 0x01, 0x00, \
    0x09, USB_DESCRIPTOR_TYPE_INTERFACE, (bFirstInterface), 0x00, 0x01, \
    USB_DEVICE_CLASS_CDC, CDC_ABSTRACT_CONTROL_MODEL, 0x01, 0x00, \
    0x05, CDC_CS_INTERFACE, CDC_FUNC_DESC_HEADER, WBVAL(CDC_V1_10), \
    0x05, CDC_CS_INTERFACE, CDC_FUNC_DESC_CALL_MANAGEMENT, 0x03, (uint8_t)((bFirstInterface) + 1U), \
    0x04, CDC_CS_INTERFACE, CDC_FUNC_DESC_ABSTRACT_CONTROL_MANAGEMENT, 0x02, \
    0x05, CDC_CS_INTERFACE, CDC_FUNC_DESC_UNION, (bFirstInterface), (uint8_t)((bFirstInterface) + 1U), \
    0x07, USB_DESCRIPTOR_TYPE_ENDPOINT, (int_ep), 0x03, 0x08, 0x00, 0x01, \
    0x09, USB_DESCRIPTOR_TYPE_INTERFACE, (uint8_t)((bFirstInterface) + 1U), 0x00, 0x02, \
    CDC_DATA_INTERFACE_CLASS, 0x00, 0x00, 0x00, \
    0x07, USB_DESCRIPTOR_TYPE_ENDPOINT, (out_ep), 0x02, WBVAL(wMaxPacketSize), 0x00, \
    0x07, USB_DESCRIPTOR_TYPE_ENDPOINT, (in_ep), 0x02, WBVAL(wMaxPacketSize), 0x00

#define USBD_VID           0x1209
#define USBD_PID           0x5741
#define USBD_MAX_POWER     50
#define USBD_LANGID_STRING 0x0409

#define USB_CONFIG_SIZE (9 + CDC_ACM_CHIBIOS_DESCRIPTOR_LEN * 2)
#define CDC_MAX_MPS     64
/*
 * [Cybernetics Ch.15] Extremum seeking: ChibiOS SerialUSB gives CUAV V5 a
 * four-buffer 256B-class output queue before endpoint service.  Keep 64B ring
 * packet granularity, but aggregate up to one ChibiOS-sized transaction before
 * arming DWC2 so MAVFTP/PARAM bursts need fewer XFRC/re-arm cycles.
 */
#define CDC_TX_CHUNK_MAX  256
#define CDC_RX_QUEUE_DEPTH 32
#define CDC_TX_RING_DEPTH  32
/* Match ChibiOS SerialUSB's CUAV V5/F7 scale: 4 logical 256B buffers. */
#define CDC_TX_RING_SOFT_LIMIT 16

static const uint8_t cherry_cdc_descriptor[] = {
    USB_DEVICE_DESCRIPTOR_INIT(USB_2_0, 0xEF, 0x02, 0x01, USBD_VID, USBD_PID, 0x0200, 0x01),
    USB_CONFIG_DESCRIPTOR_INIT(USB_CONFIG_SIZE, 0x04, 0x01, USB_CONFIG_BUS_POWERED, USBD_MAX_POWER),
    CDC_ACM_CHIBIOS_DESCRIPTOR_INIT(0x00, CDC_INT_EP, CDC_OUT_EP, CDC_IN_EP, CDC_MAX_MPS),
    CDC_ACM_CHIBIOS_DESCRIPTOR_INIT(0x02, CDC2_INT_EP, CDC2_OUT_EP, CDC2_IN_EP, CDC_MAX_MPS),
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
USB_NOCACHE_RAM_SECTION USB_MEM_ALIGNX static uint8_t cdc2_read_buf[CDC_MAX_MPS];
USB_NOCACHE_RAM_SECTION USB_MEM_ALIGNX static uint8_t cdc_tx_buf[CDC_TX_CHUNK_MAX];
USB_NOCACHE_RAM_SECTION USB_MEM_ALIGNX static uint8_t cdc2_tx_buf[CDC_MAX_MPS];
static uint8_t cherry_rx_queue[CDC_RX_QUEUE_DEPTH][CDC_MAX_MPS];
static uint8_t cherry2_rx_queue[CDC_RX_QUEUE_DEPTH][CDC_MAX_MPS];
static uint8_t cherry_tx_ring[CDC_TX_RING_DEPTH][CDC_MAX_MPS];
static volatile uint8_t cherry_tx_ring_len[CDC_TX_RING_DEPTH];
static volatile uint8_t cherry_tx_ring_head;
static volatile uint8_t cherry_tx_ring_tail;
static volatile uint8_t cherry_tx_ring_count;
static volatile uint8_t cherry_tx_inflight_slots;
static volatile uint8_t cherry_rx_len[CDC_RX_QUEUE_DEPTH];
static volatile uint8_t cherry2_rx_len[CDC_RX_QUEUE_DEPTH];
static volatile uint8_t cherry_rx_head;
static volatile uint8_t cherry_rx_tail;
static volatile uint8_t cherry_rx_count;
static volatile uint8_t cherry2_rx_head;
static volatile uint8_t cherry2_rx_tail;
static volatile uint8_t cherry2_rx_count;

static volatile bool cherry_configured;
static volatile bool cherry_dtr;
static volatile bool cherry2_dtr;
static volatile uint8_t cherry_tx_busy;
static volatile uint8_t cherry2_tx_busy;
static volatile bool cherry2_tx_needs_zlp;

volatile uint32_t rtt_dbg_usb_init RTT_DBG_DTCM_BSS       = 0;
volatile uint32_t rtt_dbg_usb_usbrst RTT_DBG_DTCM_BSS     = 0;
volatile uint32_t rtt_dbg_usb_enumdne RTT_DBG_DTCM_BSS    = 0;
volatile uint32_t rtt_dbg_usb_setup_stup RTT_DBG_DTCM_BSS = 0;
volatile uint32_t rtt_dbg_cherry_rx_enqueued RTT_DBG_DTCM_BSS = 0;
volatile uint32_t rtt_dbg_cherry_rx_dropped RTT_DBG_DTCM_BSS  = 0;
volatile uint32_t rtt_dbg_cherry_rx_drained RTT_DBG_DTCM_BSS  = 0;
volatile uint32_t rtt_dbg_cherry_usb_reset RTT_DBG_DTCM_BSS   = 0;

/* CDC IN completion chain + link state (GDB / OpenOCD observation only). */
volatile uint32_t rtt_dbg_cherry_bulk_in_calls RTT_DBG_DTCM_BSS     = 0;
volatile uint32_t rtt_dbg_cherry_tx_start_ok RTT_DBG_DTCM_BSS       = 0;
volatile uint32_t rtt_dbg_cherry_tx_start_fail RTT_DBG_DTCM_BSS     = 0;
volatile uint32_t rtt_dbg_cherry_tx_busy_state RTT_DBG_DTCM_BSS     = 0;
volatile uint32_t rtt_dbg_cherry_configured_state RTT_DBG_DTCM_BSS  = 0;
volatile uint32_t rtt_dbg_cherry_last_event RTT_DBG_DTCM_BSS        = 0;
volatile uint32_t rtt_dbg_cherry_init_calls RTT_DBG_DTCM_BSS        = 0;
volatile uint32_t rtt_dbg_cherry_init_skipped RTT_DBG_DTCM_BSS      = 0;
volatile uint32_t rtt_dbg_cherry_bkp_witness RTT_DBG_DTCM_BSS       = 0;
volatile uint32_t rtt_dbg_cherry_tx_ring_enqueued RTT_DBG_DTCM_BSS  = 0;
volatile uint32_t rtt_dbg_cherry_tx_ring_dropped RTT_DBG_DTCM_BSS   = 0;
volatile uint32_t rtt_dbg_cherry_tx_kick_calls RTT_DBG_DTCM_BSS     = 0;
volatile uint32_t rtt_dbg_cherry_epena_guard_hits RTT_DBG_DTCM_BSS    = 0;
volatile uint32_t rtt_dbg_cherry_epdis_recovery_count RTT_DBG_DTCM_BSS = 0;
volatile uint32_t rtt_dbg_cherry_tx_busy_max_ms RTT_DBG_DTCM_BSS      = 0;
volatile uint32_t rtt_dbg_cherry_recovery_busy_timeout_count RTT_DBG_DTCM_BSS = 0;
volatile uint32_t rtt_dbg_cherry_recovery_epena_stuck_count RTT_DBG_DTCM_BSS  = 0;
volatile uint32_t rtt_dbg_cherry_recovery_last_reason RTT_DBG_DTCM_BSS        = 0;
volatile uint32_t rtt_dbg_cherry_recovery_last_elapsed_ms RTT_DBG_DTCM_BSS    = 0;
volatile uint32_t rtt_dbg_cherry_recovery_max_busy_ms RTT_DBG_DTCM_BSS        = 0;
volatile uint32_t rtt_dbg_cherry_recovery_max_epena_ms RTT_DBG_DTCM_BSS       = 0;
volatile uint32_t rtt_dbg_cherry_recovery_last_diepctl RTT_DBG_DTCM_BSS       = 0;
volatile uint32_t rtt_dbg_cherry_recovery_last_diepint RTT_DBG_DTCM_BSS       = 0;
volatile uint32_t rtt_dbg_cherry_recovery_last_dieptsiz RTT_DBG_DTCM_BSS      = 0;
volatile uint32_t rtt_dbg_cherry_recovery_last_dtxfsts RTT_DBG_DTCM_BSS       = 0;
volatile uint32_t rtt_dbg_cherry_recovery_last_empmsk RTT_DBG_DTCM_BSS        = 0;
volatile uint32_t rtt_dbg_cherry_recovery_last_dctl RTT_DBG_DTCM_BSS          = 0;
volatile uint32_t rtt_dbg_cherry_recovery_last_dsts RTT_DBG_DTCM_BSS          = 0;
volatile uint32_t rtt_dbg_cherry_recovery_last_gintsts RTT_DBG_DTCM_BSS       = 0;
volatile uint32_t rtt_dbg_cherry_recovery_last_gintmsk RTT_DBG_DTCM_BSS       = 0;
volatile uint32_t rtt_dbg_cherry_recovery_last_daint RTT_DBG_DTCM_BSS         = 0;
volatile uint32_t rtt_dbg_cherry_recovery_last_ring_count RTT_DBG_DTCM_BSS    = 0;
volatile uint32_t rtt_dbg_cherry_recovery_last_bulk_arm RTT_DBG_DTCM_BSS      = 0;
volatile uint32_t rtt_dbg_cherry_recovery_last_bulk_now RTT_DBG_DTCM_BSS      = 0;
volatile uint32_t rtt_dbg_cherry_tx_inflight_slots_state RTT_DBG_DTCM_BSS     = 0;
volatile uint32_t rtt_dbg_cherry_tx_complete_discards RTT_DBG_DTCM_BSS        = 0;
volatile uint32_t rtt_dbg_cherry_tx_replay_after_recovery RTT_DBG_DTCM_BSS    = 0;
volatile uint32_t rtt_dbg_cherry_tx_completion_assumed RTT_DBG_DTCM_BSS       = 0;
volatile uint32_t rtt_dbg_cherry_tx_completion_assumed_slots RTT_DBG_DTCM_BSS = 0;
volatile uint32_t rtt_dbg_cherry_tx_completion_replayed RTT_DBG_DTCM_BSS      = 0;
volatile uint32_t rtt_dbg_cherry_tx_uncertain_drop_after_recovery RTT_DBG_DTCM_BSS = 0;
volatile uint32_t rtt_dbg_cherry_dtr_state RTT_DBG_DTCM_BSS                   = 0;
volatile uint32_t rtt_dbg_cherry_recovery_paused_dtr RTT_DBG_DTCM_BSS         = 0;
volatile uint32_t rtt_dbg_cherry_dtr_open_kicks RTT_DBG_DTCM_BSS              = 0;
volatile uint32_t rtt_dbg_cherry_dtr_closed_complete RTT_DBG_DTCM_BSS         = 0;
volatile uint32_t rtt_dbg_cherry_dtr_closed_complete_slots RTT_DBG_DTCM_BSS   = 0;
volatile uint32_t rtt_dbg_cherry_dtr_closed_orphan_epena RTT_DBG_DTCM_BSS     = 0;
volatile uint32_t rtt_dbg_cherry_dtr_closed_last_diepctl RTT_DBG_DTCM_BSS     = 0;
volatile uint32_t rtt_dbg_cherry_dtr_closed_last_diepint RTT_DBG_DTCM_BSS     = 0;
volatile uint32_t rtt_dbg_cherry_dtr_closed_last_dieptsiz RTT_DBG_DTCM_BSS    = 0;
volatile uint32_t rtt_dbg_cherry_dtr_closed_last_dtxfsts RTT_DBG_DTCM_BSS     = 0;
volatile uint32_t rtt_dbg_cherry_tx_ring_high_water RTT_DBG_DTCM_BSS          = 0;
volatile uint32_t rtt_dbg_cherry_ring_full_last_diepctl RTT_DBG_DTCM_BSS      = 0;
volatile uint32_t rtt_dbg_cherry_ring_full_last_diepint RTT_DBG_DTCM_BSS      = 0;
volatile uint32_t rtt_dbg_cherry_ring_full_last_dieptsiz RTT_DBG_DTCM_BSS     = 0;
volatile uint32_t rtt_dbg_cherry_ring_full_last_dtxfsts RTT_DBG_DTCM_BSS      = 0;
volatile uint32_t rtt_dbg_cherry_ring_full_last_dtr RTT_DBG_DTCM_BSS          = 0;
volatile uint32_t rtt_dbg_cherry_ring_full_last_busy RTT_DBG_DTCM_BSS         = 0;
volatile uint32_t rtt_dbg_cherry_epena_guard_last_ring_count RTT_DBG_DTCM_BSS = 0;
volatile uint32_t rtt_dbg_cherry_epena_guard_last_diepctl RTT_DBG_DTCM_BSS    = 0;
volatile uint32_t rtt_dbg_cherry_epena_guard_last_diepint RTT_DBG_DTCM_BSS    = 0;
volatile uint32_t rtt_dbg_cherry_epena_guard_last_dieptsiz RTT_DBG_DTCM_BSS   = 0;
volatile uint32_t rtt_dbg_cherry_epena_guard_last_dtxfsts RTT_DBG_DTCM_BSS    = 0;
volatile uint32_t rtt_dbg_cherry_epena_guard_last_dtr RTT_DBG_DTCM_BSS        = 0;
volatile uint32_t rtt_dbg_cherry_epena_guard_last_busy RTT_DBG_DTCM_BSS       = 0;
volatile uint32_t rtt_dbg_cherry_last_dtr_open_ms RTT_DBG_DTCM_BSS            = 0;
volatile uint32_t rtt_dbg_cherry_last_dtr_close_ms RTT_DBG_DTCM_BSS           = 0;
volatile uint32_t rtt_dbg_cherry_last_out_ms RTT_DBG_DTCM_BSS                 = 0;
volatile uint32_t rtt_dbg_cherry_last_bulk_in_ms RTT_DBG_DTCM_BSS             = 0;
volatile uint32_t rtt_dbg_cherry_last_ring_full_ms RTT_DBG_DTCM_BSS           = 0;
volatile uint32_t rtt_dbg_cherry_last_epena_guard_ms RTT_DBG_DTCM_BSS         = 0;
volatile uint32_t rtt_dbg_cherry_last_send_fail_ms RTT_DBG_DTCM_BSS           = 0;
volatile uint32_t rtt_dbg_cherry_last_send_ok_ms RTT_DBG_DTCM_BSS             = 0;
volatile uint32_t rtt_dbg_cherry_last_send_attempt_ms RTT_DBG_DTCM_BSS        = 0;
volatile uint32_t rtt_dbg_cherry_tx_arm_len_hist[6] RTT_DBG_DTCM_BSS          = {0};
volatile uint32_t rtt_dbg_cherry_tx_complete_len_hist[6] RTT_DBG_DTCM_BSS     = {0};
volatile uint32_t rtt_dbg_cherry_tx_arm_slots_hist[5] RTT_DBG_DTCM_BSS        = {0};
volatile uint32_t rtt_dbg_cherry_tx_arm_bytes RTT_DBG_DTCM_BSS                = 0;
volatile uint32_t rtt_dbg_cherry_tx_complete_bytes RTT_DBG_DTCM_BSS           = 0;
volatile uint32_t rtt_dbg_cherry_tx_arm_len_max RTT_DBG_DTCM_BSS              = 0;
volatile uint32_t rtt_dbg_cherry_tx_complete_len_max RTT_DBG_DTCM_BSS         = 0;
volatile uint32_t rtt_dbg_cherry_tx_last_arm_len RTT_DBG_DTCM_BSS             = 0;
volatile uint32_t rtt_dbg_cherry_tx_last_complete_len RTT_DBG_DTCM_BSS        = 0;
volatile uint32_t rtt_dbg_cherry_tx_last_arm_slots RTT_DBG_DTCM_BSS           = 0;
volatile uint32_t rtt_dbg_cherry_tx_zlp_armed RTT_DBG_DTCM_BSS                = 0;
volatile uint32_t rtt_dbg_cherry_tx_zlp_complete RTT_DBG_DTCM_BSS             = 0;
volatile uint32_t rtt_dbg_cherry_tx_zlp_start_fail RTT_DBG_DTCM_BSS           = 0;
#define RTT_DBG_CHERRY_TRACE_DEPTH 64U
volatile uint32_t rtt_dbg_cherry_trace_head RTT_DBG_DTCM_BSS                  = 0;
volatile uint32_t rtt_dbg_cherry_trace_total RTT_DBG_DTCM_BSS                 = 0;
volatile uint32_t rtt_dbg_cherry_trace_kind[RTT_DBG_CHERRY_TRACE_DEPTH] RTT_DBG_DTCM_BSS = {0};
volatile uint32_t rtt_dbg_cherry_trace_len[RTT_DBG_CHERRY_TRACE_DEPTH] RTT_DBG_DTCM_BSS  = {0};
volatile uint32_t rtt_dbg_cherry_trace_slots[RTT_DBG_CHERRY_TRACE_DEPTH] RTT_DBG_DTCM_BSS = {0};
volatile uint32_t rtt_dbg_cherry_trace_ring_count[RTT_DBG_CHERRY_TRACE_DEPTH] RTT_DBG_DTCM_BSS = {0};
volatile uint32_t rtt_dbg_cherry_trace_w0[RTT_DBG_CHERRY_TRACE_DEPTH] RTT_DBG_DTCM_BSS   = {0};
volatile uint32_t rtt_dbg_cherry_trace_w1[RTT_DBG_CHERRY_TRACE_DEPTH] RTT_DBG_DTCM_BSS   = {0};
volatile uint32_t rtt_dbg_cherry_trace_w2[RTT_DBG_CHERRY_TRACE_DEPTH] RTT_DBG_DTCM_BSS   = {0};
volatile uint32_t rtt_dbg_cherry_trace_w3[RTT_DBG_CHERRY_TRACE_DEPTH] RTT_DBG_DTCM_BSS   = {0};

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

static uint32_t cherry_dbg_pack4(const uint8_t *buf, uint32_t len, uint32_t ofs)
{
    uint32_t v = 0;
    for (uint32_t i = 0; i < 4U; i++) {
        const uint32_t p = ofs + i;
        if (buf != NULL && p < len) {
            v |= (uint32_t)buf[p] << (i * 8U);
        }
    }
    return v;
}

static void cherry_dbg_trace(uint32_t kind, const uint8_t *buf, uint32_t len, uint32_t slots)
{
    const uint32_t idx = rtt_dbg_cherry_trace_head & (RTT_DBG_CHERRY_TRACE_DEPTH - 1U);
    rtt_dbg_cherry_trace_kind[idx] = kind;
    rtt_dbg_cherry_trace_len[idx] = len;
    rtt_dbg_cherry_trace_slots[idx] = slots;
    rtt_dbg_cherry_trace_ring_count[idx] = cherry_tx_ring_count;
    rtt_dbg_cherry_trace_w0[idx] = cherry_dbg_pack4(buf, len, 0U);
    rtt_dbg_cherry_trace_w1[idx] = cherry_dbg_pack4(buf, len, 4U);
    rtt_dbg_cherry_trace_w2[idx] = cherry_dbg_pack4(buf, len, 8U);
    rtt_dbg_cherry_trace_w3[idx] = cherry_dbg_pack4(buf, len, 12U);
    rtt_dbg_cherry_trace_head++;
    rtt_dbg_cherry_trace_total++;
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
static usb_rx_callback_t cherry2_rx_cb;
static void *cherry2_rx_arg;

static struct cdc_line_coding cherry_line_coding[2] = {
    {
        .dwDTERate = 921600,
        .bCharFormat = 0,
        .bParityType = 0,
        .bDataBits = 8,
    },
    {
        .dwDTERate = 115200,
        .bCharFormat = 0,
        .bParityType = 0,
        .bDataBits = 8,
    },
};

static struct usbd_interface cherry_intf0;
static struct usbd_interface cherry_intf1;
static struct usbd_interface cherry_intf2;
static struct usbd_interface cherry_intf3;

static void cherry_rx_queue_reset(void)
{
    cherry_rx_head = 0;
    cherry_rx_tail = 0;
    cherry_rx_count = 0;
    cherry2_rx_head = 0;
    cherry2_rx_tail = 0;
    cherry2_rx_count = 0;
}

static void cherry_tx_ring_reset(void)
{
    cherry_tx_ring_head = 0;
    cherry_tx_ring_tail = 0;
    cherry_tx_ring_count = 0;
    cherry_tx_inflight_slots = 0;
    rtt_dbg_cherry_tx_inflight_slots_state = 0;
}

static bool cherry_tx_ring_enqueue(const uint8_t *data, uint32_t len)
{
    if (data == NULL || len == 0 || len > CDC_MAX_MPS) {
        return false;
    }
    if (cherry_tx_ring_count >= CDC_TX_RING_SOFT_LIMIT) {
        /* Backpressure: ring full, head packet retained (not a silent drop). */
        rtt_dbg_cherry_last_ring_full_ms = cherry_now_ms();
        rtt_dbg_cherry_ring_full_last_diepctl = CHERRY_DIEPCTL(CHERRY_CDC_IN_EP_IDX);
        rtt_dbg_cherry_ring_full_last_diepint = CHERRY_DIEPINT(CHERRY_CDC_IN_EP_IDX);
        rtt_dbg_cherry_ring_full_last_dieptsiz = CHERRY_DIEPTSIZ(CHERRY_CDC_IN_EP_IDX);
        rtt_dbg_cherry_ring_full_last_dtxfsts = CHERRY_DTXFSTS(CHERRY_CDC_IN_EP_IDX);
        rtt_dbg_cherry_ring_full_last_dtr = cherry_dtr ? 1U : 0U;
        rtt_dbg_cherry_ring_full_last_busy = cherry_tx_busy;
        rtt_dbg_cherry_tx_ring_dropped++;
        return false;
    }

    const uint8_t slot = cherry_tx_ring_head;
    memcpy(cherry_tx_ring[slot], data, len);
    cherry_tx_ring_len[slot] = (uint8_t)len;
    cherry_dbg_trace(1U, data, len, 1U);
    cherry_tx_ring_head = (uint8_t)((slot + 1U) % CDC_TX_RING_DEPTH);
    cherry_tx_ring_count++;
    if (cherry_tx_ring_count > rtt_dbg_cherry_tx_ring_high_water) {
        rtt_dbg_cherry_tx_ring_high_water = cherry_tx_ring_count;
    }
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
static bool cherry_tx_start_zlp(void);

static uint8_t cherry_len_hist_index(uint32_t len)
{
    if (len <= 32U) {
        return 0;
    }
    if (len <= 63U) {
        return 1;
    }
    if (len == 64U) {
        return 2;
    }
    if (len <= 128U) {
        return 3;
    }
    if (len <= 192U) {
        return 4;
    }
    return 5;
}

static uint8_t cherry_slots_hist_index(uint8_t slots)
{
    if (slots == 0U) {
        return 0;
    }
    if (slots <= 3U) {
        return slots;
    }
    return 4;
}

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

    /* Match ChibiOS SerialUSB: DTR is advisory; configured USB may carry MAVLink. */
    if (!cherry_configured) {
        return;
    }

    while (!cherry_tx_busy && cherry_tx_ring_count > 0) {
        if (CHERRY_DIEPCTL(CHERRY_CDC_IN_EP_IDX) & CHERRY_DIEPCTL_EPENA) {
            /*
             * [Cybernetics Ch.4] Closed-loop: mirror ChibiOS SerialUSB's
             * endpoint-idle gate before taking the next output buffer.  The
             * shared cdc_tx_buf must not be overwritten while DWC2 still owns
             * the previous IN transfer, even if the software busy flag has
             * already been cleared by a delayed or partial event path.
             */
            rtt_dbg_cherry_last_epena_guard_ms = cherry_now_ms();
            rtt_dbg_cherry_epena_guard_last_ring_count = cherry_tx_ring_count;
            rtt_dbg_cherry_epena_guard_last_diepctl = CHERRY_DIEPCTL(CHERRY_CDC_IN_EP_IDX);
            rtt_dbg_cherry_epena_guard_last_diepint = CHERRY_DIEPINT(CHERRY_CDC_IN_EP_IDX);
            rtt_dbg_cherry_epena_guard_last_dieptsiz = CHERRY_DIEPTSIZ(CHERRY_CDC_IN_EP_IDX);
            rtt_dbg_cherry_epena_guard_last_dtxfsts = CHERRY_DTXFSTS(CHERRY_CDC_IN_EP_IDX);
            rtt_dbg_cherry_epena_guard_last_dtr = cherry_dtr ? 1U : 0U;
            rtt_dbg_cherry_epena_guard_last_busy = cherry_tx_busy;
            rtt_dbg_cherry_epena_guard_hits++;
            break;
        }

        uint8_t slots = 0;
        const uint32_t len = cherry_tx_ring_drain_to_buf(CDC_TX_CHUNK_MAX, &slots);
        if (slots == 0 || len == 0) {
            cherry_tx_ring_discard_head();
            continue;
        }
        if (!cherry_tx_start_write(len)) {
            break;
        }
        cherry_dbg_trace(2U, cdc_tx_buf, len, slots);
        cherry_tx_inflight_slots = slots;
        rtt_dbg_cherry_tx_inflight_slots_state = slots;
        rtt_dbg_cherry_tx_last_arm_slots = slots;
        rtt_dbg_cherry_tx_arm_slots_hist[cherry_slots_hist_index(slots)]++;
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

static void cherry2_rx_enqueue_from_isr(const uint8_t *data, uint32_t len)
{
    if (len == 0 || len > CDC_MAX_MPS || data == NULL) {
        return;
    }

    if (cherry2_rx_count >= CDC_RX_QUEUE_DEPTH) {
        rtt_dbg_cherry_rx_dropped++;
        return;
    }

    const uint8_t slot = cherry2_rx_head;
    memcpy(cherry2_rx_queue[slot], data, len);
    cherry2_rx_len[slot] = (uint8_t)len;
    cherry2_rx_head = (uint8_t)((slot + 1U) % CDC_RX_QUEUE_DEPTH);
    cherry2_rx_count++;
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

static bool cherry2_rx_dequeue(uint8_t *data, uint32_t *len)
{
    if (data == NULL || len == NULL || cherry2_rx_count == 0) {
        return false;
    }

    const uint8_t slot = cherry2_rx_tail;
    const uint8_t n = cherry2_rx_len[slot];
    if (n == 0 || n > CDC_MAX_MPS) {
        cherry2_rx_tail = (uint8_t)((slot + 1U) % CDC_RX_QUEUE_DEPTH);
        cherry2_rx_count--;
        *len = 0;
        return true;
    }

    memcpy(data, cherry2_rx_queue[slot], n);
    cherry2_rx_len[slot] = 0;
    cherry2_rx_tail = (uint8_t)((slot + 1U) % CDC_RX_QUEUE_DEPTH);
    cherry2_rx_count--;
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
        rtt_dbg_cherry_last_epena_guard_ms = cherry_now_ms();
        rtt_dbg_cherry_epena_guard_last_ring_count = cherry_tx_ring_count;
        rtt_dbg_cherry_epena_guard_last_diepctl = CHERRY_DIEPCTL(CHERRY_CDC_IN_EP_IDX);
        rtt_dbg_cherry_epena_guard_last_diepint = CHERRY_DIEPINT(CHERRY_CDC_IN_EP_IDX);
        rtt_dbg_cherry_epena_guard_last_dieptsiz = CHERRY_DIEPTSIZ(CHERRY_CDC_IN_EP_IDX);
        rtt_dbg_cherry_epena_guard_last_dtxfsts = CHERRY_DTXFSTS(CHERRY_CDC_IN_EP_IDX);
        rtt_dbg_cherry_epena_guard_last_dtr = cherry_dtr ? 1U : 0U;
        rtt_dbg_cherry_epena_guard_last_busy = cherry_tx_busy;
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
    rtt_dbg_cherry_tx_last_arm_len = len;
    rtt_dbg_cherry_tx_arm_bytes += len;
    rtt_dbg_cherry_tx_arm_len_hist[cherry_len_hist_index(len)]++;
    if (len > rtt_dbg_cherry_tx_arm_len_max) {
        rtt_dbg_cherry_tx_arm_len_max = len;
    }
    return true;
}

static bool cherry_tx_start_zlp(void)
{
    if (!cherry_configured || (CHERRY_DIEPCTL(CHERRY_CDC_IN_EP_IDX) & CHERRY_DIEPCTL_EPENA)) {
        return false;
    }

    cherry_tx_busy = 1;
    cherry_dbg_sync_tx_busy();
    cherry_tx_busy_since_ms = cherry_now_ms();
    cherry_tx_bulk_in_arm_gen = rtt_dbg_cherry_bulk_in_calls;

    if (usbd_ep_start_write(0, CDC_IN_EP, NULL, 0) != 0) {
        cherry_tx_busy = 0;
        cherry_dbg_sync_tx_busy();
        cherry_tx_busy_since_ms = 0U;
        rtt_dbg_cherry_tx_zlp_start_fail++;
        return false;
    }

    rtt_dbg_cherry_tx_zlp_armed++;
    rtt_dbg_cherry_tx_last_arm_len = 0;
    rtt_dbg_cherry_tx_arm_len_hist[0]++;
    return true;
}

static bool cherry2_tx_start_write(const uint8_t *data, uint32_t len)
{
    if (!cherry_configured || data == NULL || len == 0U || len > CDC_MAX_MPS ||
        cherry2_tx_busy ||
        (CHERRY_DIEPCTL(CHERRY_CDC2_IN_EP_IDX) & CHERRY_DIEPCTL_EPENA)) {
        return false;
    }

    memcpy(cdc2_tx_buf, data, len);
    cherry2_tx_busy = 1U;
    cherry2_tx_needs_zlp = ((len & (CDC_MAX_MPS - 1U)) == 0U);

    if (usbd_ep_start_write(0, CDC2_IN_EP, cdc2_tx_buf, len) != 0) {
        cherry2_tx_busy = 0U;
        cherry2_tx_needs_zlp = false;
        return false;
    }
    return true;
}

static void cherry2_tx_complete(uint32_t nbytes)
{
    (void)nbytes;
    cherry2_tx_busy = 0U;
    if (cherry2_tx_needs_zlp) {
        cherry2_tx_needs_zlp = false;
        if ((CHERRY_DIEPCTL(CHERRY_CDC2_IN_EP_IDX) & CHERRY_DIEPCTL_EPENA) == 0U) {
            cherry2_tx_busy = 1U;
            if (usbd_ep_start_write(0, CDC2_IN_EP, NULL, 0) != 0) {
                cherry2_tx_busy = 0U;
            }
        }
    }
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
    rtt_dbg_cherry_recovery_last_dctl = CHERRY_DCTL;
    rtt_dbg_cherry_recovery_last_dsts = CHERRY_DSTS;
    rtt_dbg_cherry_recovery_last_gintsts = CHERRY_GINTSTS;
    rtt_dbg_cherry_recovery_last_gintmsk = CHERRY_GINTMSK;
    rtt_dbg_cherry_recovery_last_daint = CHERRY_DAINT;
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
    CHERRY_GRSTCTL = CHERRY_GRSTCTL_TXFFLSH | CHERRY_GRSTCTL_TXFNUM(CHERRY_CDC_IN_EP_IDX);
    timeout = 50000U;
    while ((CHERRY_GRSTCTL & CHERRY_GRSTCTL_TXFFLSH) != 0U) {
        if (--timeout == 0U) {
            break;
        }
    }

    cherry_tx_busy = 0;
    cherry_dbg_sync_tx_busy();
    cherry_tx_busy_since_ms = 0U;
    if (cherry_tx_inflight_slots > 0U) {
        /*
         * [Cybernetics Ch.4] Closed-loop: ChibiOS never replays a SerialUSB
         * output buffer after it has been handed to the USB peripheral.  Once
         * EP1 was armed, host-visible state is uncertain after a forced disable
         * and FIFO flush: replay can duplicate a MAVLink header if the host had
         * already received the packet but XFRC was lost/delayed.  Drop the
         * uncertain in-flight slots instead; MAVLink/FTP can retry missing
         * frames, but it cannot repair duplicated bytes inside one frame.
         */
        if (reason == RTT_DBG_CHERRY_RECOVERY_BUSY_TIMEOUT &&
            ((rtt_dbg_cherry_recovery_last_diepint & CHERRY_DIEPINT_TXFE) != 0U) &&
            ((rtt_dbg_cherry_recovery_last_dieptsiz & CHERRY_DIEPTSIZ_XFRSIZ) == 0U)) {
            rtt_dbg_cherry_tx_completion_assumed++;
            rtt_dbg_cherry_tx_completion_assumed_slots += cherry_tx_inflight_slots;
        } else {
            rtt_dbg_cherry_tx_uncertain_drop_after_recovery += cherry_tx_inflight_slots;
        }
        rtt_dbg_cherry_tx_complete_discards += cherry_tx_inflight_slots;
        cherry_tx_ring_discard_n(cherry_tx_inflight_slots);
        cherry_tx_inflight_slots = 0U;
        rtt_dbg_cherry_tx_inflight_slots_state = 0U;
    }
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
        rtt_dbg_cherry_dtr_state = cherry_dtr ? 1U : 0U;
        cherry_rx_queue_reset();
        cherry_tx_ring_reset();
        cherry_tx_busy = 0;
        cherry2_tx_busy = 0;
        cherry2_tx_needs_zlp = false;
        cherry_dbg_sync_tx_busy();
        cherry_tx_busy_since_ms = 0U;
        cherry_epena_stuck_since_ms = 0U;
        usbd_ep_start_read(busid, CDC_OUT_EP, cdc_read_buf, sizeof(cdc_read_buf));
        usbd_ep_start_read(busid, CDC2_OUT_EP, cdc2_read_buf, sizeof(cdc2_read_buf));
        break;
    case USBD_EVENT_RESET:
        rtt_dbg_usb_usbrst++;
        rtt_dbg_cherry_usb_reset++;
        rtt_dbg_cherry_last_event = RTT_DBG_CHERRY_EVT_RESET;
        cherry_configured = false;
        cherry_dbg_sync_configured();
        cherry_dtr = false;
        cherry2_dtr = false;
        rtt_dbg_cherry_dtr_state = 0U;
        cherry_rx_queue_reset();
        cherry_tx_ring_reset();
        cherry_tx_busy = 0;
        cherry2_tx_busy = 0;
        cherry2_tx_needs_zlp = false;
        cherry_dbg_sync_tx_busy();
        cherry_tx_busy_since_ms = 0U;
        cherry_epena_stuck_since_ms = 0U;
        break;
    case USBD_EVENT_DEINIT:
        rtt_dbg_cherry_last_event = RTT_DBG_CHERRY_EVT_DEINIT;
        cherry_configured = false;
        cherry_dbg_sync_configured();
        cherry_dtr = false;
        cherry2_dtr = false;
        rtt_dbg_cherry_dtr_state = 0U;
        cherry_rx_queue_reset();
        cherry_tx_ring_reset();
        cherry_tx_busy = 0;
        cherry2_tx_busy = 0;
        cherry2_tx_needs_zlp = false;
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
    /* Defer MAVLink parsing to usb_lld_poll_rtt() (main loop, IRQ masked there). */
    rtt_dbg_cherry_last_out_ms = cherry_now_ms();
    if (ep == CDC_OUT_EP) {
        cherry_rx_enqueue_from_isr(cdc_read_buf, nbytes);
        usbd_ep_start_read(busid, CDC_OUT_EP, cdc_read_buf, sizeof(cdc_read_buf));
    } else if (ep == CDC2_OUT_EP) {
        cherry2_rx_enqueue_from_isr(cdc2_read_buf, nbytes);
        usbd_ep_start_read(busid, CDC2_OUT_EP, cdc2_read_buf, sizeof(cdc2_read_buf));
    }
}

void usbd_cdc_acm_bulk_in(uint8_t busid, uint8_t ep, uint32_t nbytes)
{
    (void)busid;
    if (ep == CDC2_IN_EP) {
        cherry2_tx_complete(nbytes);
        return;
    }
    rtt_dbg_cherry_bulk_in_calls++;
    rtt_dbg_cherry_last_bulk_in_ms = cherry_now_ms();
    rtt_dbg_cherry_tx_last_complete_len = nbytes;
    rtt_dbg_cherry_tx_complete_bytes += nbytes;
    rtt_dbg_cherry_tx_complete_len_hist[cherry_len_hist_index(nbytes)]++;
    if (nbytes > rtt_dbg_cherry_tx_complete_len_max) {
        rtt_dbg_cherry_tx_complete_len_max = nbytes;
    }

    rt_base_t level = rt_hw_interrupt_disable();
    const bool completed_full_packet =
        (nbytes > 0U) && ((nbytes & (CDC_MAX_MPS - 1U)) == 0U);
    if (cherry_tx_inflight_slots > 0U) {
        rtt_dbg_cherry_tx_complete_discards += cherry_tx_inflight_slots;
        cherry_tx_ring_discard_n(cherry_tx_inflight_slots);
        cherry_tx_inflight_slots = 0U;
        rtt_dbg_cherry_tx_inflight_slots_state = 0U;
    }
    cherry_tx_busy = 0;
    cherry_dbg_sync_tx_busy();
    cherry_tx_busy_since_ms = 0U;
    if (nbytes == 0U) {
        rtt_dbg_cherry_tx_zlp_complete++;
    }
    if (cherry_tx_ring_count > 0U) {
        cherry_tx_kick();
    } else if (completed_full_packet) {
        /*
         * [Cybernetics Ch.4] Closed-loop: mirror ChibiOS sduDataTransmitted().
         * A final full-size bulk packet needs a ZLP so the host-side CDC/TTY
         * layer can complete the transfer instead of waiting for more bytes.
         */
        (void)cherry_tx_start_zlp();
    }
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

static struct usbd_endpoint cherry2_ep_out = {
    .ep_addr = CDC2_OUT_EP,
    .ep_cb = usbd_cdc_acm_bulk_out,
};

static struct usbd_endpoint cherry2_ep_in = {
    .ep_addr = CDC2_IN_EP,
    .ep_cb = usbd_cdc_acm_bulk_in,
};

void usbd_cdc_acm_set_line_coding(uint8_t busid, uint8_t intf, struct cdc_line_coding *line_coding)
{
    (void)busid;
    rtt_dbg_usb_setup_stup++;
    if (line_coding != NULL) {
        cherry_line_coding[(intf >= 2U) ? 1U : 0U] = *line_coding;
    }
}

void usbd_cdc_acm_get_line_coding(uint8_t busid, uint8_t intf, struct cdc_line_coding *line_coding)
{
    (void)busid;
    rtt_dbg_usb_setup_stup++;
    if (line_coding != NULL) {
        *line_coding = cherry_line_coding[(intf >= 2U) ? 1U : 0U];
    }
}

void usbd_cdc_acm_set_dtr(uint8_t busid, uint8_t intf, bool dtr)
{
    (void)busid;
    (void)intf;
    rtt_dbg_usb_setup_stup++;
    const uint8_t idx = (intf >= 2U) ? 1U : 0U;
    if (idx == 1U) {
        cherry2_dtr = dtr;
        return;
    }
    const bool was_dtr = cherry_dtr;
    cherry_dtr = dtr;
    rtt_dbg_cherry_dtr_state = dtr ? 1U : 0U;
    if (dtr) {
        rtt_dbg_cherry_last_dtr_open_ms = cherry_now_ms();
    } else {
        rtt_dbg_cherry_last_dtr_close_ms = cherry_now_ms();
    }

    if (!was_dtr && dtr) {
        rt_base_t level = rt_hw_interrupt_disable();
        const uint32_t now_ms = cherry_now_ms();
        if (cherry_tx_busy && cherry_tx_busy_since_ms != 0U) {
            cherry_tx_busy_since_ms = now_ms;
        }
        cherry_epena_stuck_since_ms = 0U;
        rtt_dbg_cherry_dtr_open_kicks++;
        cherry_tx_kick();
        rt_hw_interrupt_enable(level);
    }
}

static void cherry_cdc_stack_init(void)
{
    usbd_desc_register(0, cherry_cdc_descriptor);
    usbd_add_interface(0, usbd_cdc_acm_init_intf(0, &cherry_intf0));
    usbd_add_interface(0, usbd_cdc_acm_init_intf(0, &cherry_intf1));
    usbd_add_interface(0, usbd_cdc_acm_init_intf(0, &cherry_intf2));
    usbd_add_interface(0, usbd_cdc_acm_init_intf(0, &cherry_intf3));
    usbd_add_endpoint(0, &cherry_ep_out);
    usbd_add_endpoint(0, &cherry_ep_in);
    usbd_add_endpoint(0, &cherry2_ep_out);
    usbd_add_endpoint(0, &cherry2_ep_in);
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

    while (cherry2_rx_cb != NULL) {
        uint8_t local[CDC_MAX_MPS];
        uint32_t len = 0;

        rt_base_t level = rt_hw_interrupt_disable();
        const bool have_rx = cherry2_rx_dequeue(local, &len);
        rt_hw_interrupt_enable(level);

        if (!have_rx) {
            break;
        }
        if (len > 0) {
            cherry2_rx_cb(local, len, cherry2_rx_arg);
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
    if (!cherry_configured || data == NULL || len == 0) {
        return false;
    }
    if (len > CDC_MAX_MPS) {
        len = CDC_MAX_MPS;
    }
    if (ep == CHERRY_CDC2_LOGICAL_IN_EP) {
        rt_base_t level = rt_hw_interrupt_disable();
        const bool ok = cherry2_tx_start_write(data, len);
        rt_hw_interrupt_enable(level);
        return ok;
    }
    if (ep != CHERRY_CDC_LOGICAL_IN_EP) {
        return false;
    }

    rt_base_t level = rt_hw_interrupt_disable();
    const uint32_t now_ms = cherry_now_ms();
    rtt_dbg_cherry_last_send_attempt_ms = now_ms;
    const bool ok = cherry_tx_ring_enqueue(data, len);
    if (ok) {
        rtt_dbg_cherry_last_send_ok_ms = now_ms;
        cherry_tx_kick();
    } else {
        rtt_dbg_cherry_last_send_fail_ms = now_ms;
    }
    rt_hw_interrupt_enable(level);
    return ok;
}

uint32_t usb_lld_txspace_rtt(uint8_t ep)
{
    if (!cherry_configured) {
        return 0;
    }
    if (ep == CHERRY_CDC2_LOGICAL_IN_EP) {
        return cherry2_tx_busy ? 0U : CDC_MAX_MPS;
    }
    if (ep != CHERRY_CDC_LOGICAL_IN_EP) {
        return 0;
    }

    rt_base_t level = rt_hw_interrupt_disable();
    const uint8_t used = cherry_tx_ring_count;
    rt_hw_interrupt_enable(level);

    if (used >= CDC_TX_RING_SOFT_LIMIT) {
        return 0;
    }
    return (uint32_t)(CDC_TX_RING_SOFT_LIMIT - used) * CDC_MAX_MPS;
}

void usb_lld_set_rx_callback(usb_rx_callback_t cb, void *arg)
{
    cherry_rx_cb = cb;
    cherry_rx_arg = arg;
}

void usb_lld_set_rx_callback_idx(uint8_t idx, usb_rx_callback_t cb, void *arg)
{
    if (idx == 0U) {
        cherry_rx_cb = cb;
        cherry_rx_arg = arg;
    } else if (idx == 1U) {
        cherry2_rx_cb = cb;
        cherry2_rx_arg = arg;
    }
}

void usb_lld_rearm_cdc_out(void)
{
    if (cherry_configured) {
        usbd_ep_start_read(0, CDC_OUT_EP, cdc_read_buf, sizeof(cdc_read_buf));
    }
}

void usb_lld_rearm_cdc_out_idx(uint8_t idx)
{
    if (!cherry_configured) {
        return;
    }
    if (idx == 0U) {
        usbd_ep_start_read(0, CDC_OUT_EP, cdc_read_buf, sizeof(cdc_read_buf));
    } else if (idx == 1U) {
        usbd_ep_start_read(0, CDC2_OUT_EP, cdc2_read_buf, sizeof(cdc2_read_buf));
    }
}

bool usb_lld_is_configured_rtt(void)
{
    return cherry_configured;
}

bool usb_lld_is_configured_idx_rtt(uint8_t idx)
{
    (void)idx;
    return cherry_configured;
}

bool usb_lld_get_connected_rtt(void)
{
    /* Match native stack: enumerated/configured, not gated on DTR (DTR tracked for class requests). */
    return cherry_configured;
}

bool usb_lld_get_connected_idx_rtt(uint8_t idx)
{
    (void)idx;
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
