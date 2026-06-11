/*
 * RTT adaptation of ChibiOS STM32 OTGv1 USB LLD (hal_usb_lld_rtt.c)
 *
 * 1:1 port of modules/ChibiOS/os/hal/ports/STM32/LLD/OTGv1/hal_usb_lld.c
 * with the HAL state machine (_usb_reset, _usb_ep0setup, etc.) from
 * modules/ChibiOS/os/hal/src/hal_usb.c.
 *
 * Self-contained: no ChibiOS dependency, no CherryUSB dependency.
 * Targeted at STM32F767 (CUAV V5) with OTG_FS.
 *
 * Register layout: ChibiOS stm32_otg_t (single struct covering all DWC2 regs).
 * CMSIS headers used for RCC, NVIC, GPIO, and core.
 *
 * ChibiOS Copyright (C) 2006..2018 Giovanni Di Sirio
 *   Licensed under the Apache License, Version 2.0
 */

/* ========================================================================== */
/* Includes                                                                   */
/* ========================================================================== */

#include "hal_usb_lld_rtt.h"
#include "usb_cdc_rtt.h"
#include <rtthread.h>
#include <stm32f7xx.h>
#include <string.h>

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))
#endif

/* Debug counters (referenced by rtt_ctl_telemetry.c) */
volatile uint32_t rtt_dbg_usb_init       = 0;
volatile uint32_t rtt_dbg_usb_usbrst     = 0;
volatile uint32_t rtt_dbg_usb_enumdne    = 0;
volatile uint32_t rtt_dbg_usb_setup_stup = 0;
volatile uint32_t rtt_dbg_usb_set_addr   = 0;
volatile uint32_t rtt_dbg_usb_ep0_cont   = 0;  /* EP0 IN multi-packet continuation in ISR */
volatile uint32_t rtt_dbg_usb_ep0_zlp    = 0;  /* EP0 IN ZLP after exact-MPS data stage */
volatile uint32_t rtt_dbg_usb_ep0_sts_in = 0;  /* EP0 STATUS IN ZLP (host→device, wLength=0) */

/* ---- End of Debug Counters ---- */

/* ========================================================================== */
/* stm32_otg_t — DWC2 register map (ORIGINAL hybrid layout for F767 stepping 2)
 * Host registers at 0x400, channels at 0x500,
 * Device registers at 0x800, IN endpoints at 0x900, OUT endpoints at 0xB00.
 * DCFG writable at 0x800, read-only aliased at 0x400 (F767 stepping 2 quirk).
 * DCTL writable at both 0x404 and 0x804.
 * Host HCFG/HFIR/HFNUM/HPTXSTS writable at 0x400.
 * See RM0410 §44.10 and experimental HW verification on F767 stepping 2.     */
/* ========================================================================== */

typedef struct {
    volatile uint32_t HCCHAR;
    volatile uint32_t resvd8;
    volatile uint32_t HCINT;
    volatile uint32_t HCINTMSK;
    volatile uint32_t HCTSIZ;
    volatile uint32_t resvd14;
    volatile uint32_t resvd18;
    volatile uint32_t resvd1c;
} stm32_otg_host_chn_t;

typedef struct {
    volatile uint32_t DIEPCTL;
    volatile uint32_t resvd4;
    volatile uint32_t DIEPINT;
    volatile uint32_t resvdC;
    volatile uint32_t DIEPTSIZ;
    volatile uint32_t resvd14;
    volatile uint32_t DTXFSTS;
    volatile uint32_t resvd1C;
} stm32_otg_in_ep_t;

typedef struct {
    volatile uint32_t DOEPCTL;
    volatile uint32_t resvd4;
    volatile uint32_t DOEPINT;
    volatile uint32_t resvdC;
    volatile uint32_t DOEPTSIZ;
    volatile uint32_t resvd14;
    volatile uint32_t resvd18;
    volatile uint32_t resvd1C;
} stm32_otg_out_ep_t;

typedef struct {
    /* Global registers — offset 0x000 */
    volatile uint32_t GOTGCTL;
    volatile uint32_t GOTGINT;
    volatile uint32_t GAHBCFG;
    volatile uint32_t GUSBCFG;
    volatile uint32_t GRSTCTL;
    volatile uint32_t GINTSTS;
    volatile uint32_t GINTMSK;
    volatile uint32_t GRXSTSR;
    volatile uint32_t GRXSTSP;
    volatile uint32_t GRXFSIZ;
    volatile uint32_t DIEPTXF0;
    volatile uint32_t HNPTXSTS;
    volatile uint32_t resvd30;
    volatile uint32_t resvd34;
    volatile uint32_t GCCFG;
    volatile uint32_t CID;
    volatile uint32_t resvd58[48];     /* 0x040 – 0x0FF */
    volatile uint32_t HPTXFSIZ;        /* 0x100 */
    volatile uint32_t DIEPTXF[15];     /* 0x104 – 0x13C */
    volatile uint32_t resvd140[176];   /* 0x140 – 0x3FF */

    /* Host-mode registers — offset 0x400 (HOST layout, original position) */
    volatile uint32_t HCFG;            /* 0x400 */
    volatile uint32_t HFIR;            /* 0x404 */
    volatile uint32_t HFNUM;           /* 0x408 */
    volatile uint32_t resvd40C;        /* 0x40C */
    volatile uint32_t HPTXSTS;         /* 0x410 */
    volatile uint32_t HAINT;           /* 0x414 */
    volatile uint32_t HAINTMSK;        /* 0x418 */
    volatile uint32_t resvd41C[9];     /* 0x41C – 0x43C */
    volatile uint32_t HPRT;            /* 0x440 */
    volatile uint32_t resvd444[47];    /* 0x444 – 0x4FC */
    stm32_otg_host_chn_t hc[16];      /* 0x500 – 0x6FC */
    volatile uint32_t resvd700[64];    /* 0x700 – 0x7FC */

    /* Device-mode registers — offset 0x800 (DEVICE layout, original position) */
    volatile uint32_t DCFG;            /* 0x800 */
    volatile uint32_t DCTL;            /* 0x804 */
    volatile uint32_t DSTS;            /* 0x808 */
    volatile uint32_t resvd80C;        /* 0x80C */
    volatile uint32_t DIEPMSK;         /* 0x810 */
    volatile uint32_t DOEPMSK;         /* 0x814 */
    volatile uint32_t DAINT;           /* 0x818 */
    volatile uint32_t DAINTMSK;        /* 0x81C */
    volatile uint32_t resvd820;        /* 0x820 */
    volatile uint32_t resvd824;        /* 0x824 */
    volatile uint32_t DVBUSDIS;        /* 0x828 */
    volatile uint32_t DVBUSPULSE;      /* 0x82C */
    volatile uint32_t resvd830;        /* 0x830 */
    volatile uint32_t DIEPEMPMSK;      /* 0x834 */
    volatile uint32_t resvd838;        /* 0x838 */
    volatile uint32_t resvd83C;        /* 0x83C */
    volatile uint32_t resvd840[16];    /* 0x840 – 0x87C */
    volatile uint32_t resvd880[16];    /* 0x880 – 0x8BC */
    volatile uint32_t resvd8C0[16];    /* 0x8C0 – 0x8FC */

    /* IN endpoint registers — offset 0x900 (original position) */
    stm32_otg_in_ep_t  ie[16];         /* 0x900 – 0xAFC (16 * 0x20) */

    /* OUT endpoint registers — offset 0xB00 (original position)
       NOTE: ie[8..15] and oe[0..7] overlap at 0xA00-0xAFF
       (DWC2 hardware design — the struct address overlap is expected) */
    stm32_otg_out_ep_t oe[16];         /* 0xB00 – 0xCFC (16 * 0x20) */
    volatile uint32_t resvdD00[64];    /* 0xD00 – 0xDFC */

    volatile uint32_t PCGCCTL;         /* 0xE00 */
    volatile uint32_t resvdE04[127];   /* 0xE04 – 0xEFF */

    /* FIFO access — 0x1000+ */
    volatile uint32_t FIFO[16][1024];
} stm32_otg_t;

/* ========================================================================== */
/* Register bit definitions (ChibiOS stm32_otg.h, 1:1)                       */
/* ========================================================================== */

/* GOTGCTL */
#define GOTGCTL_BSVLD           (1U << 19)
#define GOTGCTL_ASVLD           (1U << 18)
#define GOTGCTL_BVALOVAL        (1U << 7)
#define GOTGCTL_BVALOEN         (1U << 6)

/* GAHBCFG */
#define GAHBCFG_PTXFELVL        (1U << 8)
#define GAHBCFG_TXFELVL         (1U << 7)
#define GAHBCFG_DMAEN           (1U << 5)
#define GAHBCFG_GINTMSK         (1U << 0)

/* GUSBCFG */
#define GUSBCFG_CTXPKT          (1U << 31)
#define GUSBCFG_FDMOD           (1U << 30)
#define GUSBCFG_FHMOD           (1U << 29)
#define GUSBCFG_TRDT_MASK       (15U << 10)
#define GUSBCFG_TRDT(n)         ((n) << 10)
#define GUSBCFG_HNPCAP          (1U << 9)
#define GUSBCFG_SRPCAP          (1U << 8)
#define GUSBCFG_PHYSEL          (1U << 6)

/* GRSTCTL */
#define GRSTCTL_AHBIDL          (1U << 31)
#define GRSTCTL_TXFNUM_MASK     (31U << 6)
#define GRSTCTL_TXFNUM(n)       ((n) << 6)
#define GRSTCTL_TXFFLSH         (1U << 5)
#define GRSTCTL_RXFFLSH         (1U << 4)
#define GRSTCTL_FCRST           (1U << 2)
#define GRSTCTL_HSRST           (1U << 1)
#define GRSTCTL_CSRST           (1U << 0)

/* GINTSTS / GINTMSK */
#define GINTSTS_WKUPINT         (1U << 31)
#define GINTSTS_SRQINT          (1U << 30)
#define GINTSTS_DISCINT         (1U << 29)
#define GINTSTS_CIDSCHG         (1U << 28)
#define GINTSTS_PTXFE           (1U << 26)
#define GINTSTS_HCINT           (1U << 25)
#define GINTSTS_HPRTINT         (1U << 24)
#define GINTSTS_IPXFR           (1U << 21)
#define GINTSTS_IISOOXFR        (1U << 21)
#define GINTSTS_IISOIXFR        (1U << 20)
#define GINTSTS_OEPINT          (1U << 19)
#define GINTSTS_IEPINT          (1U << 18)
#define GINTSTS_EOPF            (1U << 15)
#define GINTSTS_ISOODRP         (1U << 14)
#define GINTSTS_ENUMDNE         (1U << 13)
#define GINTSTS_USBRST          (1U << 12)
#define GINTSTS_USBSUSP         (1U << 11)
#define GINTSTS_ESUSP           (1U << 10)
#define GINTSTS_GONAKEFF        (1U << 7)
#define GINTSTS_GINAKEFF        (1U << 6)
#define GINTSTS_NPTXFE          (1U << 5)
#define GINTSTS_RXFLVL          (1U << 4)
#define GINTSTS_SOF             (1U << 3)
#define GINTSTS_OTGINT          (1U << 2)
#define GINTSTS_MMIS            (1U << 1)
#define GINTSTS_CMOD            (1U << 0)

#define GINTMSK_WKUM            (1U << 31)
#define GINTMSK_SRQM            (1U << 30)
#define GINTMSK_DISCM           (1U << 29)
#define GINTMSK_CIDSCHGM        (1U << 28)
#define GINTMSK_PTXFEM          (1U << 26)
#define GINTMSK_HCM             (1U << 25)
#define GINTMSK_HPRTM           (1U << 24)
#define GINTMSK_IPXFRM          (1U << 21)
#define GINTMSK_IISOOXFRM       (1U << 21)
#define GINTMSK_IISOIXFRM       (1U << 20)
#define GINTMSK_OEPM            (1U << 19)
#define GINTMSK_IEPM            (1U << 18)
#define GINTMSK_EOPFM           (1U << 15)
#define GINTMSK_ISOODRPM        (1U << 14)
#define GINTMSK_ENUMDNEM        (1U << 13)
#define GINTMSK_USBRSTM         (1U << 12)
#define GINTMSK_USBSUSPM        (1U << 11)
#define GINTMSK_ESUSPM          (1U << 10)
#define GINTMSK_GONAKEFFM       (1U << 7)
#define GINTMSK_GINAKEFFM       (1U << 6)
#define GINTMSK_NPTXFEM         (1U << 5)
#define GINTMSK_RXFLVLM         (1U << 4)
#define GINTMSK_SOFM            (1U << 3)
#define GINTMSK_OTGM            (1U << 2)
#define GINTMSK_MMISM           (1U << 1)

/* GRXSTSP / GRXSTSR */
#define GRXSTSP_PKTSTS_MASK     (15U << 17)
#define GRXSTSP_PKTSTS(n)       ((n) << 17)
#define GRXSTSP_OUT_GLOBAL_NAK  GRXSTSP_PKTSTS(1)
#define GRXSTSP_OUT_DATA        GRXSTSP_PKTSTS(2)
#define GRXSTSP_OUT_COMP        GRXSTSP_PKTSTS(3)
#define GRXSTSP_SETUP_COMP      GRXSTSP_PKTSTS(4)
#define GRXSTSP_SETUP_DATA      GRXSTSP_PKTSTS(6)
#define GRXSTSP_BCNT_MASK       (0x7FFU << 4)
#define GRXSTSP_BCNT_OFF        4
#define GRXSTSP_EPNUM_MASK      (15U << 0)
#define GRXSTSP_EPNUM_OFF       0

/* GCCFG */
#define GCCFG_NOVBUSSENS        (1U << 21)
#define GCCFG_SOFOUTEN          (1U << 20)
#define GCCFG_VBUSBSEN          (1U << 19)
#define GCCFG_VBUSASEN          (1U << 18)
#define GCCFG_VBDEN             (1U << 21)
#define GCCFG_PWRDWN            (1U << 16)

/* DCFG */
#define DCFG_PFIVL_MASK         (3U << 11)
#define DCFG_PFIVL(n)           ((n) << 11)
#define DCFG_DAD_MASK           (0x7FU << 4)
#define DCFG_DAD(n)             ((n) << 4)
#define DCFG_NZLSOHSK           (1U << 2)
#define DCFG_DSPD_MASK          (3U << 0)
#define DCFG_DSPD_HS            (0U << 0)
#define DCFG_DSPD_HS_FS         (1U << 0)
#define DCFG_DSPD_FS11          (3U << 0)

/* DSTS */
#define DSTS_FNSOF_MASK         (0x3FFU << 8)
#define DSTS_FNSOF(n)           ((n) << 8)
#define DSTS_FNSOF_ODD          (1U << 8)
#define DSTS_EERR               (1U << 3)
#define DSTS_ENUMSPD_MASK       (3U << 1)
#define DSTS_ENUMSPD_FS_48      (3U << 1)
#define DSTS_ENUMSPD_HS_480     (0U << 1)
#define DSTS_SUSPSTS            (1U << 0)

/* DCTL */
#define DCTL_POPRGDNE           (1U << 11)
#define DCTL_CGONAK             (1U << 10)
#define DCTL_SGONAK             (1U << 9)
#define DCTL_CGINAK             (1U << 8)
#define DCTL_SGINAK             (1U << 7)
#define DCTL_GONSTS             (1U << 3)
#define DCTL_GINSTS             (1U << 2)
#define DCTL_SDIS               (1U << 1)
#define DCTL_RWUSIG             (1U << 0)

/* DIEPMSK */
#define DIEPMSK_TXFEM           (1U << 6)
#define DIEPMSK_INEPNEM         (1U << 6)
#define DIEPMSK_ITTXFEMSK       (1U << 4)
#define DIEPMSK_TOCM            (1U << 3)
#define DIEPMSK_EPDM            (1U << 1)
#define DIEPMSK_XFRCM           (1U << 0)

/* DOEPMSK */
#define DOEPMSK_OTEPDM          (1U << 4)
#define DOEPMSK_STUPM           (1U << 3)
#define DOEPMSK_EPDM            (1U << 1)
#define DOEPMSK_XFRCM           (1U << 0)

/* DAINT / DAINTMSK */
#define DAINT_OEPINT(n)         (1U << (16 + (n)))
#define DAINT_IEPINT(n)         (1U << (n))
#define DAINTMSK_OEPM(n)        (1U << (16 + (n)))
#define DAINTMSK_IEPM(n)        (1U << (n))

/* DIEPCTL / DOEPCTL */
#define DIEPCTL_EPENA           (1U << 31)
#define DIEPCTL_EPDIS           (1U << 30)
#define DIEPCTL_SD1PID          (1U << 29)
#define DIEPCTL_SODDFRM         (1U << 29)
#define DIEPCTL_SD0PID          (1U << 28)
#define DIEPCTL_SEVNFRM         (1U << 28)
#define DIEPCTL_SNAK            (1U << 27)
#define DIEPCTL_CNAK            (1U << 26)
#define DIEPCTL_TXFNUM_MASK     (15U << 22)
#define DIEPCTL_TXFNUM(n)       ((n) << 22)
#define DIEPCTL_STALL           (1U << 21)
#define DIEPCTL_SNPM            (1U << 20)
#define DIEPCTL_EPTYP_MASK      (3U << 18)
#define DIEPCTL_EPTYP_CTRL      (0U << 18)
#define DIEPCTL_EPTYP_ISO       (1U << 18)
#define DIEPCTL_EPTYP_BULK      (2U << 18)
#define DIEPCTL_EPTYP_INTR      (3U << 18)
#define DIEPCTL_NAKSTS          (1U << 17)
#define DIEPCTL_EONUM           (1U << 16)
#define DIEPCTL_DPID            (1U << 16)
#define DIEPCTL_USBAEP          (1U << 15)
#define DIEPCTL_MPSIZ_MASK      (0x3FFU << 0)
#define DIEPCTL_MPSIZ(n)        ((n) << 0)

/* DIEPINT */
#define DIEPINT_TXFE            (1U << 7)
#define DIEPINT_INEPNE          (1U << 6)
#define DIEPINT_ITTXFE          (1U << 4)
#define DIEPINT_TOC             (1U << 3)
#define DIEPINT_EPDISD          (1U << 1)
#define DIEPINT_XFRC            (1U << 0)

/* DIEPTSIZ */
#define DIEPTSIZ_MCNT_MASK      (3U << 29)
#define DIEPTSIZ_MCNT(n)        ((n) << 29)
#define DIEPTSIZ_PKTCNT_MASK    (0x3FFU << 19)
#define DIEPTSIZ_PKTCNT(n)      ((n) << 19)
#define DIEPTSIZ_XFRSIZ_MASK    (0x7FFFFU << 0)
#define DIEPTSIZ_XFRSIZ(n)      ((n) << 0)

/* DOEPCTL (same bits as DIEPCTL) */
#define DOEPCTL_EPENA           DIEPCTL_EPENA
#define DOEPCTL_EPDIS           DIEPCTL_EPDIS
#define DOEPCTL_SD1PID          DIEPCTL_SD1PID
#define DOEPCTL_SODDFRM         DIEPCTL_SODDFRM
#define DOEPCTL_SD0PID          DIEPCTL_SD0PID
#define DOEPCTL_SEVNFRM         DIEPCTL_SEVNFRM
#define DOEPCTL_SNAK            DIEPCTL_SNAK
#define DOEPCTL_CNAK            DIEPCTL_CNAK
#define DOEPCTL_STALL           DIEPCTL_STALL
#define DOEPCTL_SNPM            DIEPCTL_SNPM
#define DOEPCTL_EPTYP_MASK      DIEPCTL_EPTYP_MASK
#define DOEPCTL_EPTYP_CTRL      DIEPCTL_EPTYP_CTRL
#define DOEPCTL_EPTYP_ISO       DIEPCTL_EPTYP_ISO
#define DOEPCTL_EPTYP_BULK      DIEPCTL_EPTYP_BULK
#define DOEPCTL_EPTYP_INTR      DIEPCTL_EPTYP_INTR
#define DOEPCTL_NAKSTS          DIEPCTL_NAKSTS
#define DOEPCTL_EONUM           DIEPCTL_EONUM
#define DOEPCTL_DPID            DIEPCTL_DPID
#define DOEPCTL_USBAEP          DIEPCTL_USBAEP
#define DOEPCTL_MPSIZ_MASK      DIEPCTL_MPSIZ_MASK
#define DOEPCTL_MPSIZ(n)        DIEPCTL_MPSIZ(n)

/* DOEPINT */
#define DOEPINT_SETUP_RCVD      (1U << 15)
#define DOEPINT_B2BSTUP         (1U << 6)
#define DOEPINT_OTEPDIS         (1U << 4)
#define DOEPINT_STUP            (1U << 3)
#define DOEPINT_EPDISD          (1U << 1)
#define DOEPINT_XFRC            (1U << 0)

/* DOEPTSIZ */
#define DOEPTSIZ_RXDPID_MASK    (3U << 29)
#define DOEPTSIZ_RXDPID(n)      ((n) << 29)
#define DOEPTSIZ_STUPCNT_MASK   (3U << 29)
#define DOEPTSIZ_STUPCNT(n)     ((n) << 29)
#define DOEPTSIZ_PKTCNT_MASK    (0x3FFU << 19)
#define DOEPTSIZ_PKTCNT(n)      ((n) << 19)
#define DOEPTSIZ_XFRSIZ_MASK    (0x7FFFFU << 0)
#define DOEPTSIZ_XFRSIZ(n)      ((n) << 0)

/* DTXFSTS */
#define DTXFSTS_INEPTFSAV_MASK  (0xFFFFU << 0)

/* DIEPTXF */
#define DIEPTXF_INEPTXFD_MASK   (0xFFFFU << 16)
#define DIEPTXF_INEPTXFD(n)     ((n) << 16)
#define DIEPTXF_INEPTXSA_MASK   (0xFFFFU << 0)
#define DIEPTXF_INEPTXSA(n)     ((n) << 0)

/* DIEPEMPMSK */
#define DIEPEMPMSK_INEPTXFEM(n) (1U << (n))

/* PCGCCTL */
#define PCGCCTL_PHYSUSP         (1U << 4)
#define PCGCCTL_GATEHCLK        (1U << 1)
#define PCGCCTL_STPPCLK         (1U << 0)

/* GRXFSIZ */
#define GRXFSIZ_RXFD(n)         ((n) << 0)

/* ========================================================================== */
/* Driver local definitions                                                   */
/* ========================================================================== */

#define TRDT_VALUE_FS           5
#define TRDT_VALUE_HS           9

/* FIFO sizes in words (32-bit words, not bytes) */
#define RX_FIFO_SIZE_WORDS      128     /* 512 bytes */
#define EP0_TX_FIFO_SIZE_WORDS  16      /* 64 bytes */
#define EP1_TX_FIFO_SIZE_WORDS  32      /* 128 bytes for CDC bulk IN */
#define EP3_TX_FIFO_SIZE_WORDS  4       /* 16 bytes for CDC notification */

/* Maximum packet sizes */
#define EP0_MAX_PACKET          64
#define EP1_MAX_PACKET          64
#define EP2_MAX_PACKET          64

#define OTG_FIFO_MEM_SIZE       320     /* OTG1 FS FIFO RAM in words */
#define NUM_ENDPOINTS           4       /* EP0..EP3 */
#define OTG_FS_BASE             0x50000000UL  /* STM32F7 OTG_FS base */

/* STM32F767 is OTG stepping 2 */
#define STM32_OTG_STEPPING      2

/* GCCFG init value for stepping 2 with internal VBUS sensing */
#define GCCFG_INIT_VALUE        (GCCFG_VBDEN | GCCFG_PWRDWN)

/* OSAL substitutes */
#define osalSysPolledDelayX(n)  do { volatile uint32_t _d = (n); while (_d--) { __NOP(); } } while (0)

/* ========================================================================== */
/* Peripheral-specific parameters block                                       */
/* ========================================================================== */

typedef struct {
    uint32_t rx_fifo_size;
    uint32_t otg_ram_size;
    uint32_t num_endpoints;
} stm32_otg_params_t;

static const stm32_otg_params_t fs_params = {
    RX_FIFO_SIZE_WORDS,
    OTG_FIFO_MEM_SIZE,
    NUM_ENDPOINTS
};

/* ========================================================================== */
/* EP0 static state structures (ChibiOS pattern)                              */
/* ========================================================================== */

/* EP0 uses both IN and OUT states (union — never used simultaneously) */
static union {
    USBInEndpointState  in;
    USBOutEndpointState out;
} ep0_state;

/* EP0 setup buffer (8 bytes for one SETUP packet) */
static uint8_t ep0setup_buffer[8];

/* Forward declarations of EP0 callback functions */
static void _usb_ep0setup(void *usbp, usbep_t ep);
static void _usb_ep0in(void *usbp, usbep_t ep);
static void _usb_ep0out(void *usbp, usbep_t ep);
static bool default_handler(void *usbp);
static void _usb_reset(void *usbp);
static void _usb_suspend(void *usbp);
static void _usb_wakeup(void *usbp);

/* EP0 endpoint configuration */
static RT_USBEndpointConfig ep0config;

/* Initialize ep0 config at runtime (C++ can't use static addresses in designated initializers) */
static void ep0config_init(void)
{
    ep0config.ep_mode       = USB_EP_MODE_TYPE_CTRL;
    ep0config.setup_cb      = _usb_ep0setup;
    ep0config.in_cb         = _usb_ep0in;
    ep0config.out_cb        = _usb_ep0out;
    ep0config.in_maxsize    = EP0_MAX_PACKET;
    ep0config.out_maxsize   = EP0_MAX_PACKET;
    ep0config.in_state      = &ep0_state.in;
    ep0config.out_state     = &ep0_state.out;
    ep0config.in_multiplier = 1;
    ep0config.setup_buf     = ep0setup_buffer;
}

/* ========================================================================== */
/* USB descriptors (CDC ACM with IAD) — fallback if no get_descriptor_cb      */
/* ========================================================================== */
/* ========================================================================== */

static const uint8_t usb_dev_desc[] = {
    18,                    /* bLength */
    1,                     /* bDescriptorType = DEVICE */
    0x00, 0x02,            /* bcdUSB = 2.00 */
    0xEF,                  /* bDeviceClass: Misc (IAD) */
    0x02,                  /* bDeviceSubClass: Common */
    0x01,                  /* bDeviceProtocol: IAD */
    EP0_MAX_PACKET,        /* bMaxPacketSize0 */
    0x09, 0x12,            /* idVendor = 0x1209 */
    0x41, 0x57,            /* idProduct = 0x5741 */
    0x00, 0x02,            /* bcdDevice = 2.00 */
    0x01,                  /* iManufacturer */
    0x02,                  /* iProduct */
    0x03,                  /* iSerialNumber */
    0x01                   /* bNumConfigurations */
};

static const uint8_t usb_cfg_desc[] = {
    /* Configuration descriptor */
    9,                     /* bLength */
    2,                     /* bDescriptorType = CONFIGURATION */
    0x4B, 0x00,            /* wTotalLength = 75 */
    2,                     /* bNumInterfaces */
    1,                     /* bConfigurationValue */
    0,                     /* iConfiguration */
    0xC0,                  /* bmAttributes: Self-powered */
    50,                    /* bMaxPower = 100mA */

    /* IAD */
    8,                     /* bLength */
    0x0B,                  /* bDescriptorType = IAD */
    0,                     /* bFirstInterface */
    2,                     /* bInterfaceCount */
    2,                     /* bFunctionClass = CDC Comm */
    2,                     /* bFunctionSubClass = ACM */
    1,                     /* bFunctionProtocol = AT */
    0,                     /* iFunction */

    /* Interface 0: CDC Communication */
    9, 4,                  /* bLength, bDescriptorType = INTERFACE */
    0, 0,                  /* bInterfaceNumber, bAlternateSetting */
    1,                     /* bNumEndpoints */
    2, 2, 1,               /* bInterfaceClass/SubClass/Protocol */
    0,                     /* iInterface */

    /* CDC Header FD */
    5, 0x24, 0x00,         /* bLength, CS_INTERFACE, HEADER */
    0x10, 0x01,            /* bcdCDC = 1.10 */

    /* CDC Call Management FD */
    5, 0x24, 0x01,         /* bLength, CS_INTERFACE, CALL_MGMT */
    0x01,                  /* bmCapabilities */
    1,                     /* bDataInterface */

    /* CDC ACM FD */
    4, 0x24, 0x02,         /* bLength, CS_INTERFACE, ACM */
    0x02,                  /* bmCapabilities */

    /* CDC Union FD */
    5, 0x24, 0x06,         /* bLength, CS_INTERFACE, UNION */
    0,                     /* bMasterInterface */
    1,                     /* bSlaveInterface */

    /* EP3 IN: Interrupt (CDC notification) */
    7, 5,                  /* bLength, bDescriptorType = ENDPOINT */
    0x83,                  /* bEndpointAddress: IN EP3 */
    0x03,                  /* bmAttributes: Interrupt */
    0x08, 0x00,            /* wMaxPacketSize = 8 */
    0x10,                  /* bInterval = 16ms */

    /* Interface 1: CDC Data */
    9, 4,
    1, 0,                  /* bInterfaceNumber, bAlternateSetting */
    2,                     /* bNumEndpoints */
    0x0A, 0x00, 0x00,      /* bInterfaceClass/SubClass/Protocol = CDC Data */
    0,                     /* iInterface */

    /* EP1 IN: Bulk (CDC data device→host) */
    7, 5,
    0x81,                  /* bEndpointAddress: IN EP1 */
    0x02,                  /* bmAttributes: Bulk */
    EP1_MAX_PACKET, 0x00,  /* wMaxPacketSize = 64 */
    0x00,                  /* bInterval */

    /* EP2 OUT: Bulk (CDC data host→device) */
    7, 5,
    0x02,                  /* bEndpointAddress: OUT EP2 */
    0x02,                  /* bmAttributes: Bulk */
    EP2_MAX_PACKET, 0x00,  /* wMaxPacketSize = 64 */
    0x00,                  /* bInterval */
};

/* String descriptors */
static const uint8_t usb_str_lang[] = {
    4, 3,                  /* bLength, STRING */
    0x09, 0x04,            /* wLANGID = 0x0409 (English US) */
};

static const uint8_t usb_str_manufacturer[] = {
    8, 3,                  /* bLength, STRING */
    'A', 0, 'P', 0, 'M', 0,
};

static const uint8_t usb_str_product[] = {
    28, 3,                 /* bLength = 2 + 2*13, STRING */
    'C', 0, 'U', 0, 'A', 0, 'V', 0, ' ', 0,
    'V', 0, '5', 0, ' ', 0,
    'C', 0, 'D', 0, 'C', 0, ' ', 0, '1', 0,
};

static const uint8_t usb_str_serial[] = {
    12, 3,                 /* bLength = 2 + 2*5, STRING */
    '0', 0, '0', 0, '0', 0, '0', 0, '1', 0,
};

/* ========================================================================== */
/* Global driver instance                                                     */
/* ========================================================================== */

RT_USBDriver rtt_usb;
static bool _usb_driver_inited = false;

static void _usb_set_address_after_status(void *usbp);

/* ========================================================================== */
/* Forward declarations of internal functions                                  */
/* ========================================================================== */

static void otg_core_reset(RT_USBDriver *usbp);
static void otg_disable_ep(RT_USBDriver *usbp);
static void otg_rxfifo_flush(RT_USBDriver *usbp);
static void otg_txfifo_flush(RT_USBDriver *usbp, uint32_t fifo);
static void otg_ram_reset(RT_USBDriver *usbp);
static uint32_t otg_ram_alloc(RT_USBDriver *usbp, size_t size);
static void otg_fifo_write_from_buffer(volatile uint32_t *fifop,
                                        const uint8_t *buf, size_t n);
static void otg_fifo_read_to_buffer(volatile uint32_t *fifop,
                                     uint8_t *buf, size_t n, size_t max);
static void otg_rxfifo_handler(RT_USBDriver *usbp);
static bool otg_txfifo_handler(RT_USBDriver *usbp, usbep_t ep);
static void otg_epin_handler(RT_USBDriver *usbp, usbep_t ep);
static void otg_epout_handler(RT_USBDriver *usbp, usbep_t ep);
static void otg_isoc_in_failed_handler(RT_USBDriver *usbp);
static void otg_isoc_out_failed_handler(RT_USBDriver *usbp);

/* ========================================================================== */
/* Internal helper functions (1:1 from ChibiOS hal_usb_lld.c)                 */
/* ========================================================================== */

static void otg_core_reset(RT_USBDriver *usbp)
{
    stm32_otg_t *otgp = (stm32_otg_t *)usbp->otg;

    /* Wait AHB idle. */
    while ((otgp->GRSTCTL & GRSTCTL_AHBIDL) == 0)
        ;

    /* Core reset and delay of at least 3 PHY cycles. */
    otgp->GRSTCTL = GRSTCTL_CSRST;
    osalSysPolledDelayX(12);
    while ((otgp->GRSTCTL & GRSTCTL_CSRST) != 0)
        ;

    osalSysPolledDelayX(18);

    /* Wait AHB idle again. */
    while ((otgp->GRSTCTL & GRSTCTL_AHBIDL) == 0)
        ;
}

static void otg_disable_ep(RT_USBDriver *usbp)
{
    stm32_otg_t *otgp = (stm32_otg_t *)usbp->otg;
    const stm32_otg_params_t *par = (const stm32_otg_params_t *)usbp->otgparams;
    unsigned i;

    for (i = 0; i <= par->num_endpoints; i++) {
        if ((otgp->ie[i].DIEPCTL & DIEPCTL_EPENA) != 0U)
            otgp->ie[i].DIEPCTL |= DIEPCTL_EPDIS;
        if ((otgp->oe[i].DOEPCTL & DIEPCTL_EPENA) != 0U)
            otgp->oe[i].DOEPCTL |= DIEPCTL_EPDIS;
        otgp->ie[i].DIEPINT = 0xFFFFFFFF;
        otgp->oe[i].DOEPINT = 0xFFFFFFFF;
    }
    otgp->DAINTMSK = DAINTMSK_OEPM(0) | DAINTMSK_IEPM(0);
}

static void otg_rxfifo_flush(RT_USBDriver *usbp)
{
    stm32_otg_t *otgp = (stm32_otg_t *)usbp->otg;

    otgp->GRSTCTL = GRSTCTL_RXFFLSH;
    while ((otgp->GRSTCTL & GRSTCTL_RXFFLSH) != 0)
        ;
    osalSysPolledDelayX(18);
}

static void otg_txfifo_flush(RT_USBDriver *usbp, uint32_t fifo)
{
    stm32_otg_t *otgp = (stm32_otg_t *)usbp->otg;

    otgp->GRSTCTL = GRSTCTL_TXFNUM(fifo) | GRSTCTL_TXFFLSH;
    while ((otgp->GRSTCTL & GRSTCTL_TXFFLSH) != 0)
        ;
    osalSysPolledDelayX(18);
}

static void otg_ram_reset(RT_USBDriver *usbp)
{
    usbp->pmnext = ((const stm32_otg_params_t *)usbp->otgparams)->rx_fifo_size;
}

static uint32_t otg_ram_alloc(RT_USBDriver *usbp, size_t size)
{
    uint32_t next;
    next = usbp->pmnext;
    usbp->pmnext += size;
    RT_ASSERT(usbp->pmnext <= ((const stm32_otg_params_t *)usbp->otgparams)->otg_ram_size);
    return next;
}

static void otg_fifo_write_from_buffer(volatile uint32_t *fifop,
                                        const uint8_t *buf, size_t n)
{
    RT_ASSERT(n > 0);

    while (true) {
        *fifop = *((const uint32_t *)buf);
        if (n <= 4)
            break;
        n -= 4;
        buf += 4;
    }
}

static void otg_fifo_read_to_buffer(volatile uint32_t *fifop,
                                     uint8_t *buf, size_t n, size_t max)
{
    uint32_t w = 0;
    size_t i = 0;

    while (i < n) {
        if ((i & 3) == 0)
            w = *fifop;
        if (i < max) {
            *buf++ = (uint8_t)w;
            w >>= 8;
        }
        i++;
    }
}

/* ========================================================================== */
/* TX FIFO handler (ChibiOS 1:1)                                              */
/* ========================================================================== */

static bool otg_txfifo_handler(RT_USBDriver *usbp, usbep_t ep)
{
    stm32_otg_t *otgp = (stm32_otg_t *)usbp->otg;
    USBInEndpointState *isp = (USBInEndpointState *)usbp->epc[ep]->in_state;

    while (true) {
        uint32_t n;

        if (isp->txcnt >= isp->txsize) {
            otgp->DIEPEMPMSK &= ~DIEPEMPMSK_INEPTXFEM(ep);
            return true;
        }

        n = isp->txsize - isp->txcnt;
        if (n > usbp->epc[ep]->in_maxsize)
            n = usbp->epc[ep]->in_maxsize;

        if (((otgp->ie[ep].DTXFSTS & DTXFSTS_INEPTFSAV_MASK) * 4) < n)
            return false;

#if 0 /* STM32_USB_OTGFIFO_FILL_BASEPRI — disabled by default (value 0) */
        __set_BASEPRI(STM32_USB_OTGFIFO_FILL_BASEPRI);
#endif
        otg_fifo_write_from_buffer(otgp->FIFO[ep], isp->txbuf, n);
        isp->txbuf += n;
        isp->txcnt += n;
#if 0
        __set_BASEPRI(0);
#endif
    }
}

/* ========================================================================== */
/* RX FIFO handler (ChibiOS 1:1)                                              */
/* ========================================================================== */

static void otg_rxfifo_handler(RT_USBDriver *usbp)
{
    stm32_otg_t *otgp = (stm32_otg_t *)usbp->otg;
    uint32_t sts, cnt, ep;

    sts = otgp->GRXSTSP;

    cnt = (sts & GRXSTSP_BCNT_MASK) >> GRXSTSP_BCNT_OFF;
    ep  = (sts & GRXSTSP_EPNUM_MASK) >> GRXSTSP_EPNUM_OFF;

    switch (sts & GRXSTSP_PKTSTS_MASK) {
    case GRXSTSP_SETUP_DATA:
        otg_fifo_read_to_buffer(otgp->FIFO[0],
                                usbp->epc[ep]->setup_buf, cnt, 8);
        break;

    case GRXSTSP_SETUP_COMP:
        break;

    case GRXSTSP_OUT_DATA: {
        USBOutEndpointState *osp = (USBOutEndpointState *)usbp->epc[ep]->out_state;
        if (osp != NULL && osp->rxbuf != NULL) {
            otg_fifo_read_to_buffer(otgp->FIFO[0], osp->rxbuf, cnt,
                                    osp->rxsize - osp->rxcnt);
            osp->rxbuf += cnt;
            osp->rxcnt += cnt;
        } else {
            /* No buffer — discard */
            otg_fifo_read_to_buffer(otgp->FIFO[0], NULL, cnt, 0);
        }
        break;
    }

    case GRXSTSP_OUT_COMP:
    case GRXSTSP_OUT_GLOBAL_NAK:
    default:
        break;
    }
}

/* ========================================================================== */
/* EP IN handler (ChibiOS 1:1)                                                */
/* ========================================================================== */

static void otg_epin_handler(RT_USBDriver *usbp, usbep_t ep)
{
    stm32_otg_t *otgp = (stm32_otg_t *)usbp->otg;
    uint32_t epint = otgp->ie[ep].DIEPINT;

    otgp->ie[ep].DIEPINT = epint;

    if (epint & DIEPINT_TOC) {
        /* Timeouts not handled. */
    }

    if ((epint & DIEPINT_XFRC) && (otgp->DIEPMSK & DIEPMSK_XFRCM)) {
        USBInEndpointState *isp = (USBInEndpointState *)usbp->epc[ep]->in_state;

        if (isp->txsize < isp->totsize) {
            if (ep == 0) {
                rtt_dbg_usb_ep0_cont++;
            }
            isp->txsize = isp->totsize - isp->txsize;
            isp->txcnt  = 0;
            __disable_irq();
            usb_lld_start_in(usbp, ep);
            __enable_irq();
        } else {
            /* Transfer complete — invoke IN callback. */
            if (usbp->epc[ep]->in_cb)
                usbp->epc[ep]->in_cb(usbp, ep);
        }
    }

    if ((epint & DIEPINT_TXFE) &&
        (otgp->DIEPEMPMSK & DIEPEMPMSK_INEPTXFEM(ep))) {
        otg_txfifo_handler(usbp, ep);
    }
}

/* ========================================================================== */
/* EP OUT handler (ChibiOS 1:1)                                               */
/* ========================================================================== */

static void otg_epout_handler(RT_USBDriver *usbp, usbep_t ep)
{
    stm32_otg_t *otgp = (stm32_otg_t *)usbp->otg;
    uint32_t epint = otgp->oe[ep].DOEPINT;

    otgp->oe[ep].DOEPINT = epint;

    if ((epint & DOEPINT_STUP) && (otgp->DOEPMSK & DOEPMSK_STUPM)) {
        if (usbp->epc[ep]->setup_cb)
            usbp->epc[ep]->setup_cb(usbp, ep);
    }

    if ((epint & DOEPINT_XFRC) && (otgp->DOEPMSK & DOEPMSK_XFRCM)) {
        USBOutEndpointState *osp = (USBOutEndpointState *)usbp->epc[ep]->out_state;

        if (ep == 0) {
            if (((osp->rxcnt % usbp->epc[ep]->out_maxsize) == 0) &&
                (osp->rxsize < osp->totsize)) {
                osp->rxsize = osp->totsize - osp->rxsize;
                osp->rxcnt  = 0;
                __disable_irq();
                usb_lld_start_out(usbp, ep);
                __enable_irq();
                return;
            }
        }

        if (usbp->epc[ep]->out_cb)
            usbp->epc[ep]->out_cb(usbp, ep);
    }
}

/* ========================================================================== */
/* Isochronous IN/OUT failed handlers (ChibiOS 1:1)                           */
/* ========================================================================== */

static void otg_isoc_in_failed_handler(RT_USBDriver *usbp)
{
    stm32_otg_t *otgp = (stm32_otg_t *)usbp->otg;
    const stm32_otg_params_t *par = (const stm32_otg_params_t *)usbp->otgparams;
    usbep_t ep;

    for (ep = 0; ep <= par->num_endpoints; ep++) {
        if (((otgp->ie[ep].DIEPCTL & DIEPCTL_EPTYP_MASK) == DIEPCTL_EPTYP_ISO) &&
            ((otgp->ie[ep].DIEPCTL & DIEPCTL_EPENA) != 0)) {
            otgp->ie[ep].DIEPCTL |= (DIEPCTL_EPDIS | DIEPCTL_SNAK);
            while (otgp->ie[ep].DIEPCTL & DIEPCTL_EPENA)
                ;
            otg_txfifo_flush(usbp, ep);
            if (usbp->epc[ep]->in_cb)
                usbp->epc[ep]->in_cb(usbp, ep);
        }
    }
}

static void otg_isoc_out_failed_handler(RT_USBDriver *usbp)
{
    stm32_otg_t *otgp = (stm32_otg_t *)usbp->otg;
    const stm32_otg_params_t *par = (const stm32_otg_params_t *)usbp->otgparams;
    usbep_t ep;

    for (ep = 0; ep <= par->num_endpoints; ep++) {
        if (((otgp->oe[ep].DOEPCTL & DOEPCTL_EPTYP_MASK) == DOEPCTL_EPTYP_ISO) &&
            ((otgp->oe[ep].DOEPCTL & DOEPCTL_EPENA) != 0)) {
            if (usbp->epc[ep]->out_cb)
                usbp->epc[ep]->out_cb(usbp, ep);
        }
    }
}

/* ========================================================================== */
/* usb_lld_serve_interrupt — OTG shared ISR (ChibiOS 1:1)                     */
/* ========================================================================== */

void usb_lld_serve_interrupt(void *usbp)
{
    RT_USBDriver *drv = (RT_USBDriver *)usbp;
    stm32_otg_t *otgp = (stm32_otg_t *)drv->otg;
    uint32_t sts, src;

    sts  = otgp->GINTSTS;
    sts &= otgp->GINTMSK;
    otgp->GINTSTS = sts;

    /* Reset interrupt handling. */
    if (sts & GINTSTS_USBRST) {
        _usb_reset(drv);
        return;
    }

    /* Wake-up handling. */
    if (sts & GINTSTS_WKUPINT) {
        if (otgp->PCGCCTL & (PCGCCTL_STPPCLK | PCGCCTL_GATEHCLK))
            otgp->PCGCCTL &= ~(PCGCCTL_STPPCLK | PCGCCTL_GATEHCLK);
        otgp->DCTL &= ~DCTL_RWUSIG;
        _usb_wakeup(drv);
    }

    /* Suspend handling. */
    if (sts & GINTSTS_USBSUSP) {
        otg_disable_ep(drv);
        _usb_suspend(drv);
    }

    /* Enumeration done. */
    if (sts & GINTSTS_ENUMDNE) {
        if ((otgp->DSTS & DSTS_ENUMSPD_MASK) == DSTS_ENUMSPD_HS_480)
            otgp->GUSBCFG = (otgp->GUSBCFG & ~GUSBCFG_TRDT_MASK) |
                            GUSBCFG_TRDT(TRDT_VALUE_HS);
        else
            otgp->GUSBCFG = (otgp->GUSBCFG & ~GUSBCFG_TRDT_MASK) |
                            GUSBCFG_TRDT(TRDT_VALUE_FS);
    }

    /* SOF interrupt handling. */
    if (sts & GINTSTS_SOF) {
        const RTT_USBConfig *cfg = (const RTT_USBConfig *)drv->config;
        if (cfg == NULL || cfg->sof_cb == NULL)
            otgp->GINTMSK &= ~GINTMSK_SOFM;

        if (drv->state == USB_STATE_SUSPENDED) {
            if (otgp->PCGCCTL & (PCGCCTL_STPPCLK | PCGCCTL_GATEHCLK))
                otgp->PCGCCTL &= ~(PCGCCTL_STPPCLK | PCGCCTL_GATEHCLK);
            _usb_wakeup(drv);
        }

        if (cfg && cfg->sof_cb)
            cfg->sof_cb(drv);
    }

    /* Isochronous IN failed. */
    if (sts & GINTSTS_IISOIXFR)
        otg_isoc_in_failed_handler(drv);

    /* Isochronous OUT failed. */
    if (sts & GINTSTS_IISOOXFR)
        otg_isoc_out_failed_handler(drv);

    /* RX FIFO emptying — must happen before endpoint event handling. */
    if ((sts & GINTSTS_RXFLVL) != 0U)
        otg_rxfifo_handler(drv);

    /* IN/OUT endpoint event handling. */
    src = otgp->DAINT;

    if (sts & GINTSTS_OEPINT) {
        if (src & (1U << 16)) otg_epout_handler(drv, 0);
        if (src & (1U << 17)) otg_epout_handler(drv, 1);
        if (src & (1U << 18)) otg_epout_handler(drv, 2);
        if (src & (1U << 19)) otg_epout_handler(drv, 3);
    }

    if (sts & GINTSTS_IEPINT) {
        if (src & (1U << 0))  otg_epin_handler(drv, 0);
        if (src & (1U << 1))  otg_epin_handler(drv, 1);
        if (src & (1U << 2))  otg_epin_handler(drv, 2);
        if (src & (1U << 3))  otg_epin_handler(drv, 3);
    }
}

/* ========================================================================== */
/* HAL state machine functions (from ChibiOS hal_usb.c)                       */
/* ========================================================================== */

static void _usb_reset(void *usbp)
{
    RT_USBDriver *drv = (RT_USBDriver *)usbp;

    usb_lld_reset(drv);

    /* Clear EP0 state. */
    drv->ep0state  = USB_EP0_STATE_IDLE;
    drv->ep0data   = NULL;
    drv->ep0len    = 0;
    drv->ep0max    = EP0_MAX_PACKET;
    drv->ep0endcb  = NULL;

    /* Clear endpoint bitmaps. */
    drv->transmitting = 0;
    drv->receiving    = 0;

    /* Reset configuration. */
    drv->configuration = 0;
    drv->address       = 0;

    /* State transition. */
    drv->state = USB_STATE_SELECTED;

    /* Event notification. */
    if (drv->config) {
        const RTT_USBConfig *cfg = (const RTT_USBConfig *)drv->config;
        if (cfg->event_cb)
            cfg->event_cb(drv, USB_EVENT_RESET);
    }
}

static void _usb_suspend(void *usbp)
{
    RT_USBDriver *drv = (RT_USBDriver *)usbp;

    drv->state = USB_STATE_SUSPENDED;

    if (drv->config) {
        const RTT_USBConfig *cfg = (const RTT_USBConfig *)drv->config;
        if (cfg->event_cb)
            cfg->event_cb(drv, USB_EVENT_SUSPEND);
    }
}

static void _usb_wakeup(void *usbp)
{
    RT_USBDriver *drv = (RT_USBDriver *)usbp;

    drv->state = USB_STATE_ACTIVE;

    if (drv->config) {
        const RTT_USBConfig *cfg = (const RTT_USBConfig *)drv->config;
        if (cfg->event_cb)
            cfg->event_cb(drv, USB_EVENT_WAKEUP);
    }
}

static void _usb_set_address_after_status(void *usbp)
{
    RT_USBDriver *drv = (RT_USBDriver *)usbp;

    rtt_dbg_usb_set_addr++;
    usb_lld_set_address(drv);
    drv->state = USB_STATE_SELECTED;

    if (drv->config) {
        const RTT_USBConfig *cfg = (const RTT_USBConfig *)drv->config;
        if (cfg->event_cb)
            cfg->event_cb(drv, USB_EVENT_ADDRESS);
    }
}

/* ========================================================================== */
/* Standard request handler (ChibiOS default_handler)                         */
/* ========================================================================== */

static bool default_handler(void *usbp)
{
    RT_USBDriver *drv = (RT_USBDriver *)usbp;
    uint8_t *setup = drv->setup;

    uint8_t  bmReqType = setup[0];
    uint8_t  bRequest  = setup[1];
    uint16_t wValue    = setup[2] | ((uint16_t)setup[3] << 8);
    uint16_t wIndex    = setup[4] | ((uint16_t)setup[5] << 8);
    uint16_t wLength   = setup[6] | ((uint16_t)setup[7] << 8);
    uint8_t  recipient = bmReqType & USB_RECIPIENT_MASK;

    /* Only handle standard requests. */
    if ((bmReqType & USB_TYPE_MASK) != USB_TYPE_STANDARD)
        return false;

    switch (recipient) {

    case USB_RECIPIENT_DEVICE:
        switch (bRequest) {

        case USB_REQ_GET_STATUS: {
            static const uint8_t status[2] = {0, 0};
            usb_setup_transfer(drv, status, 2, NULL);
            return true;
        }

        case USB_REQ_CLEAR_FEATURE:
            if (wValue == USB_FEATURE_REMOTE_WAKEUP)
                drv->status &= ~(1U << 1);
            usb_setup_transfer(drv, NULL, 0, NULL);
            return true;

        case USB_REQ_SET_FEATURE:
            if (wValue == USB_FEATURE_REMOTE_WAKEUP)
                drv->status |= (1U << 1);
            usb_setup_transfer(drv, NULL, 0, NULL);
            return true;

        case USB_REQ_SET_ADDRESS:
            drv->address = (uint8_t)(wValue & 0x7F);
#if defined(USB_SET_ADDRESS_MODE) && USB_SET_ADDRESS_MODE
            /* Early set address — apply immediately. */
            usb_lld_set_address(drv);
            usb_setup_transfer(drv, NULL, 0, NULL);
#else
            /* Apply address after EP0 status IN (ChibiOS default). */
            usb_setup_transfer(drv, NULL, 0, _usb_set_address_after_status);
#endif
            return true;

        case USB_REQ_GET_DESCRIPTOR: {
            uint8_t dtype  = (uint8_t)(wValue >> 8);
            uint8_t dindex = (uint8_t)(wValue & 0xFF);
            const uint8_t *desc = NULL;
            size_t dlen = 0;
            const RTT_USBConfig *dcfg = (const RTT_USBConfig *)drv->config;

            if (dcfg && dcfg->get_descriptor_cb)
                desc = dcfg->get_descriptor_cb(drv, dtype, dindex, wIndex);

            if (desc == NULL) {
                switch (dtype) {
                case USB_DTYPE_DEVICE:
                    desc = usb_dev_desc;
                    dlen = usb_dev_desc[0];
                    break;
                case USB_DTYPE_CONFIGURATION:
                    desc = usb_cfg_desc;
                    dlen = (size_t)usb_cfg_desc[2] | ((size_t)usb_cfg_desc[3] << 8);
                    break;
                case USB_DTYPE_STRING:
                    switch (dindex) {
                    case 0:  desc = usb_str_lang;          dlen = usb_str_lang[0];          break;
                    case 1:  desc = usb_str_manufacturer;  dlen = usb_str_manufacturer[0];  break;
                    case 2:  desc = usb_str_product;       dlen = usb_str_product[0];       break;
                    case 3:  desc = usb_str_serial;        dlen = usb_str_serial[0];        break;
                    default:
                        break;
                    }
                    break;
                case USB_DTYPE_DEVICE_QUALIFIER:
                case USB_DTYPE_OTHER_SPEED:
                default:
                    break;
                }
            } else {
                dlen = desc[0];
                if (dtype == USB_DTYPE_CONFIGURATION)
                    dlen = (size_t)desc[2] | ((size_t)desc[3] << 8);
            }

            if (desc == NULL) {
                usb_lld_stall_in(drv, 0);
                usb_lld_stall_out(drv, 0);
                return true;
            }

            if (wLength < dlen)
                dlen = wLength;
            usb_setup_transfer(drv, desc, dlen, NULL);
            return true;
        }

        case USB_REQ_GET_CONFIGURATION: {
            uint8_t cfg_val = drv->configuration;
            usb_setup_transfer(drv, &cfg_val, 1, NULL);
            return true;
        }

        case USB_REQ_SET_CONFIGURATION: {
            uint8_t cfg = (uint8_t)(wValue & 0xFF);
            const RTT_USBConfig *dcfg = (const RTT_USBConfig *)drv->config;

            /* ChibiOS: tear down active config before selecting a new one. */
            if (drv->state == USB_STATE_ACTIVE) {
                usb_lld_disable_endpoints(drv);
                drv->configuration = 0;
                drv->state = USB_STATE_SELECTED;
                if (dcfg && dcfg->event_cb)
                    dcfg->event_cb(drv, USB_EVENT_RESET);
            }

            drv->configuration = cfg;
            if (cfg != 0) {
                drv->state = USB_STATE_ACTIVE;
                if (dcfg && dcfg->event_cb)
                    dcfg->event_cb(drv, USB_EVENT_CONFIGURED);
            } else {
                drv->state = USB_STATE_SELECTED;
            }

            usb_setup_transfer(drv, NULL, 0, NULL);
            return true;
        }

        case USB_REQ_GET_INTERFACE: {
            uint8_t alt = 0;
            usb_setup_transfer(drv, &alt, 1, NULL);
            return true;
        }

        case USB_REQ_SET_INTERFACE:
            usb_setup_transfer(drv, NULL, 0, NULL);
            return true;

        case USB_REQ_SET_DESCRIPTOR:
        default:
            usb_lld_stall_in(drv, 0);
            usb_lld_stall_out(drv, 0);
            return true;
        }

    case USB_RECIPIENT_INTERFACE:
        /* Interface requests: CDC class requests handled in EP0 setup. */
        return false;

    case USB_RECIPIENT_ENDPOINT:
        switch (bRequest) {

        case USB_REQ_GET_STATUS: {
            uint8_t ep_st;
            if (wIndex & 0x80)
                ep_st = usb_lld_get_status_in(drv, (usbep_t)(wIndex & 0x7F));
            else
                ep_st = usb_lld_get_status_out(drv, (usbep_t)(wIndex & 0x7F));
            {
                uint8_t eps[2] = {(ep_st == EP_STATUS_STALLED) ? 1U : 0U, 0};
                usb_setup_transfer(drv, eps, 2, NULL);
            }
            return true;
        }

        case USB_REQ_CLEAR_FEATURE:
            if (wValue == USB_FEATURE_ENDPOINT_HALT) {
                if (wIndex & 0x80)
                    usb_lld_clear_in(drv, (usbep_t)(wIndex & 0x7F));
                else
                    usb_lld_clear_out(drv, (usbep_t)(wIndex & 0x7F));
            }
            usb_setup_transfer(drv, NULL, 0, NULL);
            return true;

        case USB_REQ_SET_FEATURE:
            if (wValue == USB_FEATURE_ENDPOINT_HALT) {
                if (wIndex & 0x80)
                    usb_lld_stall_in(drv, (usbep_t)(wIndex & 0x7F));
                else
                    usb_lld_stall_out(drv, (usbep_t)(wIndex & 0x7F));
            }
            usb_setup_transfer(drv, NULL, 0, NULL);
            return true;

        case USB_REQ_SYNCH_FRAME: {
            uint8_t sf[2] = {0, 0};
            usb_setup_transfer(drv, sf, 2, NULL);
            return true;
        }

        default:
            usb_lld_stall_in(drv, 0);
            usb_lld_stall_out(drv, 0);
            return true;
        }

    default:
        return false;
    }
}

/* ========================================================================== */
/* EP0 SETUP callback                                                         */
/* ========================================================================== */

static void _usb_ep0setup(void *usbp, usbep_t ep)
{
    RT_USBDriver *drv = (RT_USBDriver *)usbp;

    (void)ep;

    /* Read setup packet into driver buffer. */
    usb_lld_read_setup(drv, 0, drv->setup);

    /* Clear EP0 state. */
    drv->ep0state  = USB_EP0_STATE_IDLE;
    drv->ep0data   = NULL;
    drv->ep0len    = 0;
    drv->ep0max    = ((RT_USBEndpointConfig *)drv->epc[0])->in_maxsize;
    drv->ep0endcb  = NULL;

    /* Clear stall condition if previously stalled (USB 2.0 8.4.2). */
    {
        stm32_otg_t *otgp = (stm32_otg_t *)drv->otg;
        otgp->ie[0].DIEPCTL &= ~DIEPCTL_STALL;
        otgp->oe[0].DOEPCTL &= ~DIEPCTL_STALL;
    }

    /* Try standard request handler first. */
    if (default_handler(drv))
        return;

    /* Class/vendor hooks (CDC layer via requests_hook_cb). */
    if (drv->config) {
        const RTT_USBConfig *cfg = (const RTT_USBConfig *)drv->config;
        uint8_t bmReqType = drv->setup[0];

        if ((bmReqType & USB_TYPE_MASK) == USB_TYPE_CLASS && cfg->requests_hook_cb) {
            cfg->requests_hook_cb(drv);
            return;
        }
    }

    /* Not handled — stall. */
    usb_lld_stall_in(drv, 0);
    usb_lld_stall_out(drv, 0);
}

/* ========================================================================== */
/* EP0 IN callback — called when IN data stage completes                       */
/* ========================================================================== */

static void _usb_ep0in(void *usbp, usbep_t ep)
{
    RT_USBDriver *drv = (RT_USBDriver *)usbp;
    (void)ep;

    switch (drv->ep0state) {

    case USB_EP0_STATE_WAITING_DATA_IN: {
        /* Data stage complete (possibly multi-packet via otg_epin_handler). */
        const uint16_t wLength = (uint16_t)drv->setup[6] |
                                 ((uint16_t)drv->setup[7] << 8);
        const size_t sent = drv->ep0len;

        /* ChibiOS: ZLP required when sent < wLength and sent is MPS-aligned. */
        if (sent > 0 && sent < wLength && (sent % drv->ep0max) == 0) {
            rtt_dbg_usb_ep0_zlp++;
            drv->ep0state = USB_EP0_STATE_WAITING_IN_ZLP;
            {
                USBInEndpointState *isp =
                    (USBInEndpointState *)drv->epc[0]->in_state;
                isp->txbuf  = NULL;
                isp->txsize = 0;
                isp->txcnt  = 0;
            }
            usb_lld_start_in(drv, 0);
            break;
        }

        drv->ep0data = NULL;
        drv->ep0len  = 0;
        drv->ep0state = USB_EP0_STATE_WAITING_STATUS_OUT;
        {
            USBOutEndpointState *osp =
                (USBOutEndpointState *)drv->epc[0]->out_state;
            osp->rxbuf   = NULL;
            osp->rxsize  = 0;
            osp->rxcnt   = 0;
            osp->totsize = 0;
        }
        usb_lld_start_out(drv, 0);
        break;
    }

    case USB_EP0_STATE_WAITING_IN_ZLP:
        drv->ep0data = NULL;
        drv->ep0len  = 0;
        drv->ep0state = USB_EP0_STATE_WAITING_STATUS_OUT;
        {
            USBOutEndpointState *osp =
                (USBOutEndpointState *)drv->epc[0]->out_state;
            osp->rxbuf   = NULL;
            osp->rxsize  = 0;
            osp->rxcnt   = 0;
            osp->totsize = 0;
        }
        usb_lld_start_out(drv, 0);
        break;

    case USB_EP0_STATE_WAITING_STATUS_IN:
        /* Status stage complete — run deferred SET_ADDRESS etc. */
        if (drv->ep0endcb != NULL)
            drv->ep0endcb(drv);
        drv->ep0endcb  = NULL;
        drv->ep0state = USB_EP0_STATE_IDLE;
        break;

    default:
        break;
    }
}

/* ========================================================================== */
/* EP0 OUT callback — called when OUT data stage completes                     */
/* ========================================================================== */

static void _usb_ep0out(void *usbp, usbep_t ep)
{
    RT_USBDriver *drv = (RT_USBDriver *)usbp;
    (void)ep;

    switch (drv->ep0state) {

    case USB_EP0_STATE_WAITING_DATA_OUT: {
        /* Data arrived from host (e.g. SET_LINE_CODING data). */
        if (drv->ep0endcb != NULL) {
            void (*cb)(void *) = drv->ep0endcb;
            drv->ep0endcb = NULL;
            cb(drv);
        }

        /* Send ZLP status IN. */
        drv->ep0state = USB_EP0_STATE_WAITING_STATUS_IN;
        {
            USBInEndpointState *isp = (USBInEndpointState *)drv->epc[0]->in_state;
            isp->txbuf  = NULL;
            isp->txsize = 0;
            isp->txcnt  = 0;
            isp->totsize = 0;
        }
        usb_lld_start_in(drv, 0);
        break;
    }

    case USB_EP0_STATE_WAITING_STATUS_OUT: {
        stm32_otg_t *otgp = (stm32_otg_t *)drv->otg;
        /* Status OUT from host after a control read. */
        if (drv->ep0endcb != NULL)
            drv->ep0endcb(drv);
        drv->ep0endcb  = NULL;
        drv->ep0state = USB_EP0_STATE_IDLE;
        /* Re-arm EP0 for next SETUP (STUPCNT only). */
        otgp->oe[0].DOEPTSIZ = DOEPTSIZ_STUPCNT(3);
        otgp->oe[0].DOEPCTL |= DOEPCTL_EPENA | DOEPCTL_CNAK;
        break;
    }

    default:
        break;
    }
}

/* ========================================================================== */
/* ========================================================================== */
/* 14 public LLD functions (ChibiOS API)                                      */
/* ========================================================================== */
/* ========================================================================== */

/* -------------------------------------------------------------------------- */
/* usb_lld_init — initialize driver object and OTG params                     */
/* -------------------------------------------------------------------------- */

void usb_lld_init(void *usbp)
{
    RT_USBDriver *drv = (RT_USBDriver *)usbp;

    drv->otg       = (void *)OTG_FS_BASE;
    drv->otgparams = (void *)&fs_params;
    drv->state     = USB_STATE_UNINIT;

    /* usbObjectInit — clear state fields. */
    drv->config         = NULL;
    drv->transmitting   = 0;
    drv->receiving      = 0;
    drv->address        = 0;
    drv->configuration  = 0;
    drv->pmnext         = 0;
    drv->ep0state       = USB_EP0_STATE_IDLE;
    drv->ep0data        = NULL;
    drv->ep0len         = 0;
    drv->ep0max         = EP0_MAX_PACKET;
    drv->ep0endcb       = NULL;
    drv->event_cb       = NULL;
    memset(drv->epc, 0, sizeof(drv->epc));
    memset(drv->setup, 0, sizeof(drv->setup));

    drv->state = USB_STATE_STOP;
}

/* -------------------------------------------------------------------------- */
/* usb_lld_start — configures and activates the USB peripheral                 */
/* -------------------------------------------------------------------------- */

void usb_lld_start(void *usbp)
{
    RT_USBDriver *drv = (RT_USBDriver *)usbp;
    stm32_otg_t *otgp = (stm32_otg_t *)drv->otg;
    const RTT_USBConfig *cfg = (const RTT_USBConfig *)drv->config;

    if (drv->state == USB_STATE_STOP) {
        /* Enable OTG_FS clock and reset. */
        RCC->AHB2ENR |= RCC_AHB2ENR_OTGFSEN;
        __DSB();
        RCC->AHB2RSTR |= RCC_AHB2RSTR_OTGFSRST;
        __DSB();
        RCC->AHB2RSTR &= ~RCC_AHB2RSTR_OTGFSRST;
        __DSB();

        /* Enable NVIC. */
        NVIC_SetPriority(OTG_FS_IRQn, NVIC_EncodePriority(
            NVIC_GetPriorityGrouping(), 5, 0));
        NVIC_EnableIRQ(OTG_FS_IRQn);

        /* Force device mode, FS 1.1 PHY, turn-around time. */
        otgp->GUSBCFG = GUSBCFG_FDMOD | GUSBCFG_TRDT(TRDT_VALUE_FS) |
                        GUSBCFG_PHYSEL;

        /* 48MHz 1.1 PHY. */
        otgp->DCFG = 0x02200000 | DCFG_DSPD_FS11;

        /* PHY enabled. */
        otgp->PCGCCTL = 0;

        /* VBUS sensing. */
        otgp->GOTGCTL = GOTGCTL_BVALOEN | GOTGCTL_BVALOVAL;

        /* GCCFG: stepping 2 with VBUS sensing. */
        otgp->GCCFG = GCCFG_INIT_VALUE;

        /* Core reset. */
        otg_core_reset(drv);

        /* Re-program GUSBCFG after core reset (CSRST resets it). */
        otgp->GUSBCFG = GUSBCFG_FDMOD | GUSBCFG_TRDT(TRDT_VALUE_FS) |
                        GUSBCFG_PHYSEL;

        /* Interrupts on TXFIFOs half empty. */
        otgp->GAHBCFG = 0;

        /* Endpoints re-initialization. */
        otg_disable_ep(drv);

        /* Clear all pending Device Interrupts. */
        otgp->DIEPMSK  = 0;
        otgp->DOEPMSK  = 0;
        otgp->DAINTMSK = 0;

        /* Initial interrupt mask (no RX/EP masks yet — added in usb_lld_reset). */
        otgp->GINTMSK  = GINTMSK_ENUMDNEM | GINTMSK_USBRSTM |
                         GINTMSK_USBSUSPM | GINTMSK_ESUSPM |
                         GINTMSK_SRQM     | GINTMSK_WKUM |
                         GINTMSK_IISOIXFRM | GINTMSK_IISOOXFRM;

        if (cfg && cfg->sof_cb != NULL)
            otgp->GINTMSK |= GINTMSK_SOFM;

        /* Clear all pending IRQs. */
        otgp->GINTSTS = 0xFFFFFFFF;

        /* Enable global interrupts. */
        otgp->GAHBCFG |= GAHBCFG_GINTMSK;
    }
}

/* -------------------------------------------------------------------------- */
/* usb_lld_stop — deactivates the USB peripheral                              */
/* -------------------------------------------------------------------------- */

void usb_lld_stop(void *usbp)
{
    RT_USBDriver *drv = (RT_USBDriver *)usbp;
    stm32_otg_t *otgp = (stm32_otg_t *)drv->otg;

    if (drv->state != USB_STATE_STOP) {
        otg_disable_ep(drv);

        otgp->DAINTMSK = 0;
        otgp->GAHBCFG  = 0;
        otgp->GCCFG    = 0;

        NVIC_DisableIRQ(OTG_FS_IRQn);
        RCC->AHB2ENR &= ~RCC_AHB2ENR_OTGFSEN;

        drv->state = USB_STATE_STOP;
    }
}

/* -------------------------------------------------------------------------- */
/* usb_lld_reset — USB bus reset                                              */
/* -------------------------------------------------------------------------- */

void usb_lld_reset(void *usbp)
{
    RT_USBDriver *drv = (RT_USBDriver *)usbp;
    stm32_otg_t *otgp = (stm32_otg_t *)drv->otg;
    unsigned i;

    /* Flush TX FIFO 0. */
    otg_txfifo_flush(drv, 0);

    /* Endpoint interrupts all disabled and cleared. */
    otgp->DIEPEMPMSK = 0;
    otgp->DAINTMSK   = DAINTMSK_OEPM(0) | DAINTMSK_IEPM(0);

    /* All endpoints in NAK mode, interrupts cleared. */
    for (i = 0; i <= ((const stm32_otg_params_t *)drv->otgparams)->num_endpoints; i++) {
        otgp->ie[i].DIEPCTL = DIEPCTL_SNAK;
        otgp->oe[i].DOEPCTL = DIEPCTL_SNAK;
        otgp->ie[i].DIEPINT = 0xFFFFFFFF;
        otgp->oe[i].DOEPINT = 0xFFFFFFFF;
    }

    /* Reset FIFO allocator. */
    otg_ram_reset(drv);

    /* RX FIFO size initialization. */
    otgp->GRXFSIZ = GRXFSIZ_RXFD(((const stm32_otg_params_t *)drv->otgparams)->rx_fifo_size);
    otg_rxfifo_flush(drv);

    /* Reset device address to zero. */
    otgp->DCFG = (otgp->DCFG & ~DCFG_DAD_MASK) | DCFG_DAD(0);

    /* Enable RX/EP interrupt sources (ChibiOS: added here, not in start). */
    otgp->GINTMSK  |= GINTMSK_RXFLVLM | GINTMSK_OEPM | GINTMSK_IEPM;
    otgp->DIEPMSK   = DIEPMSK_TOCM | DIEPMSK_XFRCM;
    otgp->DOEPMSK   = DOEPMSK_STUPM | DOEPMSK_XFRCM;

    /* EP0 initialization (special case). */
    ep0config_init();
    drv->epc[0] = (RT_USBEndpointConfig *)&ep0config;

    otgp->oe[0].DOEPTSIZ = DOEPTSIZ_STUPCNT(3);
    otgp->oe[0].DOEPCTL  = DOEPCTL_SD0PID | DOEPCTL_USBAEP |
                           DOEPCTL_EPTYP_CTRL |
                           DOEPCTL_MPSIZ(ep0config.out_maxsize);

    otgp->ie[0].DIEPTSIZ = 0;
    otgp->ie[0].DIEPCTL  = DIEPCTL_SD0PID | DIEPCTL_USBAEP |
                           DIEPCTL_EPTYP_CTRL |
                           DIEPCTL_TXFNUM(0) |
                           DIEPCTL_MPSIZ(ep0config.in_maxsize);

    otgp->DIEPTXF0 = DIEPTXF_INEPTXFD(ep0config.in_maxsize / 4) |
                     DIEPTXF_INEPTXSA(otg_ram_alloc(drv,
                                                    ep0config.in_maxsize / 4));
}

/* -------------------------------------------------------------------------- */
/* usb_lld_set_address — sets the USB device address                           */
/* -------------------------------------------------------------------------- */

void usb_lld_set_address(void *usbp)
{
    RT_USBDriver *drv = (RT_USBDriver *)usbp;
    stm32_otg_t *otgp = (stm32_otg_t *)drv->otg;

    otgp->DCFG = (otgp->DCFG & ~DCFG_DAD_MASK) | DCFG_DAD(drv->address);
}

/* -------------------------------------------------------------------------- */
/* usb_lld_init_endpoint — enables an endpoint                                 */
/* -------------------------------------------------------------------------- */

void usb_lld_init_endpoint(void *usbp, usbep_t ep)
{
    RT_USBDriver *drv = (RT_USBDriver *)usbp;
    stm32_otg_t *otgp = (stm32_otg_t *)drv->otg;
    RT_USBEndpointConfig *epcp = drv->epc[ep];
    uint32_t ctl, fsize;

    if (ep == 0 || epcp == NULL)
        return;

    /* IN and OUT common parameters. */
    switch (epcp->ep_mode & USB_EP_MODE_TYPE) {
    case USB_EP_MODE_TYPE_CTRL:
        ctl = DIEPCTL_SD0PID | DIEPCTL_USBAEP | DIEPCTL_EPTYP_CTRL;
        break;
    case USB_EP_MODE_TYPE_ISO:
        ctl = DIEPCTL_SD0PID | DIEPCTL_USBAEP | DIEPCTL_EPTYP_ISO;
        break;
    case USB_EP_MODE_TYPE_BULK:
        ctl = DIEPCTL_SD0PID | DIEPCTL_USBAEP | DIEPCTL_EPTYP_BULK;
        break;
    case USB_EP_MODE_TYPE_INTR:
        ctl = DIEPCTL_SD0PID | DIEPCTL_USBAEP | DIEPCTL_EPTYP_INTR;
        break;
    default:
        return;
    }

    /* OUT endpoint activation or deactivation. */
    otgp->oe[ep].DOEPTSIZ = 0;
    if (epcp->out_state != NULL) {
        otgp->oe[ep].DOEPCTL = ctl | DOEPCTL_MPSIZ(epcp->out_maxsize);
        otgp->DAINTMSK |= DAINTMSK_OEPM(ep);
    } else {
        otgp->oe[ep].DOEPCTL &= ~DOEPCTL_USBAEP;
        otgp->DAINTMSK &= ~DAINTMSK_OEPM(ep);
    }

    /* IN endpoint activation or deactivation. */
    otgp->ie[ep].DIEPTSIZ = 0;
    if (epcp->in_state != NULL) {
        fsize = epcp->in_maxsize / 4;
        if (epcp->in_multiplier > 1)
            fsize *= epcp->in_multiplier;
        otgp->DIEPTXF[ep - 1] = DIEPTXF_INEPTXFD(fsize) |
                                DIEPTXF_INEPTXSA(otg_ram_alloc(drv, fsize));
        otg_txfifo_flush(drv, ep);

        otgp->ie[ep].DIEPCTL = ctl |
                               DIEPCTL_TXFNUM(ep) |
                               DIEPCTL_MPSIZ(epcp->in_maxsize);
        otgp->DAINTMSK |= DAINTMSK_IEPM(ep);
    } else {
        otgp->DIEPTXF[ep - 1] = 0x02000400; /* Reset value. */
        otg_txfifo_flush(drv, ep);
        otgp->ie[ep].DIEPCTL &= ~DIEPCTL_USBAEP;
        otgp->DAINTMSK &= ~DAINTMSK_IEPM(ep);
    }
}

/* -------------------------------------------------------------------------- */
/* usb_lld_disable_endpoints — disables all endpoints except EP0               */
/* -------------------------------------------------------------------------- */

void usb_lld_disable_endpoints(void *usbp)
{
    RT_USBDriver *drv = (RT_USBDriver *)usbp;

    otg_ram_reset(drv);
    otg_disable_ep(drv);
}

/* -------------------------------------------------------------------------- */
/* usb_lld_get_status_out — returns status of an OUT endpoint                  */
/* -------------------------------------------------------------------------- */

usbepstatus_t usb_lld_get_status_out(void *usbp, usbep_t ep)
{
    RT_USBDriver *drv = (RT_USBDriver *)usbp;
    stm32_otg_t *otgp = (stm32_otg_t *)drv->otg;
    uint32_t ctl = otgp->oe[ep].DOEPCTL;

    if (!(ctl & DOEPCTL_USBAEP))
        return EP_STATUS_DISABLED;
    if (ctl & DOEPCTL_STALL)
        return EP_STATUS_STALLED;
    return EP_STATUS_ACTIVE;
}

/* -------------------------------------------------------------------------- */
/* usb_lld_get_status_in — returns status of an IN endpoint                    */
/* -------------------------------------------------------------------------- */

usbepstatus_t usb_lld_get_status_in(void *usbp, usbep_t ep)
{
    RT_USBDriver *drv = (RT_USBDriver *)usbp;
    stm32_otg_t *otgp = (stm32_otg_t *)drv->otg;
    uint32_t ctl = otgp->ie[ep].DIEPCTL;

    if (!(ctl & DIEPCTL_USBAEP))
        return EP_STATUS_DISABLED;
    if (ctl & DIEPCTL_STALL)
        return EP_STATUS_STALLED;
    return EP_STATUS_ACTIVE;
}

/* -------------------------------------------------------------------------- */
/* usb_lld_read_setup — reads a setup packet from the dedicated buffer        */
/* -------------------------------------------------------------------------- */

void usb_lld_read_setup(void *usbp, usbep_t ep, uint8_t *buf)
{
    RT_USBDriver *drv = (RT_USBDriver *)usbp;

    memcpy(buf, drv->epc[ep]->setup_buf, 8);
}

/* -------------------------------------------------------------------------- */
/* usb_lld_start_out — starts a receive operation on an OUT endpoint          */
/* -------------------------------------------------------------------------- */

void usb_lld_start_out(void *usbp, usbep_t ep)
{
    RT_USBDriver *drv = (RT_USBDriver *)usbp;
    stm32_otg_t *otgp = (stm32_otg_t *)drv->otg;
    USBOutEndpointState *osp = (USBOutEndpointState *)drv->epc[ep]->out_state;
    uint32_t pcnt, rxsize;

    if (osp == NULL)
        return;

    osp->totsize = osp->rxsize;
    if ((ep == 0) && (osp->rxsize > EP0_MAX_PACKET))
        osp->rxsize = EP0_MAX_PACKET;

    if (osp->rxsize == 0U) {
        pcnt = 1U;
        rxsize = 0U;
    } else {
        pcnt   = (osp->rxsize + drv->epc[ep]->out_maxsize - 1U) /
                 drv->epc[ep]->out_maxsize;
        rxsize = (pcnt * drv->epc[ep]->out_maxsize + 3U) & 0xFFFFFFFCU;
    }

    /* EP0 status OUT must not set STUPCNT; EP0 data OUT keeps STUPCNT(3) per ChibiOS. */
    if ((ep == 0U) && (osp->rxsize == 0U)) {
        otgp->oe[ep].DOEPTSIZ = DOEPTSIZ_PKTCNT(pcnt) | DOEPTSIZ_XFRSIZ(rxsize);
    } else if (ep == 0U) {
        otgp->oe[ep].DOEPTSIZ = DOEPTSIZ_STUPCNT(3) |
                                DOEPTSIZ_PKTCNT(pcnt) |
                                DOEPTSIZ_XFRSIZ(rxsize);
    } else {
        otgp->oe[ep].DOEPTSIZ = DOEPTSIZ_PKTCNT(pcnt) | DOEPTSIZ_XFRSIZ(rxsize);
    }

    if ((drv->epc[ep]->ep_mode & USB_EP_MODE_TYPE) == USB_EP_MODE_TYPE_ISO) {
        if (otgp->DSTS & DSTS_FNSOF_ODD)
            otgp->oe[ep].DOEPCTL |= DOEPCTL_SEVNFRM;
        else
            otgp->oe[ep].DOEPCTL |= DOEPCTL_SODDFRM;
    }

    otgp->oe[ep].DOEPCTL |= DOEPCTL_EPENA | DOEPCTL_CNAK;
}

/* -------------------------------------------------------------------------- */
/* usb_lld_start_in — starts a transmit operation on an IN endpoint           */
/* -------------------------------------------------------------------------- */

void usb_lld_start_in(void *usbp, usbep_t ep)
{
    RT_USBDriver *drv = (RT_USBDriver *)usbp;
    stm32_otg_t *otgp = (stm32_otg_t *)drv->otg;
    USBInEndpointState *isp = (USBInEndpointState *)drv->epc[ep]->in_state;

    if (isp == NULL)
        return;

    isp->totsize = isp->txsize;

    if (isp->txsize == 0) {
        /* Match ChibiOS usb_lld_start_in() ZLP: PKTCNT(1)|XFRSIZ(0) only. */
        otgp->ie[ep].DIEPTSIZ = DIEPTSIZ_PKTCNT(1) | DIEPTSIZ_XFRSIZ(0);
    } else {
        if ((ep == 0) && (isp->txsize > EP0_MAX_PACKET))
            isp->txsize = EP0_MAX_PACKET;

        uint32_t pcnt = (isp->txsize + drv->epc[ep]->in_maxsize - 1) /
                        drv->epc[ep]->in_maxsize;
        otgp->ie[ep].DIEPTSIZ = DIEPTSIZ_MCNT(1) |
                                DIEPTSIZ_PKTCNT(pcnt) |
                                DIEPTSIZ_XFRSIZ(isp->txsize);
    }

    if ((drv->epc[ep]->ep_mode & USB_EP_MODE_TYPE) == USB_EP_MODE_TYPE_ISO) {
        if (otgp->DSTS & DSTS_FNSOF_ODD)
            otgp->ie[ep].DIEPCTL |= DIEPCTL_SEVNFRM;
        else
            otgp->ie[ep].DIEPCTL |= DIEPCTL_SODDFRM;
    }

    otgp->ie[ep].DIEPCTL |= DIEPCTL_EPENA | DIEPCTL_CNAK;
    otgp->DIEPEMPMSK |= DIEPEMPMSK_INEPTXFEM(ep);
}

/* -------------------------------------------------------------------------- */
/* usb_lld_stall_out / stall_in — stall an endpoint                           */
/* -------------------------------------------------------------------------- */

void usb_lld_stall_out(void *usbp, usbep_t ep)
{
    RT_USBDriver *drv = (RT_USBDriver *)usbp;
    ((stm32_otg_t *)drv->otg)->oe[ep].DOEPCTL |= DOEPCTL_STALL;
}

void usb_lld_stall_in(void *usbp, usbep_t ep)
{
    RT_USBDriver *drv = (RT_USBDriver *)usbp;
    ((stm32_otg_t *)drv->otg)->ie[ep].DIEPCTL |= DIEPCTL_STALL;
}

/* -------------------------------------------------------------------------- */
/* usb_lld_clear_out / clear_in — clear stall on an endpoint                  */
/* -------------------------------------------------------------------------- */

void usb_lld_clear_out(void *usbp, usbep_t ep)
{
    RT_USBDriver *drv = (RT_USBDriver *)usbp;
    ((stm32_otg_t *)drv->otg)->oe[ep].DOEPCTL &= ~DOEPCTL_STALL;
}

void usb_lld_clear_in(void *usbp, usbep_t ep)
{
    RT_USBDriver *drv = (RT_USBDriver *)usbp;
    ((stm32_otg_t *)drv->otg)->ie[ep].DIEPCTL &= ~DIEPCTL_STALL;
}

/* ========================================================================== */
/* ISR handlers                                                               */
/* ========================================================================== */

void OTG_FS_IRQHandler(void)
{
    usb_lld_serve_interrupt(&rtt_usb);
}

/* ========================================================================== */
/* Higher-level HAL functions                                                  */
/* ========================================================================== */

void usb_object_init(void *usbp)
{
    RT_USBDriver *drv = (RT_USBDriver *)usbp;

    memset(drv, 0, sizeof(RT_USBDriver));
    drv->state    = USB_STATE_STOP;
    drv->ep0max   = EP0_MAX_PACKET;
    drv->otg      = (void *)OTG_FS_BASE;
    drv->otgparams = (void *)&fs_params;
}

void usb_setup_transfer(void *usbp, const void *buf, size_t len,
                        void (*callback)(void *usbp))
{
    RT_USBDriver *drv = (RT_USBDriver *)usbp;

    drv->ep0data   = (const uint8_t *)buf;
    drv->ep0len    = len;
    drv->ep0max    = ((RT_USBEndpointConfig *)drv->epc[0])->in_maxsize;
    drv->ep0endcb  = callback;

    if (len == 0) {
        /* No data stage: status direction follows setup bmRequestType bit 7
         * (ChibiOS hal_usb.c _usb_ep0setup after usbSetupTransfer). */
        if ((drv->setup[0] & USB_DIR_MASK) == USB_DIR_DEVICE_TO_HOST) {
            /* Device→host setup with wLength=0: host STATUS OUT ZLP. */
            drv->ep0state = USB_EP0_STATE_WAITING_STATUS_OUT;
            {
                USBOutEndpointState *osp =
                    (USBOutEndpointState *)drv->epc[0]->out_state;
                osp->rxbuf   = NULL;
                osp->rxsize  = 0;
                osp->rxcnt   = 0;
                osp->totsize = 0;
            }
            usb_lld_start_out(drv, 0);
        } else {
            /* Host→device (SET_ADDRESS, SET_CONFIGURATION, …): STATUS IN ZLP. */
            rtt_dbg_usb_ep0_sts_in++;
            drv->ep0state = USB_EP0_STATE_WAITING_STATUS_IN;
            {
                USBInEndpointState *isp =
                    (USBInEndpointState *)drv->epc[0]->in_state;
                isp->txbuf   = NULL;
                isp->txsize  = 0;
                isp->txcnt   = 0;
                isp->totsize = 0;
            }
            usb_lld_start_in(drv, 0);
        }
    } else {
        /* Data IN stage (device sends data to host). */
        drv->ep0state = USB_EP0_STATE_WAITING_DATA_IN;
        {
            /* Match ChibiOS usbStartTransmitI: full transfer size in txsize;
             * otg_epin_handler + usb_lld_start_in split EP0 into MPS packets. */
            USBInEndpointState *isp = (USBInEndpointState *)drv->epc[0]->in_state;
            isp->txbuf  = (const uint8_t *)buf;
            isp->txsize = (uint16_t)len;
            isp->txcnt  = 0;
        }
        usb_lld_start_in(drv, 0);
    }
}

/* -------------------------------------------------------------------------- */
/* usb_lld_connect_bus / usb_lld_disconnect_bus                               */
/* -------------------------------------------------------------------------- */

void usb_lld_connect_bus(void *usbp)
{
    RT_USBDriver *drv = (RT_USBDriver *)usbp;
#if STM32_OTG_STEPPING == 1
    ((stm32_otg_t *)drv->otg)->GCCFG |= GCCFG_VBUSBSEN;
#else
    ((stm32_otg_t *)drv->otg)->DCTL &= ~DCTL_SDIS;
#endif
}

void usb_lld_disconnect_bus(void *usbp)
{
    RT_USBDriver *drv = (RT_USBDriver *)usbp;
#if STM32_OTG_STEPPING == 1
    ((stm32_otg_t *)drv->otg)->GCCFG &= ~GCCFG_VBUSBSEN;
#else
    ((stm32_otg_t *)drv->otg)->DCTL |= DCTL_SDIS;
#endif
}

/* ========================================================================== */
/* Compatibility layer (existing RTT API for UARTDriver.cpp)                  */
/* ========================================================================== */

/* -------------------------------------------------------------------------- */
/* usb_lld_init_rtt — single-call full hardware initialization                */
/* -------------------------------------------------------------------------- */

bool usb_lld_init_rtt(void)
{
    if (_usb_driver_inited)
        return true;

    /* Initialize the driver object. */
    usb_object_init(&rtt_usb);

    /* Enable OTG_FS clock and reset. */
    RCC->AHB2ENR |= RCC_AHB2ENR_OTGFSEN;
    __DSB();
    RCC->AHB2RSTR |= RCC_AHB2RSTR_OTGFSRST;
    __DSB();
    RCC->AHB2RSTR &= ~RCC_AHB2RSTR_OTGFSRST;
    __DSB();

    /* GPIO: PA9 (VBUS), PA11 (DM), PA12 (DP) — AF10. */
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;
    __DSB();

    GPIOA->MODER   = (GPIOA->MODER & ~(3U << 22)) | (2U << 22);  /* PA11 AF */
    GPIOA->AFR[1]  = (GPIOA->AFR[1] & ~(0xFU << 12)) | (10U << 12);
    GPIOA->OSPEEDR |= (3U << 22);
    GPIOA->PUPDR   &= ~(3U << 22);

    GPIOA->MODER   = (GPIOA->MODER & ~(3U << 24)) | (2U << 24);  /* PA12 AF */
    GPIOA->AFR[1]  = (GPIOA->AFR[1] & ~(0xFU << 16)) | (10U << 16);
    GPIOA->OSPEEDR |= (3U << 24);
    GPIOA->PUPDR   &= ~(3U << 24);

    GPIOA->MODER   &= ~(3U << 18);  /* PA9 input (VBUS) */

    /* PWR: enable USB supply. */
    RCC->APB1ENR |= RCC_APB1ENR_PWREN;
    __DSB();
    osalSysPolledDelayX(100);
    PWR->CR2 |= (1UL << 0);  /* USV: USB Supply Valid */
    __DSB();
    osalSysPolledDelayX(1000);

    /* GUSBCFG: forced device, FS 1.1 PHY. */
    {
        stm32_otg_t *otgp = (stm32_otg_t *)rtt_usb.otg;
        otgp->GUSBCFG = GUSBCFG_FDMOD | GUSBCFG_TRDT(TRDT_VALUE_FS) |
                        GUSBCFG_PHYSEL;
        __DSB();

        /* DCFG: set after core reset (line after otg_core_reset). */

        /* PCGCCTL: PHY enabled. */
        otgp->PCGCCTL = 0;

        otg_core_reset(&rtt_usb);

        /* Re-program GUSBCFG after core reset. */
        otgp->GUSBCFG = GUSBCFG_FDMOD | GUSBCFG_TRDT(TRDT_VALUE_FS) |
                        GUSBCFG_PHYSEL;
        __DSB();

        /* DCFG must be re-programmed AFTER core reset (core reset clears it). */
        otgp->DCFG = 0x02200000 | DCFG_DSPD_FS11;
        __DSB();

        /* GAHBCFG: no DMA, no global int yet. */
        otgp->GAHBCFG = 0;

        /* Disable endpoints, clear masks. */
        otg_disable_ep(&rtt_usb);
        otgp->DIEPMSK  = 0;
        otgp->DOEPMSK  = 0;
        otgp->DAINTMSK = 0;

        /* Initial GINTMSK (EP/RX masks added in usb_lld_reset via USBRST). */
        otgp->GINTMSK  = GINTMSK_ENUMDNEM | GINTMSK_USBRSTM |
                         GINTMSK_USBSUSPM | GINTMSK_ESUSPM |
                         GINTMSK_SRQM     | GINTMSK_WKUM |
                         GINTMSK_IISOIXFRM | GINTMSK_IISOOXFRM;

        /* Clear pending interrupts before enabling global int. */
        otgp->GINTSTS = 0xFFFFFFFF;

        /* Enable global interrupt. */
        otgp->GAHBCFG |= GAHBCFG_GINTMSK;
        __DSB();

        /* VBUS sensing + transceiver. */
        otgp->GOTGCTL = GOTGCTL_BVALOEN | GOTGCTL_BVALOVAL;
        otgp->GCCFG   = GCCFG_INIT_VALUE;
        __DSB();
    }

    /* NVIC: enable OTG_FS_IRQn at priority 5. */
    NVIC_SetPriority(OTG_FS_IRQn, NVIC_EncodePriority(
        NVIC_GetPriorityGrouping(), 5, 0));
    NVIC_EnableIRQ(OTG_FS_IRQn);
    __DSB();

    _usb_driver_inited = true;

    (void)usb_cdc_init();

    /* Soft disconnect → reconnect cycle to trigger bus reset.
     *
     * NOTE: Use read-modify-write (RMW) to clear SDIS, NOT direct write =0.
     * Some STM32F767 variants have read-only status bits in DCTL (bit25).
     * Writing 0 to ALL bits clears these read-only bits, causing hardware
     * to auto-restore them — which also re-asserts SDIS back to 1!
     * See test_L6_cdc/main.c:1077 for the original discovery. */
    {
        stm32_otg_t *otgp = (stm32_otg_t *)rtt_usb.otg;
        otgp->DCTL = DCTL_SDIS;
        __DSB();
        osalSysPolledDelayX(50000);
        otgp->DCTL &= ~DCTL_SDIS;
        __DSB();
        osalSysPolledDelayX(50000);
    }

    return true;
}

/* -------------------------------------------------------------------------- */
/* usb_lld_poll_rtt — non-interrupt polling entry point                       */
/* -------------------------------------------------------------------------- */

void usb_lld_poll_rtt(void)
{
    if (!_usb_driver_inited)
        return;

    /* Check for pending interrupts and dispatch. */
    stm32_otg_t *otgp = (stm32_otg_t *)rtt_usb.otg;
    if (otgp->GINTSTS & otgp->GINTMSK)
        usb_lld_serve_interrupt(&rtt_usb);
}

/* -------------------------------------------------------------------------- */
/* usb_lld_send_rtt — send data on CDC IN endpoint (polled + ISR compatible)  */
/* -------------------------------------------------------------------------- */

bool usb_lld_send_rtt(uint8_t ep, const uint8_t *data, uint32_t len)
{
    RT_USBDriver *drv = &rtt_usb;
    stm32_otg_t *otgp;

    if (!_usb_driver_inited || ep == 0 || ep >= ARRAY_SIZE(drv->epc) ||
        drv->epc[ep] == NULL || drv->epc[ep]->in_state == NULL)
        return false;

    if (data == NULL || len == 0)
        return false;

    otgp = (stm32_otg_t *)drv->otg;

    /* Clamp to max packet size. */
    {
        uint16_t mps = drv->epc[ep]->in_maxsize;
        if (len > mps)
            len = mps;
    }

    /* Check if endpoint is already enabled (busy). */
    if (otgp->ie[ep].DIEPCTL & DIEPCTL_EPENA)
        return false;

    /* Check TX FIFO space. */
    uint32_t fifo_avail = otgp->ie[ep].DTXFSTS & DTXFSTS_INEPTFSAV_MASK;
    uint32_t needed = (len + 3) / 4;
    if (fifo_avail < needed)
        return false;

    /* Write data directly to TX FIFO. */
    otg_fifo_write_from_buffer(otgp->FIFO[ep], data, len);

    /* Set up DIEPTSIZ. */
    uint32_t pcnt = 1;
    otgp->ie[ep].DIEPTSIZ = DIEPTSIZ_MCNT(1) |
                            DIEPTSIZ_PKTCNT(pcnt) |
                            DIEPTSIZ_XFRSIZ(len);

    /* Enable endpoint. */
    otgp->ie[ep].DIEPCTL |= DIEPCTL_EPENA | DIEPCTL_CNAK;
    __DSB();

    /* Poll for XFRC completion (short timeout for polled TX). */
    uint32_t timeout = 50000;
    while (!(otgp->ie[ep].DIEPINT & DIEPINT_XFRC)) {
        if (--timeout == 0) {
            otgp->ie[ep].DIEPCTL |= DIEPCTL_EPDIS;
            otgp->ie[ep].DIEPINT = 0xFFFFFFFF;
            return false;
        }
        __NOP();
    }

    otgp->ie[ep].DIEPINT = DIEPINT_XFRC;
    return true;
}

uint32_t usb_lld_txspace_rtt(uint8_t ep)
{
    if (!_usb_driver_inited || ep == 0 || ep >= ARRAY_SIZE(rtt_usb.epc) ||
        rtt_usb.epc[ep] == NULL || rtt_usb.epc[ep]->in_state == NULL) {
        return 0;
    }
    if (rtt_usb.state != USB_STATE_ACTIVE) {
        return 0;
    }

    stm32_otg_t *otgp = (stm32_otg_t *)rtt_usb.otg;
    if (otgp->ie[ep].DIEPCTL & DIEPCTL_EPENA) {
        return 0;
    }

    const uint32_t fifo_words = otgp->ie[ep].DTXFSTS & DTXFSTS_INEPTFSAV_MASK;
    const uint32_t fifo_bytes = fifo_words * 4U;
    const uint32_t mps = rtt_usb.epc[ep]->in_maxsize;
    return fifo_bytes > mps ? mps : fifo_bytes;
}

/* -------------------------------------------------------------------------- */
/* usb_lld_set_rx_callback — register CDC data receive callback               */
/* -------------------------------------------------------------------------- */

void usb_lld_set_rx_callback(usb_rx_callback_t cb, void *arg)
{
    usb_cdc_set_rx_callback_idx(0, cb, arg);
}

void usb_lld_set_rx_callback_idx(uint8_t idx, usb_rx_callback_t cb, void *arg)
{
    usb_cdc_set_rx_callback_idx(idx, cb, arg);
}

/* -------------------------------------------------------------------------- */
/* usb_lld_rearm_cdc_out — re-arm EP2 OUT for receiving next CDC packet       */
/* -------------------------------------------------------------------------- */

void usb_lld_rearm_cdc_out(void)
{
    usb_cdc_rearm_out_idx(0);
}

void usb_lld_rearm_cdc_out_idx(uint8_t idx)
{
    usb_cdc_rearm_out_idx(idx);
}

/* -------------------------------------------------------------------------- */
/* usb_lld_is_configured_rtt / get_connected_rtt                              */
/* -------------------------------------------------------------------------- */

bool usb_lld_is_configured_rtt(void)
{
    return rtt_usb.state == USB_STATE_ACTIVE;
}

bool usb_lld_is_configured_idx_rtt(uint8_t idx)
{
    return rtt_usb.state == USB_STATE_ACTIVE && usb_cdc_is_configured_idx(idx);
}

bool usb_lld_get_connected_rtt(void)
{
    return rtt_usb.state >= USB_STATE_SELECTED;
}

bool usb_lld_get_connected_idx_rtt(uint8_t idx)
{
    return rtt_usb.state >= USB_STATE_SELECTED && usb_cdc_is_connected_idx(idx);
}

/* -------------------------------------------------------------------------- */
/* usb_lld_set_address_rtt — compatibility wrapper                            */
/* -------------------------------------------------------------------------- */

void usb_lld_set_address_rtt(uint8_t addr)
{
    rtt_usb.address = addr;
    usb_lld_set_address(&rtt_usb);
}

/* -------------------------------------------------------------------------- */
/* usb_lld_send_cdc_notification — send CDC serial state notification         */
/* -------------------------------------------------------------------------- */

bool usb_lld_send_cdc_notification(uint16_t serial_state)
{
    uint8_t notify[10];

    /* CDC Notification header (SEND_ENCAPSULATED_REQUEST / SerialState). */
    notify[0] = 0xA1;       /* bmRequestType: Device→Host, Interface, Class */
    notify[1] = 0x20;       /* bNotification: Serial State */
    notify[2] = 0x00;       /* wValue */
    notify[3] = 0x00;
    notify[4] = 0x00;       /* wIndex: Interface 0 */
    notify[5] = 0x00;
    notify[6] = 0x02;       /* wLength = 2 */
    notify[7] = 0x00;
    notify[8] = (uint8_t)(serial_state & 0xFF);
    notify[9] = (uint8_t)((serial_state >> 8) & 0xFF);

    return usb_lld_send_rtt(3, notify, 10);
}
