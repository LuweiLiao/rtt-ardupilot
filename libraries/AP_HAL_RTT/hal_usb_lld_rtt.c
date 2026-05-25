/*
 * RTT adaptation of ChibiOS STM32 OTGv1 LLD (hal_usb_lld_rtt.c).
 *
 * Direct DWC2 register operations for polling-mode USB device.
 * Bypasses CherryUSB CDC ACM stack entirely.
 *
 * ChibiOS reference:
 *   modules/ChibiOS/os/hal/ports/STM32/LLD/OTGv1/hal_usb_lld.c   (1284 lines)
 *   modules/ChibiOS/os/hal/ports/STM32/LLD/OTGv1/stm32_otg.h      (608 lines)
 *   modules/ChibiOS/os/hal/ports/STM32/LLD/OTGv1/hal_usb_lld.h
 *
 * Adaptations for RTT (polling mode):
 *  - Direct register access via OTG_FS peripheral base (0x50000000)
 *  - No USBDriver struct / USBInEndpointState / USBOutEndpointState
 *  - No ChibiOS OSAL (chSysLock / osalDbgAssert removed)
 *  - No NVIC / interrupt handlers
 *  - Polled GINTSTS for bus events
 *  - Polled DIEPINT for transfer completion
 *  - Simplified: EP0 control + EP1 bulk IN for CDC data
 *
 * STM32F767 DWC2 register layout (RM0410 §45):
 *   Global:   base + 0x000  (USB_OTG_GlobalTypeDef)
 *   Device:   base + 0x800  (USB_OTG_DeviceTypeDef)
 *   IN EP:    base + 0x900 + ep*0x20  (USB_OTG_INEndpointTypeDef)
 *   OUT EP:   base + 0xB00 + ep*0x20  (USB_OTG_OUTEndpointTypeDef)
 *   FIFO:     base + 0x1000 + ep*4    (uint32_t FIFO[16])
 *
 * NOTE: The stm32f7_cmsis_driver-latest package defines USB_OTG_GlobalTypeDef
 * and USB_OTG_DeviceTypeDef structs but does NOT define the bit field
 * masks/constants.  All DWC2 bit definitions are defined here based on
 * RM0410 §45 and ChibiOS stm32_otg.h.
 */

#include "hal_usb_lld_rtt.h"
#include <stm32f7xx.h>
#include <string.h>

/* ========================================================================== */
/* DWC2 register bit definitions (RM0410 §45)                                */
/* ========================================================================== */

/* ---- GOTGCTL (0x000) ---- */
#define GOTGCTL_BVALOEN         0x00400000UL
#define GOTGCTL_BVALOVAL        0x00800000UL

/* ---- GAHBCFG (0x008) ---- */
#define GAHBCFG_GINTMSK         0x00000001UL
#define GAHBCFG_TXFELVL         0x00000080UL
#define GAHBCFG_PTXFELVL        0x00000100UL

/* ---- GUSBCFG (0x00C) ---- */
#define GUSBCFG_FDMOD           0x40000000UL
#define GUSBCFG_PHYSEL          0x00000040UL
#define GUSBCFG_TRDT_MASK       0x00001C00UL
#define GUSBCFG_TRDT_SHIFT      10
#define GUSBCFG_TRDT(n)         (((n) << GUSBCFG_TRDT_SHIFT) & GUSBCFG_TRDT_MASK)
#define GUSBCFG_TOC_MASK        0x0000000FUL
#define GUSBCFG_SRPCAP          0x00000100UL
#define GUSBCFG_HNPCAP          0x00000200UL

/* ---- GRSTCTL (0x010) ---- */
#define GRSTCTL_CSRST           0x00000001UL
#define GRSTCTL_HCSFT           0x00000002UL
#define GRSTCTL_RXFFLSH         0x00000010UL
#define GRSTCTL_TXFFLSH         0x00000020UL
#define GRSTCTL_TXFNUM_MASK     0x000007C0UL
#define GRSTCTL_TXFNUM_SHIFT    6
#define GRSTCTL_TXFNUM(n)       (((n) << GRSTCTL_TXFNUM_SHIFT) & GRSTCTL_TXFNUM_MASK)
#define GRSTCTL_AHBIDL          0x00000080UL

/* ---- GINTSTS / GINTMSK (0x014 / 0x018) ---- */
#define GINTMSK_USBRSTM         0x00000002UL
#define GINTMSK_ESUSPM          0x00000080UL
#define GINTMSK_SOFM            0x00000008UL
#define GINTMSK_RXFLVLM         0x00000010UL
#define GINTMSK_USBSUSPM        0x00000001UL
#define GINTMSK_WKUPM           0x80000000UL
#define GINTMSK_SRQM            0x40000000UL
#define GINTMSK_ENUMDNEM        0x00002000UL
#define GINTMSK_IISOIXFRM       0x00200000UL
#define GINTMSK_IISOOXFRM       0x00400000UL
#define GINTMSK_OEPM            0x00080000UL
#define GINTMSK_IEPM            0x00040000UL

/* ---- GRXSTSP / GRXSTSR ---- */
#define GRXSTSP_BCNT_MASK       0x0000001FUL
#define GRXSTSP_BCNT_SHIFT      0
#define GRXSTSP_EPNUM_MASK      0x07C00000UL
#define GRXSTSP_EPNUM_SHIFT     22
#define GRXSTSP_PKTSTS_MASK     0x001E0000UL
#define GRXSTSP_PKTSTS_SHIFT    17
#define  GRXSTSP_SETUP_DATA     2
#define  GRXSTSP_SETUP_COMP     4
#define  GRXSTSP_OUT_DATA       1
#define  GRXSTSP_OUT_COMP       3

/* ---- DCFG (0x800) ---- */
#define DCFG_DSPD_MASK          0x00000003UL
#define DCFG_DSPD_FS11          0x00000003UL       /* 48MHz FS 1.1 PHY */
#define DCFG_DSPD_HS            0x00000000UL       /* High speed */
#define DCFG_DSPD_HS_FS         0x00000001UL       /* HS PHY in FS mode */
#define DCFG_DAD_MASK           0x00007FF0UL
#define DCFG_DAD_SHIFT          4
#define DCFG_DAD(addr)          (((addr) << DCFG_DAD_SHIFT) & DCFG_DAD_MASK)

/* ---- DSTS (0x808) ---- */
#define DSTS_ENUMSPD_MASK       0x00000006UL
#define DSTS_ENUMSPD_SHIFT      1
#define DSTS_ENUMSPD_FS11       0x00000002UL       /* FS 1.1 PHY */
#define DSTS_ENUMSPD_HS         0x00000000UL
#define DSTS_ENUMSPD_FS48       0x00000004UL       /* FS (48MHz PHY) */
#define DSTS_FNSOF_ODD          0x80000000UL

/* ---- DCTL (0x804) ---- */
#define DCTL_RWUSIG             0x00000001UL
#define DCTL_SDIS               0x00000002UL
#define DCTL_CGIN               0x00000100UL
#define DCTL_CGON               0x00000200UL

/* ---- DIEPMSK (0x810) ---- */
#define DIEPMSK_XFRCM           0x00000001UL
#define DIEPMSK_EPDM            0x00000002UL
#define DIEPMSK_TOM             0x00000008UL
#define DIEPMSK_TXFIFOEM        0x00000080UL

/* ---- DOEPMSK (0x814) ---- */
#define DOEPMSK_XFRCM           0x00000001UL
#define DOEPMSK_STUPM           0x00000008UL

/* ---- DAINTMSK (0x81C) ---- */
#define DAINTMSK_IEPM(n)        (1UL << (n))
#define DAINTMSK_OEPM(n)        (1UL << (16 + (n)))

/* ---- DIEPCTL / DOEPCTL ---- */
#define DIEPCTL_MPSIZ_MASK      0x000007FFUL
#define DIEPCTL_MPSIZ(n)        ((n) & DIEPCTL_MPSIZ_MASK)
#define DIEPCTL_EPTYP_MASK      0x00030000UL
#define DIEPCTL_EPTYP_SHIFT     16
#define DIEPCTL_EPTYP_ISO(n)    (0x00000000UL)     /* Actually 00 = ISO */
#define DIEPCTL_EPTYP_BULK      0x00010000UL
#define DIEPCTL_EPTYP_CTRL      0x00000000UL
#define DIEPCTL_EPTYP_INTR      0x00030000UL
#define DIEPCTL_SD0PID          0x10000000UL
#define DIEPCTL_SODDFRM         DIEPCTL_SD0PID
#define DIEPCTL_SEVNFRM         0x08000000UL
#define DIEPCTL_TXFNUM_MASK     0x07C00000UL
#define DIEPCTL_TXFNUM_SHIFT    22
#define DIEPCTL_TXFNUM(n)       (((n) << DIEPCTL_TXFNUM_SHIFT) & DIEPCTL_TXFNUM_MASK)
#define DIEPCTL_STALL           0x00200000UL
#define DIEPCTL_SNAK            0x00100000UL
#define DIEPCTL_CNAK            0x00080000UL
#define DIEPCTL_USBAEP          0x00008000UL
#define DIEPCTL_EPDIS           0x40000000UL
#define DIEPCTL_EPENA           0x80000000UL
#define DIEPCTL_MPSIZ64         64

/* ---- DIEPINT ---- */
#define DIEPINT_XFRC            0x00000001UL
#define DIEPINT_TOC             0x00000008UL
#define DIEPINT_TXFE            0x00000080UL

/* ---- DOEPINT ---- */
#define DOEPINT_XFRC            0x00000001UL
#define DOEPINT_STUP            0x00000008UL

/* ---- DIEPTSIZ ---- */
#define DIEPTSIZ_XFRSIZ_MASK    0x0007FFFFUL
#define DIEPTSIZ_XFRSIZ_SHIFT   0
#define DIEPTSIZ_XFRSIZ(n)      (((n) << DIEPTSIZ_XFRSIZ_SHIFT) & DIEPTSIZ_XFRSIZ_MASK)
#define DIEPTSIZ_PKTCNT_MASK    0x1FF80000UL
#define DIEPTSIZ_PKTCNT_SHIFT   19
#define DIEPTSIZ_PKTCNT(n)      (((n) << DIEPTSIZ_PKTCNT_SHIFT) & DIEPTSIZ_PKTCNT_MASK)
#define DIEPTSIZ_MCNT_MASK      0x60000000UL
#define DIEPTSIZ_MCNT_SHIFT     29
#define DIEPTSIZ_MCNT(n)        (((n) << DIEPTSIZ_MCNT_SHIFT) & DIEPTSIZ_MCNT_MASK)

/* ---- DOEPTSIZ ---- */
#define DOEPTSIZ_STUPCNT_MASK   0x18000000UL
#define DOEPTSIZ_STUPCNT_SHIFT  27
#define DOEPTSIZ_STUPCNT(n)     (((n) << DOEPTSIZ_STUPCNT_SHIFT) & DOEPTSIZ_STUPCNT_MASK)

/* ---- DIEPTXF0 / DIEPTXF ---- */
#define DIEPTXF_INEPTXSA_MASK   0x0000FFFFUL
#define DIEPTXF_INEPTXSA_SHIFT  0
#define DIEPTXF_INEPTXSA(n)     (((n) << DIEPTXF_INEPTXSA_SHIFT) & DIEPTXF_INEPTXSA_MASK)
#define DIEPTXF_INEPTXFD_MASK   0xFFFF0000UL
#define DIEPTXF_INEPTXFD_SHIFT  16
#define DIEPTXF_INEPTXFD(n)     (((n) << DIEPTXF_INEPTXFD_SHIFT) & DIEPTXF_INEPTXFD_MASK)

/* ---- GCCFG (0x038) ---- */
#define GCCFG_PWRDWN            0x00010000UL
#define GCCFG_VBDEN             0x00100000UL
#define GCCFG_VBUSASEN          0x00040000UL
#define GCCFG_VBUSBSEN          0x00080000UL
#define GCCFG_NOVBUSSENS        0x00200000UL

/* ---- DTXFSTS ---- */
#define DTXFSTS_INEPTFSAV_MASK  0x0000FFFFUL

/* ---- PCGCCTL (USB 0xE00 — NOT in USB_OTG_GlobalTypeDef) ---- */
#define _PCGCCTL                (*((volatile uint32_t *)(USB_OTG_FS_PERIPH_BASE + 0xE00)))

/* ========================================================================== */
/* DWC2 register access via CMSIS types                                       */
/* ========================================================================== */

#define _OTG                    ((USB_OTG_GlobalTypeDef *)USB_OTG_FS_PERIPH_BASE)
#define _DEV                    ((USB_OTG_DeviceTypeDef *)(USB_OTG_FS_PERIPH_BASE + 0x800))
#define _IN_EP(n)               ((USB_OTG_INEndpointTypeDef *)(USB_OTG_FS_PERIPH_BASE + 0x900 + (n) * 0x20))
#define _OUT_EP(n)              ((USB_OTG_OUTEndpointTypeDef *)(USB_OTG_FS_PERIPH_BASE + 0xB00 + (n) * 0x20))
#define _FIFO(n)                (*((volatile uint32_t *)(USB_OTG_FS_PERIPH_BASE + 0x1000 + (n) * 4)))

/* ========================================================================== */
/* Constants                                                                  */
/* ========================================================================== */

#define TRDT_VALUE_FS           5               /* Turn-around time for FS */
#define RX_FIFO_SIZE_WORDS      128             /* 512 bytes / 4 */
#define EP0_TX_FIFO_SIZE        64              /* EP0 TX FIFO words */
#define EP0_MAX_PACKET          64
#define EP1_TX_FIFO_SIZE        64              /* 256 bytes for EP1 bulk IN */
#define EP1_MAX_PACKET          64
#define SEND_TIMEOUT            50000           /* Poll loop timeout */

#define OTG_FIFO_MEM_SIZE       320             /* OTG1 FS FIFO size in words */

/* ========================================================================== */
/* Internal state                                                             */
/* ========================================================================== */

static struct {
    volatile bool   initialized;        /* DWC2 core initialized */
    volatile bool   enumerated;         /* Enumeration complete */
    volatile uint8_t device_addr;       /* Current USB address */
    volatile bool   configured;         /* Set config received */
    /* RAM allocator for TX FIFO space */
    uint32_t        pmnext;             /* Next free FIFO word offset */
} _usb;

/* ========================================================================== */
/* Forward declarations (internal helpers)                                    */
/* ========================================================================== */

static void _otg_core_reset(void);
static void _otg_txfifo_flush(uint32_t fifo_num);
static void _otg_rxfifo_flush(void);
static void _otg_disable_endpoints(void);
static void _otg_ram_reset(void);
static uint32_t _otg_ram_alloc(uint32_t size_words);
static void _otg_fifo_write(volatile uint32_t *fifop, const uint8_t *buf, size_t n);

/* ========================================================================== */
/* Internal: FIFO RAM allocator                                               */
/* ========================================================================== */

static void _otg_ram_reset(void)
{
    /* RX FIFO occupies words 0..RX_FIFO_SIZE_WORDS-1.
     * TX FIFO allocations start after the RX FIFO. */
    _usb.pmnext = RX_FIFO_SIZE_WORDS;
}

static uint32_t _otg_ram_alloc(uint32_t size_words)
{
    uint32_t addr = _usb.pmnext;
    _usb.pmnext += size_words;
    /* OTG_FS has 320 words total */
    if (_usb.pmnext > OTG_FIFO_MEM_SIZE) {
        _usb.pmnext = OTG_FIFO_MEM_SIZE;   /* clamp — caller must check */
    }
    return addr;
}

/* ========================================================================== */
/* Internal: FIFO operations                                                  */
/* ========================================================================== */

static void _otg_fifo_write(volatile uint32_t *fifop,
                            const uint8_t *buf, size_t n)
{
    if (n == 0) return;

    /* Write word-aligned data to FIFO */
    while (n > 4) {
        uint32_t w;
        memcpy(&w, buf, 4);
        *fifop = w;
        n -= 4;
        buf += 4;
    }
    /* Last partial word */
    if (n > 0) {
        uint32_t w = 0;
        memcpy(&w, buf, n);
        *fifop = w;
    }
}

/* ========================================================================== */
/* Internal: Core reset (ChibiOS: otg_core_reset)                             */
/* ========================================================================== */

static void _otg_core_reset(void)
{
    /* ChibiOS reference: hal_usb_lld.c:136-150 (otg_core_reset) */

    /* Wait AHB idle */
    while ((_OTG->GRSTCTL & GRSTCTL_AHBIDL) == 0) {}

    /* Write CSRST */
    _OTG->GRSTCTL = GRSTCTL_CSRST;
    (void)_OTG->GRSTCTL;

    /* Wait ~3 PHY cycles (12 AHB cycles @ 216MHz ≈ 56ns ≈ fine) */
    {
        volatile uint32_t _d = 20;
        while (_d--) { __NOP(); }
    }

    /* Wait CSRST self-clears (RM0410: wait CSRST=0) */
    uint32_t timeout = 100000;
    while ((_OTG->GRSTCTL & GRSTCTL_CSRST) != 0) {
        if (--timeout == 0) break;
        __NOP();
    }

    /* Additional delay — 3 PHY cycles */
    {
        volatile uint32_t _d = 20;
        while (_d--) { __NOP(); }
    }

    /* Wait AHB idle again */
    while ((_OTG->GRSTCTL & GRSTCTL_AHBIDL) != 0) {}
}

/* ========================================================================== */
/* Internal: FIFO flush (ChibiOS: otg_txfifo_flush / otg_rxfifo_flush)        */
/* ========================================================================== */

static void _otg_txfifo_flush(uint32_t fifo_num)
{
    /* ChibiOS reference: hal_usb_lld.c:172-178 */

    _OTG->GRSTCTL = GRSTCTL_TXFNUM(fifo_num) | GRSTCTL_TXFFLSH;
    while ((_OTG->GRSTCTL & GRSTCTL_TXFFLSH) != 0) {}
    /* Wait 3 PHY clocks */
    {
        volatile uint32_t _d = 20;
        while (_d--) { __NOP(); }
    }
}

static void _otg_rxfifo_flush(void)
{
    /* ChibiOS reference: hal_usb_lld.c:163-169 */

    _OTG->GRSTCTL = GRSTCTL_RXFFLSH;
    while ((_OTG->GRSTCTL & GRSTCTL_RXFFLSH) != 0) {}
    {
        volatile uint32_t _d = 20;
        while (_d--) { __NOP(); }
    }
}

/* ========================================================================== */
/* Internal: Disable all endpoints (ChibiOS: otg_disable_ep)                  */
/* ========================================================================== */

static void _otg_disable_endpoints(void)
{
    /* ChibiOS reference: hal_usb_lld.c:152-161 */
    unsigned i;

    for (i = 0; i <= 4; i++) {             /* EP0-4, OTG FS has max 4 */ 
        if ((_IN_EP(i)->DIEPCTL & DIEPCTL_EPENA) != 0) {
            _IN_EP(i)->DIEPCTL |= DIEPCTL_EPDIS;
        }
        if ((_OUT_EP(i)->DOEPCTL & DIEPCTL_EPENA) != 0) {
            _OUT_EP(i)->DOEPCTL |= DIEPCTL_EPDIS;
        }
        _IN_EP(i)->DIEPINT = 0xFFFFFFFFU;
        _OUT_EP(i)->DOEPINT = 0xFFFFFFFFU;
    }
    _DEV->DAINTMSK = DAINTMSK_OEPM(0) | DAINTMSK_IEPM(0);
}

/* ========================================================================== */
/* Internal: Reset state after USB bus reset (ChibiOS: usb_lld_reset)         */
/* ========================================================================== */

static void _usb_reset(void)
{
    /* ChibiOS reference: hal_usb_lld.c:934-979 */

    /* Flush TX FIFO 0 */
    _otg_txfifo_flush(0);

    /* Endpoint interrupts disabled */
    _DEV->DIEPEMPMSK = 0;
    _DEV->DAINTMSK = DAINTMSK_OEPM(0) | DAINTMSK_IEPM(0);

    /* All endpoints to NAK, clear interrupts */
    for (unsigned i = 0; i <= 4; i++) {
        _IN_EP(i)->DIEPCTL = DIEPCTL_SNAK;
        _OUT_EP(i)->DOEPCTL = DIEPCTL_SNAK;  /* SNAK same bit value for OUT EP */
        _IN_EP(i)->DIEPINT = 0xFFFFFFFFU;
        _OUT_EP(i)->DOEPINT = 0xFFFFFFFFU;
    }

    /* Reset RAM allocator */
    _otg_ram_reset();

    /* RX FIFO size (address always 0) */
    _OTG->GRXFSIZ = RX_FIFO_SIZE_WORDS;
    _otg_rxfifo_flush();

    /* Reset address to 0 */
    _DEV->DCFG = (_DEV->DCFG & ~DCFG_DAD_MASK) | DCFG_DAD(0);

    /* Enable EP-related interrupts */
    _OTG->GINTMSK |= GINTMSK_RXFLVLM | GINTMSK_OEPM | GINTMSK_IEPM;
    _DEV->DIEPMSK = DIEPMSK_XFRCM | DIEPMSK_TOM;
    _DEV->DOEPMSK = DOEPMSK_STUPM | DOEPMSK_XFRCM;

    /* ---- EP0 initialization (ChibiOS: hal_usb_lld.c:968-978) ---- */

    /* OUT EP0: setup packet count = 3 (back-to-back setup support) */
    _OUT_EP(0)->DOEPTSIZ = DOEPTSIZ_STUPCNT(3);
    _OUT_EP(0)->DOEPCTL = DIEPCTL_SD0PID | DIEPCTL_USBAEP |
                          DIEPCTL_EPTYP_CTRL | DIEPCTL_MPSIZ(EP0_MAX_PACKET);
    __DSB();

    /* IN EP0: clear TSIZ, set control */
    _IN_EP(0)->DIEPTSIZ = 0;
    _IN_EP(0)->DIEPCTL = DIEPCTL_SD0PID | DIEPCTL_USBAEP |
                         DIEPCTL_EPTYP_CTRL |
                         DIEPCTL_TXFNUM(0) | DIEPCTL_MPSIZ(EP0_MAX_PACKET);
    __DSB();

    /* EP0 TX FIFO: allocate after RX FIFO */
    uint32_t ep0_tx_words = EP0_TX_FIFO_SIZE / 4;
    _OTG->DIEPTXF0_HNPTXFSIZ =
        DIEPTXF_INEPTXFD(ep0_tx_words) |
        DIEPTXF_INEPTXSA(_otg_ram_alloc(ep0_tx_words));
    __DSB();

    /* EP1 TX FIFO: allocate for bulk IN */
    uint32_t ep1_tx_words = EP1_TX_FIFO_SIZE / 4;
    _OTG->DIEPTXF[0] =                              /* DIEPTXF[0] = EP1 TX FIFO */
        DIEPTXF_INEPTXFD(ep1_tx_words) |
        DIEPTXF_INEPTXSA(_otg_ram_alloc(ep1_tx_words));
    _otg_txfifo_flush(1);

    /* EP1: bulk IN endpoint */
    _IN_EP(1)->DIEPTSIZ = 0;
    _IN_EP(1)->DIEPCTL = DIEPCTL_SD0PID | DIEPCTL_USBAEP |
                         DIEPCTL_EPTYP_BULK |
                         DIEPCTL_TXFNUM(1) | DIEPCTL_MPSIZ(EP1_MAX_PACKET);
    _DEV->DAINTMSK |= DAINTMSK_IEPM(1);
    __DSB();

    /* Reset state */
    _usb.enumerated = false;
    _usb.device_addr = 0;
    _usb.configured = false;
}

/* ========================================================================== */
/* Exported: usb_lld_init_rtt                                                 */
/* ========================================================================== */

bool usb_lld_init_rtt(void)
{
    if (_usb.initialized) {
        return true;
    }

    /* ---- Step 1: Enable OTG_FS clock and reset (ChibiOS: hal_usb_lld.c:766-767) ---- */
    /* STM32F7: OTG FS is on AHB2 bus (RM0410 §6.4.6) */
    RCC->AHB2ENR |= RCC_AHB2ENR_OTGFSEN;
    (void)RCC->AHB2ENR;
    __DSB();

    RCC->AHB2RSTR |= RCC_AHB2RSTR_OTGFSRST;
    __DSB();
    RCC->AHB2RSTR &= ~RCC_AHB2RSTR_OTGFSRST;
    __DSB();

    /* ---- Step 2: GUSBCFG — forced device mode, FS 1.1 PHY (ChibiOS: L775) ---- */
    _OTG->GUSBCFG = GUSBCFG_FDMOD | GUSBCFG_TRDT(TRDT_VALUE_FS) |
                    GUSBCFG_PHYSEL;
    (void)_OTG->GUSBCFG;
    __DSB();

    /* ---- Step 3: DCFG — FS 1.1 PHY, 48MHz (ChibiOS: L779) ---- */
    _DEV->DCFG = 0x02200000UL | DCFG_DSPD_FS11;
    __DSB();

    /* ---- Step 4: PCGCCTL — enable PHY (ChibiOS: L834) ---- */
    _PCGCCTL = 0;
    __DSB();

    /* ---- Step 5: VBUS sensing + transceiver (ChibiOS: L837-853) ---- */
    _OTG->GOTGCTL = GOTGCTL_BVALOEN | GOTGCTL_BVALOVAL;

    /* GCCFG: stepping 2 (STM32F7 is OTG stepping 2) per RM0410 */
    _OTG->GCCFG = GCCFG_VBDEN | GCCFG_PWRDWN;
    __DSB();

    /* ---- Step 6: Core reset (ChibiOS: L856) ---- */
    _otg_core_reset();

    /* ---- Step 7: GAHBCFG — no DMA, no global int yet (ChibiOS: L858-859) ---- */
    _OTG->GAHBCFG = 0;
    __DSB();

    /* ---- Step 8: Disable endpoints + clear pending (ChibiOS: L862-881) ---- */
    _otg_disable_endpoints();
    _DEV->DIEPMSK = 0;
    _DEV->DOEPMSK = 0;
    _DEV->DAINTMSK = 0;

    /* Set GINTMSK — initial only reset/wakeup/suspend/enum, no SOF yet */
    _OTG->GINTMSK = GINTMSK_ENUMDNEM | GINTMSK_USBRSTM |
                    GINTMSK_USBSUSPM | GINTMSK_ESUSPM |
                    GINTMSK_SRQM | GINTMSK_WKUPM |
                    GINTMSK_IISOIXFRM | GINTMSK_IISOOXFRM;

    /* Clear all pending interrupts */
    _OTG->GINTSTS = 0xFFFFFFFFU;
    (void)_OTG->GINTSTS;

    /* ---- Step 9: Enable global interrupt (ChibiOS: L883) ---- */
    _OTG->GAHBCFG |= GAHBCFG_GINTMSK;
    __DSB();

    _usb.initialized = true;
    _usb.enumerated = false;
    _usb.device_addr = 0;
    _usb.configured = false;

    return true;
}

/* ========================================================================== */
/* Exported: usb_lld_poll_rtt                                                 */
/* ========================================================================== */

void usb_lld_poll_rtt(void)
{
    if (!_usb.initialized) {
        return;
    }

    uint32_t sts = _OTG->GINTSTS;
    sts &= _OTG->GINTMSK;
    if (sts == 0) {
        return;
    }

    /* Clear pending bits by writing them back */
    _OTG->GINTSTS = sts;
    (void)_OTG->GINTSTS;

    /* ---- USB Reset (ChibiOS: L540-546) ---- */
    if (sts & GINTMSK_USBRSTM) {
        _usb_reset();
        return;     /* Core was reset — no more handlers this tick */
    }

    /* ---- Wakeup (ChibiOS: L549-561) ---- */
    if (sts & GINTMSK_WKUPM) {
        if (_PCGCCTL & (0x00000001UL | 0x00000002UL)) {
            _PCGCCTL &= ~(0x00000001UL | 0x00000002UL);
        }
        _DEV->DCTL &= ~DCTL_RWUSIG;
    }

    /* ---- Suspend (ChibiOS: L564-570) ---- */
    if (sts & GINTMSK_USBSUSPM) {
        _otg_disable_endpoints();
    }

    /* ---- Enumeration done (ChibiOS: L573-583) ---- */
    if (sts & GINTMSK_ENUMDNEM) {
        /* Set turn-around based on speed */
        uint32_t spd = _DEV->DSTS & DSTS_ENUMSPD_MASK;
        if (spd == DSTS_ENUMSPD_HS) {
            _OTG->GUSBCFG = (_OTG->GUSBCFG & ~GUSBCFG_TRDT_MASK) |
                            GUSBCFG_TRDT(9);   /* TRDT = 9 for HS */
        } else {
            _OTG->GUSBCFG = (_OTG->GUSBCFG & ~GUSBCFG_TRDT_MASK) |
                            GUSBCFG_TRDT(TRDT_VALUE_FS);
        }
        _usb.enumerated = true;
    }

    /* ---- SOF (ChibiOS: L586-603) ---- */
    if (sts & GINTMSK_SOFM) {
        /* SOF not essential for polling mode — just keep alive */
    }

    /* ---- Iso IN/OUT failed ---- */
    if (sts & GINTMSK_IISOIXFRM) {
        /* Isochronous IN failed — not used in polling mode */
    }
    if (sts & GINTMSK_IISOOXFRM) {
        /* Isochronous OUT failed */
    }

    /* ---- RX FIFO data available (ChibiOS: L617-618) ---- */
    /* In a full driver, this would read setup packets and OUT data.
     * For polling mode, incoming setup/OUT data from RX FIFO is
     * handled by the higher layer (e.g. CherryUSB EP0 control path).
     * The RXFLVL interrupt is unmasked but we don't process it here
     * to keep the polling driver simple — the EP0 control path
     * should be handled separately. */
    if (sts & GINTMSK_RXFLVLM) {
        /* Read and discard RX FIFO to prevent overflow */
        uint32_t rxsts = _OTG->GRXSTSP;
        (void)rxsts;
    }

    /* ---- IN/OUT endpoint interrupts ---- */
    if (sts & GINTMSK_IEPM) {
        uint32_t daint = _DEV->DAINT;
        /* Check EP0 IN */
        if (daint & (1 << 0)) {
            uint32_t epint = _IN_EP(0)->DIEPINT;
            _IN_EP(0)->DIEPINT = epint;    /* Clear */
        }
        /* Check EP1 IN */
        if (daint & (1 << 1)) {
            uint32_t epint = _IN_EP(1)->DIEPINT;
            _IN_EP(1)->DIEPINT = epint;    /* Clear */
        }
    }

    if (sts & GINTMSK_OEPM) {
        uint32_t daint = _DEV->DAINT;
        if (daint & (1 << 16)) {             /* OEP 0 */
            uint32_t epint = _OUT_EP(0)->DOEPINT;
            _OUT_EP(0)->DOEPINT = epint;    /* Clear */
        }
    }
}

/* ========================================================================== */
/* Exported: usb_lld_send_rtt                                                 */
/* ========================================================================== */

bool usb_lld_send_rtt(uint8_t ep, const uint8_t *data, uint32_t len)
{
    if (!_usb.initialized || ep > 3) {
        return false;
    }

    uint32_t max_pkt = (ep == 0) ? EP0_MAX_PACKET : EP1_MAX_PACKET;
    if (len > max_pkt) {
        len = max_pkt;      /* Single packet per call */
    }

    /* ---- 1. Check TX FIFO space ---- */
    uint32_t fifo_avail = _IN_EP(ep)->DTXFSTS & DTXFSTS_INEPTFSAV_MASK;
    uint32_t needed = (len + 3) / 4;            /* Bytes → words */
    if (fifo_avail < needed) {
        return false;       /* FIFO full — try again later */
    }

    /* ---- 2. Write data to FIFO (ChibiOS: otg_fifo_write_from_buffer) ---- */
    volatile uint32_t *fifop = &_FIFO(ep);
    _otg_fifo_write(fifop, data, len);

    /* ---- 3. Set DIEPTSIZ (ChibiOS: usb_lld_start_in L1200-1213) ---- */
    uint32_t pcnt = (len + max_pkt - 1) / max_pkt;
    uint32_t xfrsiz = len;

    _IN_EP(ep)->DIEPTSIZ = DIEPTSIZ_MCNT(1) |
                           DIEPTSIZ_PKTCNT(pcnt) |
                           DIEPTSIZ_XFRSIZ(xfrsiz);
    __DSB();

    /* ---- 4. Enable endpoint: EPENA | CNAK (ChibiOS: L1226) ---- */
    _IN_EP(ep)->DIEPCTL |= DIEPCTL_EPENA | DIEPCTL_CNAK;
    __DSB();

    /* ---- 5. Poll DIEPINT for XFRC (transfer complete) ---- */
    uint32_t timeout = SEND_TIMEOUT;
    while (!(_IN_EP(ep)->DIEPINT & DIEPINT_XFRC)) {
        if (--timeout == 0) {
            /* Timeout — clear endpoint state */
            _IN_EP(ep)->DIEPCTL |= DIEPCTL_EPDIS;
            _IN_EP(ep)->DIEPINT = 0xFFFFFFFFU;
            return false;
        }
        __NOP();
    }

    /* ---- 6. Clear XFRC ---- */
    _IN_EP(ep)->DIEPINT = DIEPINT_XFRC;

    return true;
}

/* ========================================================================== */
/* Exported: usb_lld_set_address_rtt                                          */
/* ========================================================================== */

void usb_lld_set_address_rtt(uint8_t addr)
{
    /* ChibiOS reference: usb_lld_set_address (hal_usb_lld.c:988-992) */
    _DEV->DCFG = (_DEV->DCFG & ~DCFG_DAD_MASK) | DCFG_DAD(addr);
    _usb.device_addr = addr;
}

/* ========================================================================== */
/* Exported: usb_lld_get_connected_rtt                                        */
/* ========================================================================== */

bool usb_lld_get_connected_rtt(void)
{
    return _usb.enumerated;
}
