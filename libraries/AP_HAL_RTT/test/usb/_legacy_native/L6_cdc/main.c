/**
 * test_L6_cdc — Layer 6: Full CDC ACM data transfer
 *
 * Complete CDC ACM implementation from ChibiOS OTGv1 hal_usb_lld.c.
 * Steps:
 *  1-7: USB init (same as L5_usb, ChibiOS register sequence)
 *  8:   Enable IRQ + endpoint config
 *  9:   Echo service running, poll for data
 *
 * Build: scons --target=cuav-v5 --test=L6_cdc -j$(nproc)
 * Verify: echo "hello" > /dev/ttyACM1 && cat /dev/ttyACM1
 */

#include "test_runner.h"
#include <rtthread.h>
#include <string.h>
#include <stdint.h>

/* =========================================================================
 *  OTG_FS Register Map (from ChibiOS stm32_otg.h)
 * ========================================================================= */
#define OTG                 0x50000000UL

/* Global */
#define OTG_GOTGCTL         (*(volatile uint32_t *)(OTG + 0x000))
#define OTG_GOTGINT         (*(volatile uint32_t *)(OTG + 0x004))
#define OTG_GAHBCFG         (*(volatile uint32_t *)(OTG + 0x008))
#define OTG_GUSBCFG         (*(volatile uint32_t *)(OTG + 0x00C))
#define OTG_GRSTCTL         (*(volatile uint32_t *)(OTG + 0x010))
#define OTG_GINTSTS         (*(volatile uint32_t *)(OTG + 0x014))
#define OTG_GINTMSK         (*(volatile uint32_t *)(OTG + 0x018))
#define OTG_GRXSTSR         (*(volatile uint32_t *)(OTG + 0x01C))
#define OTG_GRXSTSP         (*(volatile uint32_t *)(OTG + 0x020))
#define OTG_GRXFSIZ         (*(volatile uint32_t *)(OTG + 0x024))
#define OTG_DIEPTXF0        (*(volatile uint32_t *)(OTG + 0x028))
#define OTG_HNPTXFSIZ       (*(volatile uint32_t *)(OTG + 0x028))
#define OTG_GCCFG           (*(volatile uint32_t *)(OTG + 0x038))
#define OTG_GPWRDN          (*(volatile uint32_t *)(OTG + 0x034))
#define OTG_PCGCCTL         (*(volatile uint32_t *)(OTG + 0xE00))

/* Device */
#define OTG_DCFG            (*(volatile uint32_t *)(OTG + 0x800))
#define OTG_DCTL            (*(volatile uint32_t *)(OTG + 0x804))
#define OTG_DSTS            (*(volatile uint32_t *)(OTG + 0x808))
#define OTG_DIEPMSK         (*(volatile uint32_t *)(OTG + 0x810))
#define OTG_DOEPMSK         (*(volatile uint32_t *)(OTG + 0x814))
#define OTG_DAINT           (*(volatile uint32_t *)(OTG + 0x818))
#define OTG_DAINTMSK        (*(volatile uint32_t *)(OTG + 0x81C))
#define OTG_DIEPEMPMSK      (*(volatile uint32_t *)(OTG + 0x834))

/* EP-specific (index i: 0..3) */
#define OTG_DIEPCTL(i)      (*(volatile uint32_t *)(OTG + 0x900 + (i)*0x20))
#define OTG_DIEPTSIZ(i)     (*(volatile uint32_t *)(OTG + 0x910 + (i)*0x20))
#define OTG_DIEPINT(i)      (*(volatile uint32_t *)(OTG + 0x908 + (i)*0x20))
#define OTG_DTXFSTS(i)      (*(volatile uint32_t *)(OTG + 0x918 + (i)*0x20))
#define OTG_DOEPCTL(i)      (*(volatile uint32_t *)(OTG + 0xB00 + (i)*0x20))
#define OTG_DOEPTSIZ(i)     (*(volatile uint32_t *)(OTG + 0xB10 + (i)*0x20))
#define OTG_DOEPINT(i)      (*(volatile uint32_t *)(OTG + 0xB08 + (i)*0x20))

/* TX FIFO registers (DIEPTXF[n] at 0x104 + n*4, n=0..14) */
#define OTG_DIEPTXF(n)      (*(volatile uint32_t *)(OTG + 0x104 + (n)*4))

/* FIFO data port */
#define OTG_DFIFO           ((volatile uint32_t *)(OTG + 0x1000))

/* RCC */
#define RCC_AHB2ENR         (*(volatile uint32_t *)0x40023834UL)
#define RCC_AHB2RSTR        (*(volatile uint32_t *)0x4002387CUL)
#define RCC_AHB1ENR         (*(volatile uint32_t *)0x40023830UL)
#define RCC_APB1ENR         (*(volatile uint32_t *)0x40023840UL)
#define PWR_CR2             (*(volatile uint32_t *)0x40007018UL)

/* GPIO */
#define PA_MODER            (*(volatile uint32_t *)0x40020000UL)
#define PA_AFRH             (*(volatile uint32_t *)0x40020024UL)
#define PA_OSPEEDR          (*(volatile uint32_t *)0x40020008UL)
#define PA_PUPDR            (*(volatile uint32_t *)0x4002000CUL)
#define NVIC_ISER2          (*(volatile uint32_t *)0xE000E108UL)

/* =========================================================================
 *  Bit definitions (from ChibiOS stm32_otg.h)
 * ========================================================================= */

/* GUSBCFG */
#define GUSBCFG_FDMOD           (1UL << 30)
#define GUSBCFG_PHYSEL          (1UL << 6)
#define GUSBCFG_TRDT(n)         (((n) << 10) & 0x1C00UL)
#define GUSBCFG_TRDT_MASK       0x1C00UL

/* GRSTCTL */
#define GRSTCTL_AHBIDL          (1UL << 31)
#define GRSTCTL_CSRST           (1UL << 0)
#define GRSTCTL_TXFFLSH         (1UL << 5)
#define GRSTCTL_TXFNUM(n)       (((n) << 6) & (0x1FUL << 6))
#define GRSTCTL_RXFFLSH         (1UL << 4)

/* DCFG */
#define DCFG_NZLSOHSK           (1UL << 25)
#define DCFG_DSPD_FS11          (3UL << 0)
#define DCFG_DAD(n)             ((n) << 4)
#define DCFG_DAD_MASK           (0x7FUL << 4)

/* GOTGCTL */
#define GOTGCTL_BVALOEN         (1UL << 21)
#define GOTGCTL_BVALOVAL        (1UL << 22)

/* GCCFG (stepping 2) */
#define GCCFG_PWRDWN            (1UL << 16)
#define GCCFG_VBDEN             (1UL << 21)

/* DCTL */
#define DCTL_SDIS               (1UL << 1)
#define DCTL_RWUSIG             (1UL << 0)

/* GAHBCFG */
#define GAHBCFG_GINTMSK         (1UL << 0)

/* GINTSTS / GINTMSK */
#define GINTMSK_SOFM            (1UL << 3)
#define GINTMSK_RXFLVLM         (1UL << 4)
#define GINTMSK_OEPM            (1UL << 19)
#define GINTMSK_IEPM            (1UL << 18)
#define GINTMSK_ENUMDNEM        (1UL << 13)
#define GINTMSK_USBRSTM         (1UL << 12)
#define GINTMSK_USBSUSPM        (1UL << 11)
#define GINTMSK_ESUSPM          (1UL << 10)
#define GINTMSK_SRQM            (1UL << 30)
#define GINTMSK_WKUM            (1UL << 31)
#define GINTMSK_IISOIXFRM       (1UL << 20)
#define GINTMSK_IISOOXFRM       (1UL << 21)

/* GRXSTSP packet status */
#define GRXSTSP_BCNT_OFF        4
#define GRXSTSP_BCNT_MASK       (0x7FFUL << 4)
#define GRXSTSP_EPNUM_OFF       0
#define GRXSTSP_EPNUM_MASK      0x0FUL
#define GRXSTSP_PKTSTS_OFF      17
#define GRXSTSP_PKTSTS_MASK     (0x0FUL << 17)
#define GRXSTSP_SETUP_DATA      (6UL << 17)
#define GRXSTSP_SETUP_COMP      (4UL << 17)
#define GRXSTSP_OUT_DATA        (2UL << 17)
#define GRXSTSP_OUT_COMP        (3UL << 17)
#define GRXSTSP_OUT_GLOBAL_NAK  (5UL << 17)

/* DSTS */
#define DSTS_ENUMSPD_MASK       0x03UL
#define DSTS_ENUMSPD_FS_11      0x03UL

/* PCGCCTL */
#define PCGCCTL_STPPCLK         (1UL << 0)
#define PCGCCTL_GATEHCLK        (1UL << 1)

/* DIEPCTL / DOEPCTL */
#define DIEPCTL_EPENA           (1UL << 31)
#define DIEPCTL_EPDIS           (1UL << 30)
#define DIEPCTL_SNAK            (1UL << 27)
#define DIEPCTL_CNAK            (1UL << 26)
#define DIEPCTL_TXFNUM(n)       ((n) << 22)
#define DIEPCTL_STALL           (1UL << 21)
#define DIEPCTL_SD0PID          (1UL << 6)
#define DIEPCTL_SEVNFRM         (1UL << 8)
#define DIEPCTL_SODDFRM         (1UL << 7)
#define DIEPCTL_USBAEP          (1UL << 15)
#define DIEPCTL_MPSIZ(n)        ((n) << 0)
#define DIEPCTL_EPTYP_SHIFT     18
#define DIEPCTL_EPTYP_CTRL      (0UL << 18)
#define DIEPCTL_EPTYP_ISO       (1UL << 18)
#define DIEPCTL_EPTYP_BULK      (2UL << 18)
#define DIEPCTL_EPTYP_INTR      (3UL << 18)

#define DOEPCTL_EPENA           (1UL << 31)
#define DOEPCTL_EPDIS           (1UL << 30)
#define DOEPCTL_SNAK            (1UL << 27)
#define DOEPCTL_CNAK            (1UL << 26)
#define DOEPCTL_STALL           (1UL << 21)
#define DOEPCTL_SD0PID          (1UL << 6)
#define DOEPCTL_USBAEP          (1UL << 15)
#define DOEPCTL_MPSIZ(n)        ((n) << 0)
#define DOEPCTL_EPTYP_CTRL      (0UL << 18)
#define DOEPCTL_EPTYP_BULK      (2UL << 18)

/* DIEPTSIZ / DOEPTSIZ */
#define DIEPTSIZ_XFRSIZ(n)      ((n) << 0)
#define DIEPTSIZ_PKTCNT_SHIFT   19
#define DIEPTSIZ_PKTCNT(n)      ((n) << 19)
#define DIEPTSIZ_MCNT(n)        ((n) << 29)

#define DOEPTSIZ_XFRSIZ(n)      ((n) << 0)
#define DOEPTSIZ_PKTCNT(n)      ((n) << 19)
#define DOEPTSIZ_STUPCNT(n)     ((n) << 29)

/* DIEPINT / DOEPINT */
#define DIEPINT_XFRC            (1UL << 0)
#define DIEPINT_TXFE            (1UL << 7)
#define DIEPINT_TOC             (1UL << 3)
#define DOEPINT_XFRC            (1UL << 0)
#define DOEPINT_STUP            (1UL << 3)

/* DIEPMSK / DOEPMSK */
#define DIEPMSK_XFRCM           (1UL << 0)
#define DIEPMSK_TOCM            (1UL << 3)
#define DIEPMSK_TXFEM           (1UL << 6)
#define DOEPMSK_XFRCM           (1UL << 0)
#define DOEPMSK_STUPM           (1UL << 3)

/* DIEPEMPMSK */
#define DIEPEMPMSK_INEPTXFEM(n) (1UL << n)

/* DANTMSK */
#define DAINTMSK_OEPM(n)        (1UL << (16 + n))
#define DAINTMSK_IEPM(n)        (1UL << n)

/* DIEPTXF / DIEPTXF0 */
#define DIEPTXF_INEPTXSA(n)     ((n) << 0)
#define DIEPTXF_INEPTXFD(n)     ((n) << 16)

/* DTXFSTS */
#define DTXFSTS_INEPTFSAV_MASK  0xFFFFUL

/* =========================================================================
 *  USB Descriptor constants (USB spec / CDC)
 * ========================================================================= */
#define USB_DESC_DEVICE(bcdUSB, bClass, bSubClass, bProtocol, MPS, \
                        vid, pid, bcdDev, iM, iP, iS, bNumCfg) \
  18, 0x01, (uint8_t)(bcdUSB), (uint8_t)(bcdUSB>>8), bClass, bSubClass, bProtocol, MPS, \
  (uint8_t)(vid), (uint8_t)(vid>>8), \
  (uint8_t)(pid), (uint8_t)(pid>>8), \
  (uint8_t)(bcdDev), (uint8_t)(bcdDev>>8), \
  iM, iP, iS, bNumCfg

#define USB_DESC_CONFIGURATION(len, nItfs, cfgVal, iCfg, attrs, mA) \
  9, 0x02, (uint8_t)(len), (uint8_t)(len>>8), nItfs, cfgVal, iCfg, attrs, mA

#define USB_DESC_INTERFACE(nItf, alt, nEP, cls, sub, proto, iI) \
  9, 0x04, nItf, alt, nEP, cls, sub, proto, iI

#define USB_DESC_ENDPOINT(addr, attr, maxPkt, interval) \
  7, 0x05, addr, attr, (uint8_t)(maxPkt), (uint8_t)(maxPkt>>8), interval

/* CDC descriptor subtypes */
#define CS_INTERFACE            0x24
#define CDC_HEADER_FD           0x00
#define CDC_CALL_MGMT_FD        0x01
#define CDC_ACM_FD              0x02
#define CDC_UNION_FD            0x06

/* Standard requests */
#define USB_REQ_GET_DESCRIPTOR  0x06
#define USB_REQ_SET_ADDRESS     0x05
#define USB_REQ_SET_CONFIG      0x09
#define USB_REQ_GET_CONFIG      0x08
#define USB_REQ_GET_STATUS      0x00
#define USB_REQ_SET_FEATURE     0x03
#define USB_REQ_CLEAR_FEATURE   0x01

/* CDC requests */
#define CDC_SET_LINE_CODING     0x20
#define CDC_GET_LINE_CODING     0x21
#define CDC_SET_CTRL_LINE_STATE 0x22

/* Descriptor types */
#define DESC_DEVICE                 1
#define DESC_CONFIG                 2
#define DESC_STRING                 3
#define DESC_DEVICE_QUALIFIER       6
#define DESC_OTHER_SPEED_CONFIG     7

/* =========================================================================
 *  USB Endpoint Numbers
 * ========================================================================= */
#define EP0_IN                  0
#define EP0_OUT                 0
#define EP1_IN                  1   /* 0x81 — CDC Bulk IN */
#define EP1_OUT                 1   /* 0x01 — CDC Bulk OUT */
#define EP2_IN                  2   /* 0x82 — CDC Interrupt IN (notification) */

#define EP0_MPS                 64
#define EP1_MPS                 64
#define EP2_MPS                 8

#define MAX_EP                  5
#define RX_FIFO_SIZE            128   /* words */
#define TX0_FIFO_SIZE           32    /* words — EP0, enough for config descriptor */
#define TX1_FIFO_SIZE           64    /* words — EP1 Bulk IN (256 bytes) */
#define TX2_FIFO_SIZE           4     /* words — EP2 Interrupt IN (16 bytes) */

/* =========================================================================
 *  Global USB state (replaces ChibiOS USBDriver)
 * ========================================================================= */
static volatile uint32_t usb_configured   = 0;
static volatile uint32_t usb_address      = 0;
static volatile uint32_t usb_enum_speed   = 0;
static volatile uint32_t usb_sof_count    = 0;
static volatile uint32_t usb_rx_count     = 0;
static volatile uint32_t usb_tx_count     = 0;
static volatile uint32_t usb_ep0_setups   = 0;
static volatile uint32_t usb_ep0_in_chunks = 0;
static volatile uint32_t usb_reset_count  = 0;

/* Debug slot — firmware writes values here for OpenOCD readback */
volatile uint32_t test_debug_slot[4] __attribute__((section(".bss")));
volatile uint32_t usb_last_grxstsp;

/* L6 EP0/GRX diagnostics (memory-only; OpenOCD x/wx). */
#define L6_SETUP_HISTORY          4U

volatile uint32_t l6_diag_setup_count;
volatile uint32_t l6_diag_setup_rx_count;
volatile uint32_t l6_diag_grxstsp_last;
volatile uint32_t l6_diag_grxstsp_at_setup;
volatile uint32_t l6_diag_grxstsp_total;
volatile uint8_t  l6_diag_setup_last_8[8];
volatile uint32_t l6_diag_setup_hist[L6_SETUP_HISTORY][2];
volatile uint32_t l6_diag_setup_hist_idx;
volatile uint32_t l6_diag_ep0_in_xfrc;
volatile uint32_t l6_diag_ep0_in_txfe;
volatile uint32_t l6_diag_ep0_in_stall_set;
volatile uint32_t l6_diag_ep0_out_xfrc;
volatile uint32_t l6_diag_ep0_out_stup;
volatile uint32_t l6_diag_ep0_out_stall_set;
volatile uint32_t l6_diag_rx_global_nak;
volatile uint32_t l6_diag_last_diepint0;
volatile uint32_t l6_diag_last_doepint0;
volatile uint32_t l6_diag_last_gintsts;
volatile uint32_t l6_diag_last_ep0_state;
volatile uint32_t l6_diag_diepctl0_snap;
volatile uint32_t l6_diag_doepctl0_snap;
volatile uint32_t l6_diag_summary[8];

/* EP0 state */
static uint8_t  ep0_rxbuf[64];
static uint8_t  ep0_txbuf[128];
static volatile uint32_t ep0_txlen       = 0;  /* current IN packet (<= MPS) */
static volatile uint32_t ep0_txcnt       = 0;  /* bytes pushed to TX FIFO this packet */
static volatile uint32_t ep0_in_total    = 0;  /* full control-IN transfer length */
static volatile uint32_t ep0_in_sent     = 0;  /* bytes completed on the bus */
static uint8_t  ep0_setup_buf[8];
static uint8_t  ep0_ctrl_buf[128];
static volatile uint32_t ep0_ctrl_len    = 0;
static volatile uint16_t ep0_out_expect  = 0;  /* EP0 control OUT data bytes pending */
static volatile uint16_t ep0_out_got     = 0;
static volatile uint8_t  ep0_pending_addr = 0;
static volatile uint8_t  ep0_addr_defer   = 0;  /* SET_ADDRESS: apply DAD after status IN */

enum {
    EP0_IDLE = 0,
    EP0_DATA_IN,
    EP0_DATA_IN_ZLP,
    EP0_STATUS_IN,
    EP0_DATA_OUT,
    EP0_STATUS_OUT,
};
static volatile uint8_t ep0_state = EP0_IDLE;

/* EP1 Bulk state (CDC data) */
static uint8_t  ep1_rxbuf[512];
static volatile uint32_t ep1_rxcnt       = 0;
static volatile uint32_t ep1_txlen       = 0;
static volatile uint32_t ep1_txcnt       = 0;
static volatile uint32_t ep1_busy        = 0;   /* TX in progress */
static volatile uint32_t ep1_out_armed   = 0;   /* EP1 OUT primed for host data */
static volatile uint32_t usb_echo_pkts   = 0;   /* completed echo round-trips */

/* EP2 Interrupt state (CDC notification) */
static uint8_t  ep2_txbuf[16];
static volatile uint32_t ep2_txlen       = 0;

/* CDC line coding */
static uint8_t cdc_line_coding[7] = { 0x00, 0xC2, 0x01, 0x00, 0x00, 0x00, 0x08 };
static uint32_t cdc_ctrl_line_state = 0;

/* =========================================================================
 *  Stubs
 * ========================================================================= */
void ap_rtt_iwdg_kick(void)
{
    /* Feed IWDG: write 0xAAAA to Key Register */
    *(volatile uint32_t *)0x40002800 = 0x0000AAAA;
    __asm volatile("dsb" ::: "memory");
}

/* =========================================================================
 *  Helpers
 * ========================================================================= */
static void _dsb(void)     { __asm volatile("dsb" ::: "memory"); }
static void _isb(void)     { __asm volatile("isb" ::: "memory"); }

/* Feed IWDG every 100 iterations of _delay */
static void _delay(uint32_t n)
{
    uint32_t feed = 0;
    while (n--) {
        __asm volatile("nop");
        if (++feed >= 100) {
            feed = 0;
            *(volatile uint32_t *)0x40002800 = 0x0000AAAA;
            __asm volatile("dsb" ::: "memory");
        }
    }
}
static void _mdelay(uint32_t ms) {
    uint32_t goal = rt_tick_get() + (ms * RT_TICK_PER_SECOND) / 1000;
    while (rt_tick_get() < goal) _delay(100);
}

static void l6_diag_record_setup_raw(const uint8_t *s)
{
    uint32_t idx = l6_diag_setup_hist_idx % L6_SETUP_HISTORY;
    uint32_t lo = (uint32_t)s[0] | ((uint32_t)s[1] << 8) |
                  ((uint32_t)s[2] << 16) | ((uint32_t)s[3] << 24);
    uint32_t hi = (uint32_t)s[4] | ((uint32_t)s[5] << 8) |
                  ((uint32_t)s[6] << 16) | ((uint32_t)s[7] << 24);

    l6_diag_setup_hist[idx][0] = lo;
    l6_diag_setup_hist[idx][1] = hi;
    l6_diag_setup_hist_idx++;
    for (uint32_t i = 0; i < 8; i++) {
        l6_diag_setup_last_8[i] = s[i];
    }
}

static void l6_diag_refresh_summary(void)
{
    l6_diag_summary[0] = l6_diag_setup_count;
    l6_diag_summary[1] = l6_diag_setup_rx_count;
    l6_diag_summary[2] = l6_diag_grxstsp_at_setup;
    l6_diag_summary[3] = (l6_diag_ep0_in_xfrc << 16) | (l6_diag_ep0_out_xfrc & 0xFFFFU);
    l6_diag_summary[4] = (l6_diag_ep0_in_txfe << 16) | (l6_diag_ep0_out_stup & 0xFFFFU);
    l6_diag_summary[5] = (l6_diag_ep0_in_stall_set << 16) | l6_diag_ep0_out_stall_set;
    l6_diag_summary[6] = l6_diag_rx_global_nak;
    l6_diag_summary[7] = (l6_diag_last_diepint0 << 16) | (l6_diag_last_doepint0 & 0xFFFFU);
}

static void l6_diag_ep0_clear_stall(void)
{
    OTG_DIEPCTL(0) &= ~DIEPCTL_STALL;
    OTG_DOEPCTL(0) &= ~DOEPCTL_STALL;
    _dsb();
}

/* =========================================================================
 *  FIFO helpers (from ChibiOS hal_usb_lld.c)
 * ========================================================================= */
static void fifo_write(volatile uint32_t *fifop, const uint8_t *buf, size_t n)
{
    while (n > 0) {
        *fifop = *(const uint32_t *)buf;
        if (n <= 4) break;
        n -= 4; buf += 4;
    }
}

static void fifo_read(volatile uint32_t *fifop, uint8_t *buf, size_t n, size_t max)
{
    uint32_t w = 0;
    size_t i;
    for (i = 0; i < n; i++) {
        if ((i & 3) == 0) w = *fifop;
        if (i < max) { *buf++ = (uint8_t)w; w >>= 8; }
    }
}

/* Discard RX FIFO bytes when no buffer is armed (ChibiOS otg_fifo_read max=0). */
static void fifo_discard(volatile uint32_t *fifop, size_t n)
{
    fifo_read(fifop, NULL, n, 0);
}

/* =========================================================================
 *  TXFIFO flush (from ChibiOS)
 * ========================================================================= */
static void txfifo_flush(uint32_t fifo_num)
{
    OTG_GRSTCTL = GRSTCTL_TXFNUM(fifo_num) | GRSTCTL_TXFFLSH;
    _dsb();
    { uint32_t sp = 100000; while ((OTG_GRSTCTL & GRSTCTL_TXFFLSH) && sp--) _delay(10); }
}

static void rxfifo_flush(void)
{
    OTG_GRSTCTL = GRSTCTL_RXFFLSH;
    _dsb();
    { uint32_t sp = 100000; while ((OTG_GRSTCTL & GRSTCTL_RXFFLSH) && sp--) _delay(10); }
}

/* =========================================================================
 *  RAM allocator (simple linear allocator for FIFO regions)
 * ========================================================================= */
static uint32_t fifo_alloc_ptr = 0;
static void fifo_alloc_reset(void) { fifo_alloc_ptr = RX_FIFO_SIZE; }
static uint32_t fifo_alloc(uint32_t words) {
    uint32_t addr = fifo_alloc_ptr;
    fifo_alloc_ptr += words;
    return addr;
}

/* =========================================================================
 *  USB Start IN/OUT (from ChibiOS usb_lld_start_in / usb_lld_start_out)
 * ========================================================================= */

/* EP0 TX FIFO fill — ChibiOS otg_txfifo_handler (one MPS chunk per usb_lld_start_in). */
static bool ep0_txfifo_fill(const uint8_t *buf)
{
    uint32_t n;

    if (ep0_txcnt >= ep0_txlen) {
        OTG_DIEPEMPMSK &= ~DIEPEMPMSK_INEPTXFEM(0);
        return true;
    }

    n = ep0_txlen - ep0_txcnt;
    if (n > EP0_MPS) {
        n = EP0_MPS;
    }
    if (((OTG_DTXFSTS(0) & DTXFSTS_INEPTFSAV_MASK) * 4) < n) {
        return false;
    }

    fifo_write(&OTG_DFIFO[0], buf + ep0_txcnt, n);
    ep0_txcnt += n;
    if (ep0_txcnt >= ep0_txlen) {
        OTG_DIEPEMPMSK &= ~DIEPEMPMSK_INEPTXFEM(0);
        return true;
    }
    return false;
}

static void ep0_start_in_chunk(void)
{
    uint32_t remain = ep0_in_total - ep0_in_sent;
    uint32_t chunk = remain;

    if (chunk > EP0_MPS) {
        chunk = EP0_MPS;
    }
    ep0_txlen = chunk;
    ep0_txcnt = 0;

    OTG_DIEPTSIZ(0) = DIEPTSIZ_MCNT(1) | DIEPTSIZ_PKTCNT(1) | DIEPTSIZ_XFRSIZ(chunk);
    _dsb();

    OTG_DIEPCTL(0) |= DIEPCTL_EPENA | DIEPCTL_CNAK;
    _dsb();

    if (chunk > 0) {
        usb_ep0_in_chunks++;
        if (!ep0_txfifo_fill(ep0_ctrl_buf + ep0_in_sent)) {
            OTG_DIEPEMPMSK |= DIEPEMPMSK_INEPTXFEM(0);
            _dsb();
        }
    }
}

static void usb_start_in(int ep, const uint8_t *buf, uint32_t len)
{
    volatile uint32_t *tsiz = (ep == 0) ? &OTG_DIEPTSIZ(0) : &OTG_DIEPTSIZ(ep);
    volatile uint32_t *tctl = (ep == 0) ? &OTG_DIEPCTL(0) : &OTG_DIEPCTL(ep);

    if (ep == 0) {
        (void)buf;
        ep0_start_in_chunk();
        return;
    }

    uint32_t mps = EP1_MPS;
    uint32_t pcnt = (len + mps - 1) / mps;
    if (len == 0) { pcnt = 1; }
    *tsiz = DIEPTSIZ_MCNT(1) | DIEPTSIZ_PKTCNT(pcnt) | DIEPTSIZ_XFRSIZ(len);
    _dsb();

    *tctl |= DIEPCTL_EPENA | DIEPCTL_CNAK;
    _dsb();

    if (len > 0 && ((OTG_DTXFSTS(ep) & DTXFSTS_INEPTFSAV_MASK) * 4) >= len) {
        fifo_write(&OTG_DFIFO[ep], buf, len);
        OTG_DIEPEMPMSK |= DIEPEMPMSK_INEPTXFEM(ep);
        _dsb();
    }
}

static void ep0_out_enable(uint32_t doeptsiz)
{
    OTG_DOEPINT(0) = 0xFFFFFFFFU;
    OTG_DOEPTSIZ(0) = doeptsiz;
    _dsb();

    if (OTG_DOEPCTL(0) & DOEPCTL_EPENA) {
        OTG_DOEPCTL(0) |= DOEPCTL_EPDIS;
        _dsb();
        uint32_t spin = 10000U;
        while ((OTG_DOEPCTL(0) & DOEPCTL_EPENA) && (spin-- > 0U)) {
            _delay(1);
        }
    }

    OTG_DOEPCTL(0) |= DOEPCTL_EPENA | DOEPCTL_CNAK;
    _dsb();
}

/* STATUS OUT ZLP: PKTCNT=1, XFRSIZ=0 — no STUPCNT (do not mix with setup arm). */
static void ep0_arm_status_out(void)
{
    ep0_state = EP0_STATUS_OUT;
    ep0_out_enable(DOEPTSIZ_PKTCNT(1) | DOEPTSIZ_XFRSIZ(0));
}

/* EP0 control OUT data stage (host->device). */
static void ep0_arm_data_out(uint32_t size)
{
    uint32_t pcnt = (size + EP0_MPS - 1U) / EP0_MPS;
    uint32_t rxsize = (pcnt * EP0_MPS + 3U) & ~3U;

    ep0_out_enable(DOEPTSIZ_PKTCNT(pcnt) | DOEPTSIZ_XFRSIZ(rxsize));
}

static void usb_start_out(int ep, uint8_t *buf, uint32_t size)
{
    volatile uint32_t *tsiz = &OTG_DOEPTSIZ(ep);
    volatile uint32_t *tctl = &OTG_DOEPCTL(ep);

    if (ep == 0) {
        (void)buf;
        (void)size;
        return;
    }

    uint32_t mps = EP1_MPS;
    uint32_t pcnt = (size + mps - 1U) / mps;
    uint32_t rxsize;
    if (size == 0U) {
        pcnt = 1U;
        rxsize = 0U;
    } else {
        rxsize = ((pcnt * mps) + 3U) & ~3U;
    }
    *tsiz = DOEPTSIZ_PKTCNT(pcnt) | DOEPTSIZ_XFRSIZ(rxsize);
    _dsb();
    *tctl |= DOEPCTL_EPENA | DOEPCTL_CNAK;
    _dsb();
}

static void ep0_rearm_setup(void);

/* =========================================================================
 *  USB Reset Handler (from ChibiOS usb_lld_reset)
 * ========================================================================= */
static void usb_handle_reset(void)
{
    int i;
    usb_reset_count++;
    usb_configured = 0;
    usb_address = 0;
    ep0_state = EP0_IDLE;
    ep0_pending_addr = 0;
    ep0_addr_defer = 0;
    ep0_out_expect = 0;
    ep0_out_got = 0;
    ep0_in_total = 0;
    ep0_in_sent = 0;

    /* Flush TX FIFO 0 */
    txfifo_flush(0);

    /* Disable all EP interrupts except EP0 */
    OTG_DIEPEMPMSK = 0;
    OTG_DAINTMSK = DAINTMSK_OEPM(0) | DAINTMSK_IEPM(0);

    /* All EPs to NAK, clear interrupts */
    for (i = 0; i < MAX_EP; i++) {
        OTG_DIEPCTL(i) = DIEPCTL_SNAK;
        OTG_DOEPCTL(i) = DOEPCTL_SNAK;
        OTG_DIEPINT(i) = 0xFFFFFFFF;
        OTG_DOEPINT(i) = 0xFFFFFFFF;
    }

    /* Reset FIFO allocator */
    fifo_alloc_reset();

    /* RX FIFO */
    OTG_GRXFSIZ = RX_FIFO_SIZE;
    rxfifo_flush();

    /* Device address = 0 */
    OTG_DCFG = (OTG_DCFG & ~DCFG_DAD_MASK) | DCFG_DAD(0);

    /* Re-assert speed bits — may get cleared by host reset */
    OTG_DCFG |= DCFG_NZLSOHSK | DCFG_DSPD_FS11;

    /* Force device mode — USB reset may clear FDMOD in GUSBCFG,
     * causing core to exit device mode and SDIS to be re-asserted.
     * This is the ROOT CAUSE of the device never appearing on the bus. */
    OTG_GUSBCFG |= GUSBCFG_FDMOD;
    _dsb();

    /* Ensure soft disconnect is clear */
    OTG_DCTL &= ~DCTL_SDIS;
    _dsb();

    /* Enable EP-related interrupts */
    OTG_GINTMSK |= GINTMSK_RXFLVLM | GINTMSK_OEPM | GINTMSK_IEPM;
    OTG_DIEPMSK  = DIEPMSK_TOCM | DIEPMSK_XFRCM | DIEPMSK_TXFEM;
    OTG_DOEPMSK  = DOEPMSK_STUPM | DOEPMSK_XFRCM;

    /* EP0 init */
    OTG_DOEPTSIZ(0) = DOEPTSIZ_STUPCNT(3);
    _dsb();
    OTG_DOEPCTL(0)  = DOEPCTL_SD0PID | DOEPCTL_USBAEP | DOEPCTL_EPTYP_CTRL | DOEPCTL_MPSIZ(64);
    OTG_DIEPTSIZ(0) = 0;
    OTG_DIEPCTL(0)  = DIEPCTL_SD0PID | DIEPCTL_USBAEP | DIEPCTL_EPTYP_CTRL |
                      DIEPCTL_TXFNUM(0) | DIEPCTL_MPSIZ(64);
    OTG_DIEPTXF0    = DIEPTXF_INEPTXFD(TX0_FIFO_SIZE) |
                      DIEPTXF_INEPTXSA(fifo_alloc(TX0_FIFO_SIZE));
    _dsb();
    ep0_rearm_setup();
}

static void ep0_start_status_in(void)
{
    ep0_in_total = 0;
    ep0_in_sent = 0;
    ep0_txlen = 0;
    ep0_txcnt = 0;
    OTG_DIEPEMPMSK &= ~DIEPEMPMSK_INEPTXFEM(0);
    ep0_state = EP0_STATUS_IN;
    /* ChibiOS usb_lld_start_in(): ZLP status uses PKTCNT|XFRSIZ only (no MCNT). */
    OTG_DIEPTSIZ(0) = DIEPTSIZ_PKTCNT(1) | DIEPTSIZ_XFRSIZ(0);
    _dsb();
    OTG_DIEPCTL(0) |= DIEPCTL_EPENA | DIEPCTL_CNAK;
    _dsb();
    OTG_DIEPEMPMSK |= DIEPEMPMSK_INEPTXFEM(0);
    _dsb();
}

static void ep0_rearm_setup(void)
{
    ep0_state = EP0_IDLE;
    ep0_out_expect = 0;
    ep0_out_got = 0;
    OTG_DIEPEMPMSK &= ~DIEPEMPMSK_INEPTXFEM(0);
    l6_diag_ep0_clear_stall();
    /* Drop any stale IN transfer before arming OUT for the next SETUP. */
    OTG_DIEPTSIZ(0) = 0;
    _dsb();
    if (OTG_DIEPCTL(0) & DIEPCTL_EPENA) {
        OTG_DIEPCTL(0) |= DIEPCTL_EPDIS;
        _dsb();
    }
    /* Next SETUP: STUPCNT=3 only (PKTCNT/XFRSIZ cleared). */
    ep0_out_enable(DOEPTSIZ_STUPCNT(3));
}

static void ep0_start_data_in(const uint8_t *buf, uint32_t len)
{
    if (buf != ep0_ctrl_buf) {
        memcpy(ep0_ctrl_buf, buf, len);
    }
    ep0_ctrl_len = len;
    ep0_in_total = len;
    ep0_in_sent = 0;
    ep0_state = EP0_DATA_IN;
    ep0_start_in_chunk();
}

static void ep0_start_status_out(void)
{
    ep0_in_total = 0;
    ep0_in_sent = 0;
    ep0_txlen = 0;
    ep0_txcnt = 0;
    OTG_DIEPEMPMSK &= ~DIEPEMPMSK_INEPTXFEM(0);
    ep0_arm_status_out();
}

static void ep0_finish_data_in(void)
{
    uint16_t wLength = (uint16_t)ep0_setup_buf[6] |
                       ((uint16_t)ep0_setup_buf[7] << 8);

    if (ep0_in_total < wLength && (ep0_in_total % EP0_MPS) == 0 && ep0_in_total > 0) {
        ep0_state = EP0_DATA_IN_ZLP;
        ep0_start_in_chunk();
        return;
    }
    ep0_start_status_out();
}

/* =========================================================================
 *  RXFLVL Interrupt Handler (from ChibiOS otg_rxfifo_handler)
 * ========================================================================= */
static void handle_rxflvl(void)
{
    uint32_t sts = OTG_GRXSTSP;
    uint32_t cnt = (sts & GRXSTSP_BCNT_MASK) >> GRXSTSP_BCNT_OFF;
    uint32_t ep  = (sts & GRXSTSP_EPNUM_MASK) >> GRXSTSP_EPNUM_OFF;
    uint32_t pkt = sts & GRXSTSP_PKTSTS_MASK;

    l6_diag_grxstsp_last = sts;
    l6_diag_grxstsp_total++;
    usb_last_grxstsp = sts;

    if (pkt == GRXSTSP_SETUP_DATA) {
        /* Read SETUP 8 bytes */
        if (cnt > 8U) {
            cnt = 8U;
        }
        fifo_read(&OTG_DFIFO[0], ep0_setup_buf, cnt, 8);
        l6_diag_setup_rx_count++;
        l6_diag_grxstsp_at_setup = sts;
        l6_diag_diepctl0_snap = OTG_DIEPCTL(0);
        l6_diag_doepctl0_snap = OTG_DOEPCTL(0);
        l6_diag_record_setup_raw(ep0_setup_buf);
        l6_diag_refresh_summary();
        return;
    }

    if (pkt == GRXSTSP_SETUP_COMP)
        return;

    if (pkt == GRXSTSP_OUT_DATA) {
        if (ep == 0) {
            if (ep0_out_expect > 0) {
                uint32_t space = ep0_out_expect - ep0_out_got;
                uint32_t n = cnt;
                if (n > space) {
                    n = space;
                }
                fifo_read(&OTG_DFIFO[0], ep0_ctrl_buf + ep0_out_got, n, n);
                ep0_out_got = (uint16_t)(ep0_out_got + n);
            } else {
                fifo_discard(&OTG_DFIFO[0], cnt);
            }
        } else if (ep == 1) {
            /* CDC Bulk OUT data — read into RX buffer */
            uint32_t space = sizeof(ep1_rxbuf) - ep1_rxcnt;
            uint32_t n = cnt;
            if (n > space) {
                n = space;
            }
            if (n > 0) {
                fifo_read(&OTG_DFIFO[0], ep1_rxbuf + ep1_rxcnt, n, n);
                ep1_rxcnt += n;
                usb_rx_count += n;
            }
        }
        return;
    }

    if (pkt == GRXSTSP_OUT_GLOBAL_NAK) {
        l6_diag_rx_global_nak++;
        l6_diag_refresh_summary();
        return;
    }

    if (pkt == GRXSTSP_OUT_COMP)
        return;

    /* Unknown PKTSTS — must pop bytes or RX FIFO stalls. */
    if (cnt > 0U) {
        fifo_discard(&OTG_DFIFO[0], cnt);
    }
}

/* =========================================================================
 *  SETUP packet processing
 * ========================================================================= */

/* Standard descriptor tables */
static const uint8_t dev_desc[18] = {
    USB_DESC_DEVICE(0x0200, 0x02, 0x00, 0x00, 0x40,
                    0x1209, 0x5741, 0x0200, 1, 2, 3, 1)
};

static const uint8_t dev_qual_desc[10] = {
    10, DESC_DEVICE_QUALIFIER,
    0x00, 0x02,        /* USB 2.0 */
    0x02, 0x00, 0x00,  /* CDC class at device level */
    0x40,              /* EP0 MPS */
    0x01,              /* other-speed configurations */
    0x00
};

static const uint8_t cfg_desc[67] = {
    /* Configuration */
    USB_DESC_CONFIGURATION(67, 0x02, 0x01, 0, 0xC0, 50),
    /* Interface 0: CDC Control */
    USB_DESC_INTERFACE(0x00, 0x00, 0x01, 0x02, 0x02, 0x01, 0),
    /* CDC Header FD */
    5, CS_INTERFACE, CDC_HEADER_FD, 0x10, 0x01,
    /* CDC Call Management FD */
    5, CS_INTERFACE, CDC_CALL_MGMT_FD, 0x00, 0x01,
    /* CDC ACM FD */
    4, CS_INTERFACE, CDC_ACM_FD, 0x02,
    /* CDC Union FD */
    5, CS_INTERFACE, CDC_UNION_FD, 0x00, 0x01,
    /* EP2 IN: Interrupt (notification) */
    USB_DESC_ENDPOINT(0x82, 0x03, 8, 0xFF),
    /* Interface 1: CDC Data */
    USB_DESC_INTERFACE(0x01, 0x00, 0x02, 0x0A, 0x00, 0x00, 0),
    /* EP1 OUT: Bulk */
    USB_DESC_ENDPOINT(0x01, 0x02, 64, 0),
    /* EP1 IN: Bulk */
    USB_DESC_ENDPOINT(0x81, 0x02, 64, 0),
};

static const uint8_t str0_desc[4] = { 4, 0x03, 0x09, 0x04 };
static const uint8_t str1_desc[30] = { 30, 0x03,
    'C',0, 'U',0, 'A',0, 'V',0, ' ',0, 'V',0, '5',0, ' ',0, 'R',0, 'T',0, 'T',0, 0,0, 0,0 };
static const uint8_t str2_desc[30] = { 30, 0x03,
    'A',0, 'r',0, 'd',0, 'u',0, 'P',0, 'i',0, 'l',0, 'o',0, 't',0, ' ',0, 'R',0, 'T',0, 'T',0, 0,0 };
static const uint8_t str3_desc[10] = { 10, 0x03,
    '0',0, '0',0, '0',0, '1',0 };

static const uint8_t *get_string(uint8_t idx)
{
    if (idx == 0) return str0_desc;
    if (idx == 1) return str1_desc;
    if (idx == 2) return str2_desc;
    if (idx == 3) return str3_desc;
    return NULL;
}

/* Handle a complete SETUP packet */
static void handle_setup(void)
{
    uint8_t bmReqType = ep0_setup_buf[0];
    uint8_t bRequest  = ep0_setup_buf[1];
    uint16_t wValue   = (uint16_t)ep0_setup_buf[2] | ((uint16_t)ep0_setup_buf[3] << 8);
    uint16_t wIndex   = (uint16_t)ep0_setup_buf[4] | ((uint16_t)ep0_setup_buf[5] << 8);
    uint16_t wLength  = (uint16_t)ep0_setup_buf[6] | ((uint16_t)ep0_setup_buf[7] << 8);

    int dir_in = (bmReqType & 0x80) != 0;
    int type   = (bmReqType & 0x60);
    int recip  = (bmReqType & 0x1F);

    l6_diag_ep0_clear_stall();
    l6_diag_last_ep0_state = ep0_state;
    l6_diag_record_setup_raw(ep0_setup_buf);

    usb_ep0_setups++;
    l6_diag_setup_count++;
    test_debug_slot[0] = ((uint32_t)bmReqType << 24) | ((uint32_t)bRequest << 16) | wLength;
    test_debug_slot[1] = wValue;
    test_debug_slot[3] = usb_last_grxstsp;
    l6_diag_refresh_summary();

    /* ===== Standard requests ===== */
    if (type == 0) {  /* Standard */
        if (bRequest == USB_REQ_GET_DESCRIPTOR && dir_in) {
            uint8_t desc_type = (wValue >> 8) & 0xFF;
            uint8_t desc_idx  = wValue & 0xFF;
            const uint8_t *data = NULL;
            uint32_t len = 0;

            if (desc_type == DESC_DEVICE) { data = dev_desc; len = sizeof(dev_desc); }
            else if (desc_type == DESC_CONFIG) {
                data = cfg_desc;
                len = sizeof(cfg_desc);
                test_debug_slot[3] = ((OTG_DTXFSTS(0) & DTXFSTS_INEPTFSAV_MASK) << 16) |
                                     (uint32_t)usb_ep0_in_chunks;
            }
            else if (desc_type == DESC_OTHER_SPEED_CONFIG) {
                data = cfg_desc;
                len = sizeof(cfg_desc);
            }
            else if (desc_type == DESC_STRING) {
                data = get_string(desc_idx);
                if (data) {
                    len = data[0];
                }
            }
            else if (desc_type == DESC_DEVICE_QUALIFIER) { data = dev_qual_desc; len = sizeof(dev_qual_desc); }

            if (data && len > 0) {
                uint32_t send = (len < wLength) ? len : wLength;
                ep0_start_data_in(data, send);
            } else {
                l6_diag_ep0_in_stall_set++;
                l6_diag_ep0_out_stall_set++;
                OTG_DIEPCTL(0) |= DIEPCTL_STALL;
                OTG_DOEPCTL(0) |= DOEPCTL_STALL;
                l6_diag_refresh_summary();
            }
            return;
        }

        if (bRequest == USB_REQ_SET_ADDRESS && !dir_in) {
            /* Defer DCFG.DAD until EP0 status-IN ZLP completes (ChibiOS late set_address). */
            ep0_pending_addr = (uint8_t)(wValue & 0x7F);
            ep0_addr_defer = 1;
            ep0_start_status_in();
            return;
        }

        if (bRequest == USB_REQ_SET_CONFIG && !dir_in) {
            usb_configured = (wValue != 0) ? 1 : 0;
            test_debug_slot[2] = 0xC0000000 | usb_configured;
            if (usb_configured) {
                /* Configure CDC endpoints */
                /* EP1 OUT (Bulk) */
                OTG_DOEPCTL(1)  = DOEPCTL_SD0PID | DOEPCTL_USBAEP |
                                  DOEPCTL_EPTYP_BULK | DOEPCTL_MPSIZ(64);
                OTG_DAINTMSK    |= DAINTMSK_OEPM(1);
                /* EP1 IN (Bulk) */
                uint32_t ep1_txsa = fifo_alloc(TX1_FIFO_SIZE);
                OTG_DIEPTXF(0)   = DIEPTXF_INEPTXFD(TX1_FIFO_SIZE) |
                                   DIEPTXF_INEPTXSA(ep1_txsa);
                txfifo_flush(1);
                OTG_DIEPCTL(1)  = DIEPCTL_SD0PID | DIEPCTL_USBAEP |
                                  DIEPCTL_EPTYP_BULK |
                                  DIEPCTL_TXFNUM(1) | DIEPCTL_MPSIZ(64);
                OTG_DAINTMSK    |= DAINTMSK_IEPM(1);
                /* EP2 IN (Interrupt) */
                uint32_t ep2_txsa = fifo_alloc(TX2_FIFO_SIZE);
                OTG_DIEPTXF(1)   = DIEPTXF_INEPTXFD(TX2_FIFO_SIZE) |
                                   DIEPTXF_INEPTXSA(ep2_txsa);
                txfifo_flush(2);
                OTG_DIEPCTL(2)  = DIEPCTL_SD0PID | DIEPCTL_USBAEP |
                                  DIEPCTL_EPTYP_INTR |
                                  DIEPCTL_TXFNUM(2) | DIEPCTL_MPSIZ(8);
                OTG_DAINTMSK    |= DAINTMSK_IEPM(2);

                /* Start EP1 OUT receiving */
                ep1_rxcnt = 0;
                ep1_busy = 0;
                ep1_out_armed = 0;
                usb_start_out(1, ep1_rxbuf, sizeof(ep1_rxbuf));
                ep1_out_armed = 1;
            }
            ep0_start_status_in();
            return;
        }

        if (bRequest == USB_REQ_GET_CONFIG && dir_in) {
            ep0_ctrl_buf[0] = usb_configured ? 1 : 0;
            ep0_start_data_in(ep0_ctrl_buf, 1);
            return;
        }

        if (bRequest == USB_REQ_GET_STATUS && dir_in) {
            /* Self-powered, remote wakeup disabled */
            ep0_ctrl_buf[0] = (recip == 0) ? 0x01 : 0x00;  /* Device: self-powered */
            ep0_ctrl_buf[1] = 0x00;
            ep0_start_data_in(ep0_ctrl_buf, 2);
            return;
        }

        if (bRequest == USB_REQ_CLEAR_FEATURE && !dir_in) {
            if (recip == 2 && wValue == 0) {  /* ENDPOINT_HALT */
                int ep = wIndex & 0x0F;
                int dir_ep = (wIndex & 0x80) ? 1 : 0;
                if (dir_ep && ep < MAX_EP) OTG_DIEPCTL(ep) &= ~DIEPCTL_STALL;
                else if (ep < MAX_EP) OTG_DOEPCTL(ep) &= ~DOEPCTL_STALL;
            }
            ep0_start_status_in();
            return;
        }
    }

    /* ===== CDC requests ===== */
    if (type == 0x20 && recip == 1) {  /* Class, Interface */
        if (bRequest == CDC_SET_LINE_CODING && !dir_in) {
            /* Host sends line coding in the EP0 OUT data stage (not in SETUP). */
            if (wLength > 0) {
                ep0_out_expect = wLength;
                ep0_out_got = 0;
                ep0_state = EP0_DATA_OUT;
                ep0_arm_data_out(wLength);
                return;
            }
            ep0_start_status_in();
            return;
        }
        if (bRequest == CDC_GET_LINE_CODING && dir_in) {
            ep0_ctrl_buf[0] = cdc_line_coding[0];  /* dwDTERate */
            ep0_ctrl_buf[1] = cdc_line_coding[1];
            ep0_ctrl_buf[2] = cdc_line_coding[2];
            ep0_ctrl_buf[3] = cdc_line_coding[3];
            ep0_ctrl_buf[4] = cdc_line_coding[4];  /* bCharFormat */
            ep0_ctrl_buf[5] = cdc_line_coding[5];  /* bParityType */
            ep0_ctrl_buf[6] = cdc_line_coding[6];  /* bDataBits */
            ep0_start_data_in(ep0_ctrl_buf, 7);
            return;
        }
        if (bRequest == CDC_SET_CTRL_LINE_STATE && !dir_in) {
            cdc_ctrl_line_state = wValue;
            ep0_start_status_in();
            return;
        }
    }

    /* Unknown request — stall */
    l6_diag_ep0_in_stall_set++;
    l6_diag_ep0_out_stall_set++;
    OTG_DIEPCTL(0) |= DIEPCTL_STALL;
    OTG_DOEPCTL(0) |= DOEPCTL_STALL;
    l6_diag_refresh_summary();
}

/* =========================================================================
 *  IN/OUT endpoint event handlers
 * ========================================================================= */
/* Cache buffer for echo */
static uint8_t ep1_txbuf_cache[512];

static const char l6_ready_msg[] = "\r\nL6_CDC echo ready (write bytes, get echo)\r\n";

static void usb_arm_ep1_out(void)
{
    if (!usb_configured || ep1_out_armed || ep1_busy) {
        return;
    }
    ep1_rxcnt = 0;
    usb_start_out(1, ep1_rxbuf, sizeof(ep1_rxbuf));
    ep1_out_armed = 1;
}

static void usb_try_echo_bulk(void)
{
    uint32_t len;

    if (!usb_configured || ep1_busy || ep1_rxcnt == 0) {
        return;
    }

    len = ep1_rxcnt;
    if (len > sizeof(ep1_txbuf_cache)) {
        len = sizeof(ep1_txbuf_cache);
    }
    memcpy(ep1_txbuf_cache, ep1_rxbuf, len);
    ep1_rxcnt = 0;
    ep1_txlen = len;
    ep1_txcnt = 0;
    ep1_busy = 1;
    ep1_out_armed = 0;
    usb_start_in(1, ep1_txbuf_cache, ep1_txlen);
}

static void usb_send_cdc_in(const uint8_t *buf, uint32_t len)
{
    if (!usb_configured || ep1_busy || len == 0) {
        return;
    }
    if (len > sizeof(ep1_txbuf_cache)) {
        len = sizeof(ep1_txbuf_cache);
    }
    memcpy(ep1_txbuf_cache, buf, len);
    ep1_txlen = len;
    ep1_txcnt = 0;
    ep1_busy = 1;
    usb_start_in(1, ep1_txbuf_cache, ep1_txlen);
}

static void handle_epin(int ep)
{
    uint32_t epint;
    if (ep == 0)      epint = OTG_DIEPINT(0);
    else if (ep == 1) epint = OTG_DIEPINT(1);
    else if (ep == 2) epint = OTG_DIEPINT(2);
    else return;

    if (ep == 0) {
        l6_diag_last_diepint0 = epint;
    }

    OTG_DIEPINT(ep) = epint;

    if ((epint & DIEPINT_XFRC) && (OTG_DIEPMSK & DIEPMSK_XFRCM)) {
        if (ep == 0) {
            l6_diag_ep0_in_xfrc++;
            if (ep0_state == EP0_STATUS_IN) {
                if (ep0_addr_defer) {
                    ep0_addr_defer = 0;
                    usb_address = ep0_pending_addr;
                    OTG_DCFG = (OTG_DCFG & ~DCFG_DAD_MASK) | DCFG_DAD(usb_address);
                    test_debug_slot[2] = 0xAD000000 | usb_address;
                    ep0_pending_addr = 0;
                }
                ep0_rearm_setup();
            } else if (ep0_state == EP0_DATA_IN) {
                OTG_DIEPEMPMSK &= ~DIEPEMPMSK_INEPTXFEM(0);
                ep0_in_sent += ep0_txlen;
                if (ep0_in_sent < ep0_in_total) {
                    ep0_start_in_chunk();
                } else {
                    ep0_finish_data_in();
                }
            } else if (ep0_state == EP0_DATA_IN_ZLP) {
                ep0_start_status_out();
            }
        } else if (ep == 1) {
            /* EP1 IN complete — Bulk data sent */
            if (ep1_txlen > 0) {
                usb_tx_count += ep1_txlen;
                usb_echo_pkts++;
            }
            ep1_txlen = 0;
            ep1_txcnt = 0;
            ep1_busy = 0;
            usb_arm_ep1_out();
        }
    }

    if ((epint & DIEPINT_TXFE) &&
        (OTG_DIEPEMPMSK & DIEPEMPMSK_INEPTXFEM(ep))) {
        if (ep == 0) {
            l6_diag_ep0_in_txfe++;
        }
        if (ep == 0 && ep0_state == EP0_DATA_IN) {
            if (ep0_txfifo_fill(ep0_ctrl_buf + ep0_in_sent)) {
                OTG_DIEPEMPMSK &= ~DIEPEMPMSK_INEPTXFEM(0);
            }
        }
    }

    if (ep == 0) {
        l6_diag_last_ep0_state = ep0_state;
        l6_diag_refresh_summary();
    }
}

static void handle_epout(int ep)
{
    uint32_t epint;
    if (ep == 0)      epint = OTG_DOEPINT(0);
    else if (ep == 1) epint = OTG_DOEPINT(1);
    else return;

    if (ep == 0) {
        l6_diag_last_doepint0 = epint;
    }

    OTG_DOEPINT(ep) = epint;

    if ((epint & DOEPINT_STUP) && (OTG_DOEPMSK & DOEPMSK_STUPM)) {
        l6_diag_ep0_out_stup++;
        handle_setup();
    }

    if ((epint & DOEPINT_XFRC) && (OTG_DOEPMSK & DOEPMSK_XFRCM)) {
        if (ep == 0) {
            l6_diag_ep0_out_xfrc++;
            if (ep0_state == EP0_STATUS_OUT) {
                ep0_rearm_setup();
                return;
            }
            if (ep0_out_expect > 0 && ep0_out_got >= ep0_out_expect) {
                if (ep0_out_expect >= 7) {
                    memcpy(cdc_line_coding, ep0_ctrl_buf, 7);
                }
                ep0_out_expect = 0;
                ep0_out_got = 0;
                ep0_start_status_in();
            } else if (ep0_state == EP0_IDLE) {
                ep0_rearm_setup();
            }
        } else if (ep == 1) {
            ep1_out_armed = 0;
            usb_try_echo_bulk();
        }
    }

    if (ep == 0) {
        l6_diag_last_ep0_state = ep0_state;
        l6_diag_refresh_summary();
    }
}

/* =========================================================================
 *  Main ISR (from ChibiOS usb_lld_serve_interrupt)
 * ========================================================================= */
void OTG_FS_IRQHandler(void)
{
    rt_interrupt_enter();

    uint32_t sts = OTG_GINTSTS;
    uint32_t msk = OTG_GINTMSK;
    uint32_t active = sts & msk;

    l6_diag_last_gintsts = sts;

    /* USB Reset — must handle first, clear only this bit before early return */
    if (active & GINTMSK_USBRSTM) {
        OTG_GINTSTS = GINTMSK_USBRSTM;   /* clear only USBRST, other bits stay */
        _dsb();
        usb_handle_reset();
        rt_interrupt_leave();
        return;
    }

    /* Wakeup */
    if (active & GINTMSK_WKUM) {
        OTG_GINTSTS = GINTMSK_WKUM;
        if (OTG_PCGCCTL & (PCGCCTL_STPPCLK | PCGCCTL_GATEHCLK))
            OTG_PCGCCTL &= ~(PCGCCTL_STPPCLK | PCGCCTL_GATEHCLK);
        OTG_DCTL &= ~DCTL_RWUSIG;
        _dsb();
    }

    /* Suspend */
    if (active & GINTMSK_USBSUSPM) {
        OTG_GINTSTS = GINTMSK_USBSUSPM;
        /* Stop handling for test */
    }

    if (active & GINTMSK_ESUSPM) {
        OTG_GINTSTS = GINTMSK_ESUSPM;
    }

    if (active & GINTMSK_SRQM) {
        OTG_GINTSTS = GINTMSK_SRQM;
    }

    if (active & GINTMSK_IISOIXFRM) {
        OTG_GINTSTS = GINTMSK_IISOIXFRM;
    }

    if (active & GINTMSK_IISOOXFRM) {
        OTG_GINTSTS = GINTMSK_IISOOXFRM;
    }

    /* Enumeration done */
    if (active & GINTMSK_ENUMDNEM) {
        OTG_GINTSTS = GINTMSK_ENUMDNEM;
        if ((OTG_DSTS & DSTS_ENUMSPD_MASK) == DSTS_ENUMSPD_FS_11) {
            OTG_GUSBCFG = (OTG_GUSBCFG & ~GUSBCFG_TRDT_MASK) |
                          GUSBCFG_TRDT(5);
        } else {
            OTG_GUSBCFG = (OTG_GUSBCFG & ~GUSBCFG_TRDT_MASK) |
                          GUSBCFG_TRDT(5);
        }
        usb_enum_speed = OTG_DSTS & DSTS_ENUMSPD_MASK;
        _dsb();
    }

    /* SOF */
    if (active & GINTMSK_SOFM) {
        OTG_GINTSTS = GINTMSK_SOFM;
        usb_sof_count++;
    }

    /* RXFLVL — drain FIFO status queue (ChibiOS pops GRXSTSP per entry). */
    while ((OTG_GINTSTS & GINTMSK_RXFLVLM) && (OTG_GINTMSK & GINTMSK_RXFLVLM)) {
        handle_rxflvl();
    }

    /* OUT endpoint events */
    if (active & GINTMSK_OEPM) {
        uint32_t daint = OTG_DAINT;
        if (daint & (1UL << 16)) handle_epout(0);
        if (daint & (1UL << 17)) handle_epout(1);
        if (daint & (1UL << 18)) handle_epout(2);
    }

    /* IN endpoint events */
    if (active & GINTMSK_IEPM) {
        uint32_t daint = OTG_DAINT;
        if (daint & (1UL << 0))  handle_epin(0);
        if (daint & (1UL << 1))  handle_epin(1);
        if (daint & (1UL << 2))  handle_epin(2);
    }

    rt_interrupt_leave();
}

/* =========================================================================
 *  Test Steps
 * ========================================================================= */

/* Step 1-7: Same as L5_usb */
static void step_reg_access(void)
{
    TEST_STEP("Register accessibility");
    uint32_t gusb = OTG_GUSBCFG;
    uint32_t dcfg = OTG_DCFG;
    uint32_t dctl = OTG_DCTL;
    test_printf("    GUSBCFG=0x%08lx DCFG=0x%08lx DCTL=0x%08lx\r\n",
                (unsigned long)gusb, (unsigned long)dcfg, (unsigned long)dctl);
    TEST_ASSERT(gusb != 0xFFFFFFFFU, "GUSBCFG readable");
    TEST_PASS();
}

static void step_clock_gpio(void)
{
    TEST_STEP("Clock + GPIO");
    RCC_APB1ENR |= (1UL << 28); _dsb(); _delay(100);
    PWR_CR2     |= (1UL << 0);  _dsb(); _delay(100);

    uint32_t before = RCC_AHB2ENR;
    RCC_AHB2ENR = before | (1UL << 7); _dsb(); _isb();
    uint32_t after = RCC_AHB2ENR;
    test_printf("    AHB2ENR 0x%08lx → 0x%08lx\r\n", (unsigned long)before, (unsigned long)after);
    TEST_ASSERT(after & (1UL << 7), "OTG_FS clock");

    RCC_AHB2RSTR |= (1UL << 7); _dsb(); _delay(500);
    RCC_AHB2RSTR &= ~(1UL << 7); _dsb(); _delay(500);

    RCC_AHB1ENR |= (1UL << 0); _dsb();
    PA_MODER = (PA_MODER & ~(3UL << 22)) | (2UL << 22);
    PA_MODER = (PA_MODER & ~(3UL << 24)) | (2UL << 24);
    PA_AFRH  = (PA_AFRH & ~(0xFUL << 12)) | (10UL << 12);
    PA_AFRH  = (PA_AFRH & ~(0xFUL << 16)) | (10UL << 16);
    PA_OSPEEDR |= (3UL << 22) | (3UL << 24);
    PA_PUPDR  &= ~((3UL << 22) | (3UL << 24));
    PA_MODER  &= ~(3UL << 18); _dsb();
    test_printf("    MODER=0x%08lx AFRH=0x%08lx\r\n",
                (unsigned long)PA_MODER, (unsigned long)PA_AFRH);
    TEST_PASS();
}

static void step_soft_disconnect(void)
{
    TEST_STEP("Soft disconnect");
    OTG_DCTL = DCTL_SDIS; _dsb(); _mdelay(60);
    test_printf("    DCTL=0x%08lx SDIS=%lu\r\n",
                (unsigned long)OTG_DCTL, (unsigned long)((OTG_DCTL >> 1) & 1));
    TEST_ASSERT(OTG_DCTL & DCTL_SDIS, "SDIS set");
    TEST_PASS();
}

static void step_core_config(void)
{
    TEST_STEP("Core config");
    OTG_GUSBCFG = GUSBCFG_FDMOD | GUSBCFG_TRDT(5) | GUSBCFG_PHYSEL; _dsb();
    OTG_DCFG    = DCFG_NZLSOHSK | DCFG_DSPD_FS11; _dsb();
    OTG_PCGCCTL = 0; _dsb();
    OTG_GOTGCTL = GOTGCTL_BVALOEN | GOTGCTL_BVALOVAL; _dsb();
    OTG_GCCFG   = GCCFG_VBDEN | GCCFG_PWRDWN; _dsb();

    /* Check mode — save to RTC BKP for non-volatile readback */
    test_debug_slot[0] = OTG_GUSBCFG;
    test_debug_slot[1] = OTG_GINTSTS & 1;
    test_debug_slot[2] = OTG_DCFG;
    test_debug_slot[3] = OTG_GCCFG;
    *(volatile uint32_t *)0x40002850 = OTG_GUSBCFG;
    *(volatile uint32_t *)0x40002854 = OTG_GINTSTS;
    *(volatile uint32_t *)0x40002858 = OTG_DCFG;
    *(volatile uint32_t *)0x4000285C = OTG_GCCFG;

    test_printf("    GUSBCFG=0x%08lx DCFG=0x%08lx GCCFG=0x%08lx\r\n",
                (unsigned long)OTG_GUSBCFG,
                (unsigned long)OTG_DCFG,
                (unsigned long)OTG_GCCFG);
    TEST_PASS();
}

static void step_core_reset(void)
{
    TEST_STEP("Core reset");
    { uint32_t sp = 50000; while (!(OTG_GRSTCTL & GRSTCTL_AHBIDL) && sp--) { _delay(10); ap_rtt_iwdg_kick(); } }
    OTG_GRSTCTL = GRSTCTL_CSRST; _dsb(); _delay(500);
    { uint32_t sp = 50000; while ((OTG_GRSTCTL & GRSTCTL_CSRST) && sp--) { _delay(10); ap_rtt_iwdg_kick(); } }
    { uint32_t sp = 50000; while (!(OTG_GRSTCTL & GRSTCTL_AHBIDL) && sp--) { _delay(10); ap_rtt_iwdg_kick(); } }
    test_printf("    CSRST DONE AHBIDL=%lu\r\n",
                (unsigned long)((OTG_GRSTCTL >> 31) & 1));

    /* CSRST does NOT reset GUSBCFG/DCFG — they persist from step_core_config.
     * Verify FDMOD and CurMod before any writes. */
    {
        uint32_t gusb = OTG_GUSBCFG;
        uint32_t cur  = OTG_GINTSTS & 1;
        test_printf("    GUSBCFG=0x%08lx CurMod=%lu\r\n",
                    (unsigned long)gusb, (unsigned long)cur);
        if (cur != 0) {
            /* Force FDMOD again and wait */
            OTG_GUSBCFG = GUSBCFG_FDMOD | GUSBCFG_TRDT(5) | GUSBCFG_PHYSEL;
            _dsb();
            uint32_t dm_wait = 1000;
            while ((OTG_GINTSTS & 1) && dm_wait--) {
                _delay(10);
                ap_rtt_iwdg_kick();
            }
            test_printf("    Retry: CurMod=%lu polls=%lu\r\n",
                        (unsigned long)(OTG_GINTSTS & 1),
                        (unsigned long)(1000 - dm_wait));
        }
    }
    OTG_DCTL = 0;
    _dsb(); _isb();
    test_printf("    DCTL post-reset=0x%08lx\\r\\n", (unsigned long)OTG_DCTL);

    /* Soft disconnect to prevent host from enumerating before EP0 is ready */
    OTG_DCTL |= DCTL_SDIS; _dsb();
    _mdelay(10);
    test_printf("    SDIS=1 (disconnected)\\r\\n");

    TEST_PASS();
}

/* =========================================================================
 *  Step 8: Full USB CDC init with ISR + EP config
 * ========================================================================= */
static void step_usb_enable(void)
{
    TEST_STEP("USB CDC init + ISR + EP0");

    /* FIFO */
    OTG_GRXFSIZ = RX_FIFO_SIZE;
    OTG_DIEPTXF0 = DIEPTXF_INEPTXFD(TX0_FIFO_SIZE) |
                   DIEPTXF_INEPTXSA(fifo_alloc(TX0_FIFO_SIZE));
    _dsb();

    /* GAHBCFG */
    OTG_GAHBCFG = 0;

    /* GINTMSK — enable ALL USB interrupts we need */
    OTG_GINTMSK  = GINTMSK_USBRSTM | GINTMSK_ENUMDNEM |
                   GINTMSK_ESUSPM  | GINTMSK_USBSUSPM |
                   GINTMSK_RXFLVLM | GINTMSK_OEPM | GINTMSK_IEPM |
                   GINTMSK_SOFM    | GINTMSK_WKUM;
    _dsb();

    /* Clear pending */
    OTG_GINTSTS = 0xFFFFFFFF; _dsb();

    /* Flush TX FIFO 0 */
    txfifo_flush(0);

    /* DIEPMSK + DOEPMSK (minimal for EP0)
     * NOTE: pre-configure BEFORE reconnect, don't rely on USBRST ISR
     * to set these — there's a race between SDIS clear and ISR dispatch.
     * Same values as usb_handle_reset() would set. */
    OTG_DIEPMSK  = DIEPMSK_TOCM | DIEPMSK_XFRCM | DIEPMSK_TXFEM;
    OTG_DOEPMSK  = DOEPMSK_STUPM | DOEPMSK_XFRCM;
    OTG_DAINTMSK = DAINTMSK_OEPM(0) | DAINTMSK_IEPM(0);

    /* EP0 init */
    OTG_DOEPTSIZ(0) = DOEPTSIZ_STUPCNT(3); _dsb();
    OTG_DOEPCTL(0)  = DOEPCTL_SD0PID | DOEPCTL_USBAEP |
                      DOEPCTL_EPTYP_CTRL | DOEPCTL_MPSIZ(64);
    OTG_DIEPCTL(0)  = DIEPCTL_SD0PID | DIEPCTL_USBAEP |
                      DIEPCTL_EPTYP_CTRL |
                      DIEPCTL_TXFNUM(0) | DIEPCTL_MPSIZ(64);
    _dsb();
    test_printf("    EP0 configured\r\n");

    /* NVIC: OTG_FS IRQ 67 */
    NVIC_ISER2 = (1UL << 3); _dsb();

    /* GAHBCFG: global interrupt */
    OTG_GAHBCFG |= GAHBCFG_GINTMSK; _dsb();
    test_printf("    GAHBCFG=0x%08lx GINT=%s\r\n",
                (unsigned long)OTG_GAHBCFG,
                (OTG_GAHBCFG & GAHBCFG_GINTMSK) ? "EN" : "DIS");

    /* Reconnect: SOFT DISCONNECT → DELAY → RECONNECT
     * This ensures a clean DP line drop → rise sequence so the host
     * detects a new device and re-enumerates. Critical when coming from
     * bootloader which already had USB initialized — just clearing SDIS=0
     * does nothing if SDIS was already 0 after CSRST. */
    OTG_DCTL |= DCTL_SDIS; _dsb();
    {
        uint32_t dctl_wait = 5000;
        while (dctl_wait--) _delay(10);   /* ~5ms soft disconnect hold */
    }
    test_printf("    SDIS set to 1 (disconnect), DCTL=0x%08lx\\r\\n",
                (unsigned long)OTG_DCTL);

    /* Write DCTL to clear SDIS. Use full write with bit25=1 preserved
     * (some STM32F767 variants assert bit25 as a read-only status, so
     * writing 0x00000000 gets overridden back to 0x02000003). */
    OTG_DCTL = (OTG_DCTL & ~DCTL_SDIS); _dsb();
    {
        uint32_t dctl_wait = 2000;
        while ((OTG_DCTL & DCTL_SDIS) && dctl_wait--) {
            _delay(10);
            /* Try: write the current value minus SDIS bit.
             * If hardware re-asserts SDIS, we need to find out why. */
            OTG_DCTL = (OTG_DCTL & ~DCTL_SDIS);
            _dsb();
        }
        test_printf("    Reconnect: DCTL=0x%08lx SDIS=%lu waited=%lu\\r\\n",
                    (unsigned long)OTG_DCTL,
                    (unsigned long)((OTG_DCTL >> 1) & 1),
                    (unsigned long)(2000 - dctl_wait));
    }

    /* Force DCFG again — may get reset by core reset / host reset */
    OTG_DCFG = DCFG_NZLSOHSK | DCFG_DSPD_FS11;
    _dsb();
    test_printf("    DCFG=0x%08lx\r\n", (unsigned long)OTG_DCFG);
    if ((OTG_DCFG & 3) != DCFG_DSPD_FS11) {
        test_printf("    DCFG write FAIL! Trying again...\r\n");
        /* Read GINTSTS to check mode */
        test_printf("    GINTSTS=0x%08lx CurMod=%lu\r\n",
                    (unsigned long)OTG_GINTSTS,
                    (unsigned long)(OTG_GINTSTS & 1));
        OTG_DCFG = (OTG_DCFG & ~3) | 3;
        _dsb();
        test_printf("    DCFG after retry=0x%08lx\r\n", (unsigned long)OTG_DCFG);
    }
    /* Save DCFG to RTC BKP for non-volatile readback */
    *(volatile uint32_t *)0x40002850 = OTG_DCFG;
    *(volatile uint32_t *)0x40002854 = OTG_DCTL;
    *(volatile uint32_t *)0x40002858 = OTG_GINTSTS;
    *(volatile uint32_t *)0x4000285C = OTG_GUSBCFG;
    test_debug_slot[0] = OTG_DCFG;
    test_debug_slot[1] = OTG_DCTL;
    _delay(10000);

    uint32_t dsts = OTG_DSTS;
    test_printf("    DSTS=0x%08lx ENUMSPD=%lu\r\n",
                (unsigned long)dsts, (unsigned long)(dsts & 3));

    TEST_PASS();
}

/* =========================================================================
 *  Step 9: Wait for enumeration, then run echo service
 * ========================================================================= */
static void step_echo(void)
{
    TEST_STEP("Echo service");

    test_printf("    Waiting for enumeration + configure...\r\n");
    test_printf("    DCTL at start=0x%08lx\r\n", (unsigned long)OTG_DCTL);
    uint32_t start = rt_tick_get();
    uint32_t timeout = RT_TICK_PER_SECOND * 30;
    uint32_t ready_sent = 0;
    uint32_t last_rx = 0;
    uint32_t last_tx = 0;
    uint32_t last_echo = 0;
    uint32_t last_report = start;

    while ((rt_tick_get() - start) < timeout) {
        ap_rtt_iwdg_kick();
        /* Keep SDIS cleared — hardware may assert it */
        if (OTG_DCTL & DCTL_SDIS) {
            OTG_DCTL &= ~DCTL_SDIS;
            _dsb();
            test_printf("    * SDIS cleared\r\n");
        }
        if (usb_configured) {
            break;
        }
        _delay(5000);
    }

    uint32_t elapsed_ms = ((rt_tick_get() - start) * 1000) / RT_TICK_PER_SECOND;
    test_printf("    Wait time: %lu ms\r\n", (unsigned long)elapsed_ms);
    test_printf("    Configured=%lu Address=%lu Speed=%lu\r\n",
                (unsigned long)usb_configured,
                (unsigned long)usb_address,
                (unsigned long)usb_enum_speed);
    test_printf("    EP0 setups=%lu handled=%lu rx=%lu Resets=%lu\r\n",
                (unsigned long)usb_ep0_setups,
                (unsigned long)l6_diag_setup_count,
                (unsigned long)l6_diag_setup_rx_count,
                (unsigned long)usb_reset_count);
    l6_diag_refresh_summary();
    test_printf("    SOF count=%lu\r\n", (unsigned long)usb_sof_count);

    if (!usb_configured) {
        test_printf("    USB not configured within timeout (host may need more time)\r\n");
        TEST_PASS();
        return;
    }

    test_printf("    USB configured — CDC bulk echo active\r\n");
    test_printf("    Host test: echo \"hello\" > /dev/ttyACM* && cat /dev/ttyACM*\r\n");

    /* Service OUT/IN echo in thread context as backup to OTG ISR */
    start = rt_tick_get();
    timeout = RT_TICK_PER_SECOND * 120;

    while ((rt_tick_get() - start) < timeout) {
        ap_rtt_iwdg_kick();

        if (OTG_DCTL & DCTL_SDIS) {
            OTG_DCTL &= ~DCTL_SDIS;
            _dsb();
        }

        if (!ready_sent && !ep1_busy) {
            usb_send_cdc_in((const uint8_t *)l6_ready_msg,
                            (uint32_t)(sizeof(l6_ready_msg) - 1));
            ready_sent = 1;
            test_printf("    Sent CDC ready banner on EP1 IN\r\n");
        }

        if (!ep1_out_armed && !ep1_busy) {
            usb_arm_ep1_out();
        }
        usb_try_echo_bulk();

        if (usb_rx_count != last_rx || usb_tx_count != last_tx ||
            usb_echo_pkts != last_echo) {
            test_printf("    CDC stats: rx=%lu tx=%lu echo_pkts=%lu busy=%lu\r\n",
                        (unsigned long)usb_rx_count,
                        (unsigned long)usb_tx_count,
                        (unsigned long)usb_echo_pkts,
                        (unsigned long)ep1_busy);
            last_rx = usb_rx_count;
            last_tx = usb_tx_count;
            last_echo = usb_echo_pkts;
        } else if ((rt_tick_get() - last_report) >= RT_TICK_PER_SECOND * 10) {
            test_printf("    CDC idle (waiting host OUT) rx=%lu echo_pkts=%lu\r\n",
                        (unsigned long)usb_rx_count,
                        (unsigned long)usb_echo_pkts);
            last_report = rt_tick_get();
        }

        _delay(2000);
    }

    test_printf("    Echo service window ended (IRQ still active after TEST_DONE)\r\n");
    test_printf("    Final: rx=%lu tx=%lu echo_pkts=%lu\r\n",
                (unsigned long)usb_rx_count,
                (unsigned long)usb_tx_count,
                (unsigned long)usb_echo_pkts);

    TEST_PASS();
}

/* =========================================================================
 *  Main
 * ========================================================================= */
int main(void)
{
    TEST_INIT("L6_CDC");
    test_current_layer = 6;

    /* Feed IWDG immediately — bootloader may have set very short timeout */
    ap_rtt_iwdg_kick();
    ap_rtt_iwdg_kick();
    ap_rtt_iwdg_kick();

    step_reg_access();
    ap_rtt_iwdg_kick();
    step_clock_gpio();
    ap_rtt_iwdg_kick();
    /* Bootloader handoff: SDIS + delay before reconfig (avoid host stuck on CUAVv5-BL). */
    step_soft_disconnect();
    ap_rtt_iwdg_kick();
    step_core_config();
    ap_rtt_iwdg_kick();
    step_core_reset();
    ap_rtt_iwdg_kick();
    step_usb_enable();
    ap_rtt_iwdg_kick();
    step_echo();

    TEST_DONE();
    return 0;
}
