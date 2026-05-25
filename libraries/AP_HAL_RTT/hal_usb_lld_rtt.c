/*
 * RTT adaptation of ChibiOS STM32 OTGv1 LLD (hal_usb_lld_rtt.c).
 *
 * Complete self-contained DWC2 USB device driver:
 *  - Direct DWC2 register operations (polling mode, no interrupts)
 *  - EP0 control transfers: USB enumeration, standard requests
 *  - CDC ACM class: EP1 IN (bulk, device→host), EP2 OUT (bulk, host→device)
 *  - Embedded USB descriptors (device, config, string)
 *  - No CherryUSB dependency
 *
 * ChibiOS reference:
 *   modules/ChibiOS/os/hal/ports/STM32/LLD/OTGv1/hal_usb_lld.c
 *   modules/ChibiOS/os/hal/ports/STM32/LLD/OTGv1/stm32_otg.h
 *
 * STM32F767 DWC2 register layout (RM0410 §45):
 *   Global:   base + 0x000  (USB_OTG_GlobalTypeDef)
 *   Device:   base + 0x800  (USB_OTG_DeviceTypeDef)
 *   IN EP:    base + 0x900 + ep*0x20
 *   OUT EP:   base + 0xB00 + ep*0x20
 *   FIFO:     base + 0x1000 + ep*4
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
#define GINTMSK_IISOIXFRM       0x00100000UL
#define GINTMSK_IISOOXFRM       0x00200000UL
#define GINTMSK_OEPM            0x00080000UL
#define GINTMSK_IEPM            0x00040000UL

#define GINTSTS_USBRST          0x00000002UL
#define GINTSTS_WKUPINT         0x80000000UL
#define GINTSTS_USBSUSP         0x00000001UL
#define GINTSTS_ENUMDNE         0x00002000UL
#define GINTSTS_SOF             0x00000008UL
#define GINTSTS_RXFLVL          0x00000010UL
#define GINTSTS_OEPINT          0x00080000UL
#define GINTSTS_IEPINT          0x00040000UL
#define GINTSTS_IISOIXFR        0x00100000UL
#define GINTSTS_IISOOXFR        0x00200000UL

/* ---- GRXSTSP / GRXSTSR ---- */
#define GRXSTSP_BCNT_MASK       0x0000001FUL
#define GRXSTSP_BCNT_SHIFT      0
#define GRXSTSP_EPNUM_MASK      0x07C00000UL
#define GRXSTSP_EPNUM_SHIFT     22
#define GRXSTSP_PKTSTS_MASK     0x001E0000UL
#define GRXSTSP_PKTSTS_SHIFT    17
#define GRXSTSP_SETUP_DATA      2
#define GRXSTSP_SETUP_COMP      4
#define GRXSTSP_OUT_DATA        1
#define GRXSTSP_OUT_COMP        3
#define GRXSTSP_OUT_DONE        5   /* Global IN NAK? Not common, but handle */

/* ---- DCFG (0x800) ---- */
#define DCFG_DSPD_MASK          0x00000003UL
#define DCFG_DSPD_FS11          0x00000003UL       /* 48MHz FS 1.1 PHY */
#define DCFG_DSPD_HS            0x00000000UL
#define DCFG_DSPD_HS_FS         0x00000001UL
#define DCFG_DAD_MASK           0x00007FF0UL
#define DCFG_DAD_SHIFT          4
#define DCFG_DAD(addr)          (((addr) << DCFG_DAD_SHIFT) & DCFG_DAD_MASK)

/* ---- DSTS (0x808) ---- */
#define DSTS_ENUMSPD_MASK       0x00000006UL
#define DSTS_ENUMSPD_SHIFT      1
#define DSTS_ENUMSPD_FS11       0x00000002UL
#define DSTS_ENUMSPD_HS         0x00000000UL
#define DSTS_ENUMSPD_FS48       0x00000004UL
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
#define DIEPCTL_EPTYP_ISO       0x00000000UL
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
#define DIEPINT_EPDISBLD        0x00000004UL
#define DIEPINT_TOC             0x00000008UL
#define DIEPINT_TXFE            0x00000080UL
#define DIEPINT_INEPNE          0x00000040UL

/* ---- DOEPINT ---- */
#define DOEPINT_XFRC            0x00000001UL
#define DOEPINT_EPDISBLD        0x00000004UL
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
#define DOEPTSIZ_XFRSIZ_MASK    0x0007FFFFUL
#define DOEPTSIZ_XFRSIZ_SHIFT   0
#define DOEPTSIZ_XFRSIZ(n)      (((n) << DOEPTSIZ_XFRSIZ_SHIFT) & DOEPTSIZ_XFRSIZ_MASK)
#define DOEPTSIZ_PKTCNT_MASK    0x1FF80000UL
#define DOEPTSIZ_PKTCNT_SHIFT   19
#define DOEPTSIZ_PKTCNT(n)      (((n) << DOEPTSIZ_PKTCNT_SHIFT) & DOEPTSIZ_PKTCNT_MASK)

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

/* ---- PCGCCTL (USB 0xE00) ---- */
#define PCGCCTL_STPPCLK         0x00000001UL
#define PCGCCTL_GATEHCLK        0x00000002UL
#define _PCGCCTL                (*((volatile uint32_t *)(USB_OTG_FS_PERIPH_BASE + 0xE00)))

/* ---- DIEPEMPMSK ---- */
#define DIEPEMPMSK_INEPTXFEM(ep) (1UL << (ep))

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
#define EP0_TX_FIFO_SIZE_WORDS  16              /* 64 bytes / 4 */
#define EP1_TX_FIFO_SIZE_WORDS  16              /* 64 bytes / 4 */
#define EP0_MAX_PACKET          64
#define EP1_MAX_PACKET          64
#define EP2_MAX_PACKET          64
#define SEND_TIMEOUT            50000           /* Poll loop timeout */
#define OTG_FIFO_MEM_SIZE       320             /* OTG1 FS FIFO size in words */

/* USB standard request codes */
#define USB_REQ_GET_STATUS          0x00
#define USB_REQ_CLEAR_FEATURE       0x01
#define USB_REQ_SET_FEATURE         0x03
#define USB_REQ_SET_ADDRESS         0x05
#define USB_REQ_GET_DESCRIPTOR      0x06
#define USB_REQ_SET_DESCRIPTOR      0x07
#define USB_REQ_GET_CONFIGURATION   0x08
#define USB_REQ_SET_CONFIGURATION   0x09
#define USB_REQ_GET_INTERFACE       0x0A
#define USB_REQ_SET_INTERFACE       0x0B

/* Descriptor types */
#define USB_DTYPE_DEVICE            1
#define USB_DTYPE_CONFIG            2
#define USB_DTYPE_STRING            3
#define USB_DTYPE_INTERFACE         4
#define USB_DTYPE_ENDPOINT          5
#define USB_DTYPE_DEVICE_QUALIFIER  6
#define USB_DTYPE_OTHER_SPEED       7
#define USB_DTYPE_IAD               0x0B   /* Interface Association Descriptor */

/* CDC class codes */
#define CDC_COMM_INTFACE            2
#define CDC_DATA_INTFACE            0x0A
#define CDC_SCS_HEADER              0x00
#define CDC_SCS_CALL_MGMT           0x01
#define CDC_SCS_ACM                 0x02
#define CDC_SCS_UNION               0x06

/* EP0 state machine */
#define EP0_STATE_IDLE              0
#define EP0_STATE_DATA_IN           1   /* Sending data to host */
#define EP0_STATE_DATA_OUT          2   /* Receiving data from host */
#define EP0_STATE_STATUS_IN         3   /* Zero-length status IN */
#define EP0_STATE_STATUS_OUT        4   /* Zero-length status OUT */
#define EP0_STATE_STALL             5   /* Stalled */

/* ========================================================================== */
/* USB Descriptors (CDC ACM, IAD)                                             */
/* ========================================================================== */

/*
 * Device descriptor: CDC ACM, vid=0x1209, pid=0x5741
 */
static const uint8_t usb_dev_desc[] = {
    18,                    /* bLength */
    USB_DTYPE_DEVICE,      /* bDescriptorType */
    0x00, 0x02,            /* bcdUSB = 2.00 */
    0x02,                  /* bDeviceClass: CDC (Communications Device Class) */
    0x00,                  /* bDeviceSubClass */
    0x00,                  /* bDeviceProtocol */
    EP0_MAX_PACKET,        /* bMaxPacketSize0 */
    0x09, 0x12,            /* idVendor = 0x1209 */
    0x41, 0x57,            /* idProduct = 0x5741 */
    0x00, 0x02,            /* bcdDevice = 2.00 */
    0x01,                  /* iManufacturer */
    0x02,                  /* iProduct */
    0x03,                  /* iSerialNumber */
    0x01                   /* bNumConfigurations */
};

/*
 * Configuration descriptor with CDC ACM IAD:
 *   IAD + CDC Communication Interface (EP3 IN interrupt for notification)
 *   + CDC Data Interface (EP1 IN bulk, EP2 OUT bulk)
 */
static const uint8_t usb_cfg_desc[] = {
    /* ---- Configuration descriptor ---- */
    9,                     /* bLength */
    USB_DTYPE_CONFIG,      /* bDescriptorType */
    0x43, 0x00,            /* wTotalLength = 67 */
    2,                     /* bNumInterfaces */
    1,                     /* bConfigurationValue */
    0,                     /* iConfiguration */
    0xC0,                  /* bmAttributes: Self-powered */
    50,                    /* bMaxPower = 100mA */

    /* ---- IAD (Interface Association Descriptor) ---- */
    8,                     /* bLength */
    USB_DTYPE_IAD,         /* bDescriptorType */
    0,                     /* bFirstInterface */
    2,                     /* bInterfaceCount */
    CDC_COMM_INTFACE,      /* bFunctionClass */
    2,                     /* bFunctionSubClass: Abstract Control Model */
    1,                     /* bFunctionProtocol: AT-commands (v.250 etc.) */
    0,                     /* iFunction */

    /* ---- Interface 0: CDC Communication Interface ---- */
    9,                     /* bLength */
    USB_DTYPE_INTERFACE,   /* bDescriptorType */
    0,                     /* bInterfaceNumber */
    0,                     /* bAlternateSetting */
    1,                     /* bNumEndpoints */
    CDC_COMM_INTFACE,      /* bInterfaceClass: Communications */
    2,                     /* bInterfaceSubClass: Abstract Control Model */
    1,                     /* bInterfaceProtocol: AT-commands */
    0,                     /* iInterface */

    /* CDC Header Functional Descriptor */
    5,                     /* bFunctionLength */
    0x24,                  /* bDescriptorType: CS_INTERFACE */
    CDC_SCS_HEADER,        /* bDescriptorSubtype */
    0x10, 0x01,            /* bcdCDC = 1.10 */

    /* CDC Call Management Functional Descriptor */
    5,                     /* bFunctionLength */
    0x24,                  /* bDescriptorType: CS_INTERFACE */
    CDC_SCS_CALL_MGMT,     /* bDescriptorSubtype */
    0x01,                  /* bmCapabilities: Device handles call management */
    1,                     /* bDataInterface */

    /* CDC ACM Functional Descriptor */
    4,                     /* bFunctionLength */
    0x24,                  /* bDescriptorType: CS_INTERFACE */
    CDC_SCS_ACM,           /* bDescriptorSubtype */
    0x02,                  /* bmCapabilities: Device supports line coding + serial state */

    /* CDC Union Functional Descriptor */
    5,                     /* bFunctionLength */
    0x24,                  /* bDescriptorType: CS_INTERFACE */
    CDC_SCS_UNION,         /* bDescriptorSubtype */
    0,                     /* bMasterInterface (CDC Comm) */
    1,                     /* bSlaveInterface0 (CDC Data) */

    /* EP3 IN: Interrupt (CDC notification endpoint) */
    7,                     /* bLength */
    USB_DTYPE_ENDPOINT,    /* bDescriptorType */
    0x83,                  /* bEndpointAddress: IN EP3 */
    0x03,                  /* bmAttributes: Interrupt */
    0x08, 0x00,            /* wMaxPacketSize = 8 */
    0x10,                  /* bInterval = 16ms */

    /* ---- Interface 1: CDC Data Interface ---- */
    9,                     /* bLength */
    USB_DTYPE_INTERFACE,   /* bDescriptorType */
    1,                     /* bInterfaceNumber */
    0,                     /* bAlternateSetting */
    2,                     /* bNumEndpoints */
    CDC_DATA_INTFACE,      /* bInterfaceClass: CDC Data */
    0x00,                  /* bInterfaceSubClass */
    0x00,                  /* bInterfaceProtocol */
    0,                     /* iInterface */

    /* EP1 IN: Bulk (CDC data device→host) */
    7,                     /* bLength */
    USB_DTYPE_ENDPOINT,    /* bDescriptorType */
    0x81,                  /* bEndpointAddress: IN EP1 */
    0x02,                  /* bmAttributes: Bulk */
    EP1_MAX_PACKET, 0x00,  /* wMaxPacketSize = 64 */
    0x00,                  /* bInterval (ignored for bulk) */

    /* EP2 OUT: Bulk (CDC data host→device) */
    7,                     /* bLength */
    USB_DTYPE_ENDPOINT,    /* bDescriptorType */
    0x02,                  /* bEndpointAddress: OUT EP2 */
    0x02,                  /* bmAttributes: Bulk */
    EP2_MAX_PACKET, 0x00,  /* wMaxPacketSize = 64 */
    0x00,                  /* bInterval (ignored for bulk) */
};

/*
 * String descriptors
 */
static const uint8_t usb_str_lang[] = {
    4,                     /* bLength */
    USB_DTYPE_STRING,      /* bDescriptorType */
    0x09, 0x04,            /* wLANGID[0] = 0x0409 (English US) */
};

static const uint8_t usb_str_manufacturer[] = {
    8,                     /* bLength = 2 + 2 * (num chars) */
    USB_DTYPE_STRING,      /* bDescriptorType */
    'A', 0,
    'P', 0,
    'M', 0,
};

static const uint8_t usb_str_product[] = {
    28,                    /* bLength = 2 + 2 * 13 */
    USB_DTYPE_STRING,      /* bDescriptorType */
    'C', 0,
    'U', 0,
    'A', 0,
    'V', 0,
    ' ', 0,
    'V', 0,
    '5', 0,
    ' ', 0,
    'C', 0,
    'D', 0,
    'C', 0,
    ' ', 0,
    '1', 0,
};

static const uint8_t usb_str_serial[] = {
    12,                    /* bLength = 2 + 2 * 5 */
    USB_DTYPE_STRING,      /* bDescriptorType */
    '0', 0,
    '0', 0,
    '0', 0,
    '0', 0,
    '1', 0,
};

/* ========================================================================== */
/* Internal state                                                             */
/* ========================================================================== */

/* EP0 control transfer state */
static struct {
    uint8_t  setup[8];         /* Current setup packet */
    uint8_t  ep0state;         /* EP0_STATE_* */
    const uint8_t *data_ptr;   /* Pointer to data for EP0 IN transfer */
    uint32_t data_len;         /* Total bytes to send/receive */
    uint32_t data_sent;        /* Bytes already sent in current transaction */
    uint32_t pkt_pending;      /* IN packets remaining */
    bool     zlp;              /* Need zero-length status IN? */
    uint8_t  stall_ep;         /* Which endpoint to stall */
    uint32_t ctrl_remaining;   /* Data stage remaining for multi-packet */
} _ep0;

/* CDC Serial State notification (for serial state changes via EP3 IN) */
static struct {
    volatile uint32_t serial_state; /* CDC SERIAL_STATE_* bits */
} _cdc;

/* User callbacks */
static usb_rx_callback_t _rx_cb = NULL;
static void *_rx_cb_arg = NULL;

/* DWC2 core state */
static struct {
    volatile bool initialized;
    volatile bool enumerated;
    volatile bool configured;
    volatile uint8_t device_addr;
    /* FIFO RAM allocator */
    uint32_t pmnext;
} _usb;

/* ========================================================================== */
/* Forward declarations                                                       */
/* ========================================================================== */

static void _otg_core_reset(void);
static void _otg_txfifo_flush(uint32_t fifo_num);
static void _otg_rxfifo_flush(void);
static void _otg_disable_endpoints(void);
static void _otg_ram_reset(void);
static uint32_t _otg_ram_alloc(uint32_t size_words);
static void _otg_fifo_write(volatile uint32_t *fifop, const uint8_t *buf, size_t n);
static void _otg_fifo_read(volatile uint32_t *fifop, uint8_t *buf, size_t n);
static void _usb_reset(void);
static void _ep0_handle_setup(void);
static int _ep0_handle_std_request(void);
static void _ep0_send_data(const uint8_t *data, uint32_t len);
static void _ep0_send_status(void);
static void _ep0_stall(void);
static void _ep0_out_term(bool success);
static void _ep0_in_term(bool success);
static void _ep0_setup_term(void);

/* ========================================================================== */
/* Internal: FIFO RAM allocator                                               */
/* ========================================================================== */

static void _otg_ram_reset(void)
{
    _usb.pmnext = RX_FIFO_SIZE_WORDS;
}

static uint32_t _otg_ram_alloc(uint32_t size_words)
{
    uint32_t addr = _usb.pmnext;
    _usb.pmnext += size_words;
    if (_usb.pmnext > OTG_FIFO_MEM_SIZE) {
        _usb.pmnext = OTG_FIFO_MEM_SIZE;
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
    while (n > 4) {
        uint32_t w;
        memcpy(&w, buf, 4);
        *fifop = w;
        n -= 4;
        buf += 4;
    }
    if (n > 0) {
        uint32_t w = 0;
        memcpy(&w, buf, n);
        *fifop = w;
    }
}

static void _otg_fifo_read(volatile uint32_t *fifop, uint8_t *buf, size_t n)
{
    size_t i = 0;
    uint32_t w = 0;
    while (i < n) {
        if ((i & 3) == 0) {
            w = *fifop;
        }
        if (buf && i < n) {
            *buf++ = (uint8_t)w;
            w >>= 8;
        }
        i++;
    }
}

/* ========================================================================== */
/* Internal: Core reset (ChibiOS: otg_core_reset)                             */
/* ========================================================================== */

static void _otg_core_reset(void)
{
    uint32_t timeout = 100000;
    while ((_OTG->GRSTCTL & GRSTCTL_AHBIDL) == 0) {
        if (--timeout == 0) break;
        __NOP();
    }
    _OTG->GRSTCTL = GRSTCTL_CSRST;
    (void)_OTG->GRSTCTL;
    { volatile uint32_t _d = 20; while (_d--) { __NOP(); } }
    timeout = 100000;
    while ((_OTG->GRSTCTL & GRSTCTL_CSRST) != 0) {
        if (--timeout == 0) break;
        __NOP();
    }
    { volatile uint32_t _d = 20; while (_d--) { __NOP(); } }
    while ((_OTG->GRSTCTL & GRSTCTL_AHBIDL) != 0) {}
}

/* ========================================================================== */
/* Internal: FIFO flush                                                       */
/* ========================================================================== */

static void _otg_txfifo_flush(uint32_t fifo_num)
{
    _OTG->GRSTCTL = GRSTCTL_TXFNUM(fifo_num) | GRSTCTL_TXFFLSH;
    while ((_OTG->GRSTCTL & GRSTCTL_TXFFLSH) != 0) {}
    { volatile uint32_t _d = 20; while (_d--) { __NOP(); } }
}

static void _otg_rxfifo_flush(void)
{
    _OTG->GRSTCTL = GRSTCTL_RXFFLSH;
    while ((_OTG->GRSTCTL & GRSTCTL_RXFFLSH) != 0) {}
    { volatile uint32_t _d = 20; while (_d--) { __NOP(); } }
}

/* ========================================================================== */
/* Internal: Disable all endpoints                                            */
/* ========================================================================== */

static void _otg_disable_endpoints(void)
{
    unsigned i;
    for (i = 0; i <= 4; i++) {
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
/* Internal: Reset state after USB bus reset                                  */
/* ========================================================================== */

static void _usb_reset(void)
{
    _otg_txfifo_flush(0);
    _DEV->DIEPEMPMSK = 0;
    _DEV->DAINTMSK = DAINTMSK_OEPM(0) | DAINTMSK_IEPM(0);

    for (unsigned i = 0; i <= 4; i++) {
        _IN_EP(i)->DIEPCTL = DIEPCTL_SNAK;
        _OUT_EP(i)->DOEPCTL = DIEPCTL_SNAK;
        _IN_EP(i)->DIEPINT = 0xFFFFFFFFU;
        _OUT_EP(i)->DOEPINT = 0xFFFFFFFFU;
    }

    _otg_ram_reset();

    _OTG->GRXFSIZ = RX_FIFO_SIZE_WORDS;
    _otg_rxfifo_flush();

    _DEV->DCFG = (_DEV->DCFG & ~DCFG_DAD_MASK) | DCFG_DAD(0);

    _OTG->GINTMSK |= GINTMSK_RXFLVLM | GINTMSK_OEPM | GINTMSK_IEPM;
    _DEV->DIEPMSK = DIEPMSK_XFRCM | DIEPMSK_TOM;
    _DEV->DOEPMSK = DOEPMSK_STUPM | DOEPMSK_XFRCM;

    /* ---- EP0 initialization ---- */
    _OUT_EP(0)->DOEPTSIZ = DOEPTSIZ_STUPCNT(3);
    _OUT_EP(0)->DOEPCTL = DIEPCTL_SD0PID | DIEPCTL_USBAEP |
                          DIEPCTL_EPTYP_CTRL | DIEPCTL_MPSIZ(EP0_MAX_PACKET);
    __DSB();

    _IN_EP(0)->DIEPTSIZ = 0;
    _IN_EP(0)->DIEPCTL = DIEPCTL_SD0PID | DIEPCTL_USBAEP |
                         DIEPCTL_EPTYP_CTRL |
                         DIEPCTL_TXFNUM(0) | DIEPCTL_MPSIZ(EP0_MAX_PACKET);
    __DSB();

    /* EP0 TX FIFO */
    _OTG->DIEPTXF0_HNPTXFSIZ =
        DIEPTXF_INEPTXFD(EP0_TX_FIFO_SIZE_WORDS) |
        DIEPTXF_INEPTXSA(_otg_ram_alloc(EP0_TX_FIFO_SIZE_WORDS));
    __DSB();

    /* EP1 TX FIFO (for CDC data IN) */
    _OTG->DIEPTXF[0] =
        DIEPTXF_INEPTXFD(EP1_TX_FIFO_SIZE_WORDS) |
        DIEPTXF_INEPTXSA(_otg_ram_alloc(EP1_TX_FIFO_SIZE_WORDS));
    _otg_txfifo_flush(1);

    /* EP1: bulk IN endpoint (CDC data device→host) */
    _IN_EP(1)->DIEPTSIZ = 0;
    _IN_EP(1)->DIEPCTL = DIEPCTL_SD0PID | DIEPCTL_USBAEP |
                         DIEPCTL_EPTYP_BULK |
                         DIEPCTL_TXFNUM(1) | DIEPCTL_MPSIZ(EP1_MAX_PACKET);
    _DEV->DAINTMSK |= DAINTMSK_IEPM(1);
    __DSB();

    /* EP2: bulk OUT endpoint (CDC data host→device) */
    _IN_EP(2)->DIEPTSIZ = 0;
    _OUT_EP(2)->DOEPTSIZ = DOEPTSIZ_STUPCNT(3) | DOEPTSIZ_PKTCNT(1) |
                           DOEPTSIZ_XFRSIZ(EP2_MAX_PACKET);
    _OUT_EP(2)->DOEPCTL = DIEPCTL_SD0PID | DIEPCTL_USBAEP |
                          DIEPCTL_EPTYP_BULK | DIEPCTL_MPSIZ(EP2_MAX_PACKET);
    _DEV->DAINTMSK |= DAINTMSK_OEPM(2);
    __DSB();

    /* EP3: interrupt IN endpoint (CDC notification) */
    _IN_EP(3)->DIEPTSIZ = 0;
    _IN_EP(3)->DIEPCTL = DIEPCTL_SD0PID | DIEPCTL_USBAEP |
                         DIEPCTL_EPTYP_INTR |
                         DIEPCTL_TXFNUM(3) | DIEPCTL_MPSIZ(8);
    _DEV->DAINTMSK |= DAINTMSK_IEPM(3);
    __DSB();

    /* Enable EP2 OUT */
    _OUT_EP(2)->DOEPCTL |= DIEPCTL_EPENA | DIEPCTL_CNAK;
    __DSB();

    /* Init EP0 state */
    _ep0.ep0state = EP0_STATE_IDLE;
    _usb.enumerated = false;
    _usb.device_addr = 0;
    _usb.configured = false;
}

/* ========================================================================== */
/* Internal: EP0 send data (for EP0 IN data stage)                           */
/* ========================================================================== */

static void _ep0_send_data(const uint8_t *data, uint32_t len)
{
    uint32_t pkt_size = (len > EP0_MAX_PACKET) ? EP0_MAX_PACKET : len;
    uint32_t pcnt = (pkt_size > 0) ? 1 : 0;

    /* If ZLP needed, send one */
    if (pkt_size == 0 && len == 0) {
        pcnt = 1;
    }

    /* Write data to EP0 TX FIFO */
    if (pkt_size > 0) {
        uint32_t fifo_avail = _IN_EP(0)->DTXFSTS & DTXFSTS_INEPTFSAV_MASK;
        uint32_t needed = (pkt_size + 3) / 4;
        if (fifo_avail < needed) {
            /* Will need to poll until space — but for EP0 this is rare */
            return;
        }
        _otg_fifo_write(&_FIFO(0), data, pkt_size);
    }

    /* Set TSIZ */
    _IN_EP(0)->DIEPTSIZ = DIEPTSIZ_MCNT(1) |
                          DIEPTSIZ_PKTCNT(pcnt) |
                          DIEPTSIZ_XFRSIZ(pkt_size);
    __DSB();

    /* Enable IN EP0 */
    _IN_EP(0)->DIEPCTL |= DIEPCTL_EPENA | DIEPCTL_CNAK;
    __DSB();
}

/*
 * Send EP0 status stage (zero-length IN packet)
 */
static void _ep0_send_status(void)
{
    _ep0_send_data(NULL, 0);
}

/*
 * Stall EP0
 */
static void _ep0_stall(void)
{
    _IN_EP(0)->DIEPCTL |= DIEPCTL_STALL;
    _OUT_EP(0)->DOEPCTL |= DIEPCTL_STALL;
    _ep0.ep0state = EP0_STATE_STALL;
}

/* ========================================================================== */
/* Internal: Handle USB standard requests                                     */
/* ========================================================================== */

static int _ep0_handle_std_request(void)
{
    uint8_t bmReqType = _ep0.setup[0];
    uint8_t bRequest   = _ep0.setup[1];
    uint16_t wValue   = _ep0.setup[2] | ((uint16_t)_ep0.setup[3] << 8);
    uint16_t wIndex   = _ep0.setup[4] | ((uint16_t)_ep0.setup[5] << 8);
    uint16_t wLength  = _ep0.setup[6] | ((uint16_t)_ep0.setup[7] << 8);

    /* Standard requests only (bmReqType bits 5:6 = 00) */
    uint8_t req_type = bmReqType & 0x60;
    if (req_type != 0x00) {
        return -1; /* Not standard — pass to class handler */
    }

    switch (bRequest) {

    case USB_REQ_GET_STATUS:
    {
        /* Return 2-byte status */
        static const uint8_t zero_status[2] = {0, 0};
        _ep0_send_data(zero_status, 2);
        _ep0.ep0state = EP0_STATE_DATA_IN;
        _ep0.data_ptr = zero_status;
        _ep0.data_len = 2;
        _ep0.data_sent = 0;
        return 0;
    }

    case USB_REQ_CLEAR_FEATURE:
    {
        uint8_t ep = (uint8_t)(wIndex & 0x7F);
        uint8_t dir = (uint8_t)(wIndex >> 7);
        if (wValue == 0) { /* ENDPOINT_HALT */
            if (dir) {
                _IN_EP(ep)->DIEPCTL &= ~DIEPCTL_STALL;
            } else {
                _OUT_EP(ep)->DOEPCTL &= ~DIEPCTL_STALL;
            }
        }
        _ep0_send_status();
        _ep0.ep0state = EP0_STATE_STATUS_IN;
        return 0;
    }

    case USB_REQ_SET_FEATURE:
    {
        if (wValue == 0) { /* ENDPOINT_HALT */
            uint8_t ep = (uint8_t)(wIndex & 0x7F);
            uint8_t dir = (uint8_t)(wIndex >> 7);
            if (dir) {
                _IN_EP(ep)->DIEPCTL |= DIEPCTL_STALL;
            } else if (ep > 0) {
                _OUT_EP(ep)->DOEPCTL |= DIEPCTL_STALL;
            }
        } else if (wValue == 1) { /* TEST_MODE — just skip, not implemented */
            /* Skip */
        }
        _ep0_send_status();
        _ep0.ep0state = EP0_STATE_STATUS_IN;
        return 0;
    }

    case USB_REQ_SET_ADDRESS:
    {
        /* Per spec, address is set after status stage completes.
         * We store it now and apply in _ep0_in_term when status completes.
         * The USB spec says: device must respond with status IN (ZLP) at
         * address 0, then switch to new address. */
        _usb.device_addr = (uint8_t)(wValue & 0x7F);
        _ep0_send_status();
        _ep0.ep0state = EP0_STATE_STATUS_IN;
        return 0;
    }

    case USB_REQ_GET_DESCRIPTOR:
    {
        uint8_t dtype = (uint8_t)(wValue >> 8);
        uint8_t dindex = (uint8_t)(wValue & 0xFF);
        const uint8_t *desc = NULL;
        uint32_t dlen = 0;

        switch (dtype) {

        case USB_DTYPE_DEVICE:
            desc = usb_dev_desc;
            dlen = (uint32_t)usb_dev_desc[0];
            break;

        case USB_DTYPE_CONFIG:
            desc = usb_cfg_desc;
            dlen = (uint32_t)usb_cfg_desc[2] |
                   ((uint32_t)usb_cfg_desc[3] << 8);
            break;

        case USB_DTYPE_STRING:
            switch (dindex) {
            case 0:  desc = usb_str_lang;       dlen = usb_str_lang[0]; break;
            case 1:  desc = usb_str_manufacturer; dlen = usb_str_manufacturer[0]; break;
            case 2:  desc = usb_str_product;     dlen = usb_str_product[0]; break;
            case 3:  desc = usb_str_serial;      dlen = usb_str_serial[0]; break;
            default: /* Unsupported string index */
                _ep0_stall();
                return 0;
            }
            break;

        case USB_DTYPE_DEVICE_QUALIFIER:
        case USB_DTYPE_OTHER_SPEED:
            /* Device qualifier: request error (stall), not required for FS */
            _ep0_stall();
            return 0;

        default:
            _ep0_stall();
            return 0;
        }

        if (desc == NULL) {
            _ep0_stall();
            return 0;
        }

        /* Clamp length to wLength */
        uint32_t send_len = dlen;
        if (wLength < send_len) {
            send_len = wLength;
        }

        _ep0_send_data(desc, send_len);
        _ep0.ep0state = EP0_STATE_DATA_IN;
        _ep0.data_ptr = desc;
        _ep0.data_len = dlen;
        _ep0.data_sent = send_len;
        return 0;
    }

    case USB_REQ_SET_CONFIGURATION:
    {
        uint8_t cfg = (uint8_t)(wValue & 0xFF);
        if (cfg == 0) {
            _usb.configured = false;
            /* Re-arm EP2 */
        } else if (cfg == 1) {
            _usb.configured = true;
            /* Enable EP2 OUT for reception */
            _OUT_EP(2)->DOEPTSIZ = DOEPTSIZ_STUPCNT(3) |
                                   DOEPTSIZ_PKTCNT(1) |
                                   DOEPTSIZ_XFRSIZ(EP2_MAX_PACKET);
            _OUT_EP(2)->DOEPCTL |= DIEPCTL_EPENA | DIEPCTL_CNAK;
            __DSB();
        } else {
            _ep0_stall();
            return 0;
        }
        _cdc.serial_state = 0x00000000; /* Clear serial state */
        _ep0_send_status();
        _ep0.ep0state = EP0_STATE_STATUS_IN;
        return 0;
    }

    case USB_REQ_GET_CONFIGURATION:
    {
        uint8_t cfg_val = _usb.configured ? 1 : 0;
        _ep0_send_data(&cfg_val, 1);
        _ep0.ep0state = EP0_STATE_DATA_IN;
        return 0;
    }

    case USB_REQ_GET_INTERFACE:
    {
        uint8_t alt = 0;
        _ep0_send_data(&alt, 1);
        _ep0.ep0state = EP0_STATE_DATA_IN;
        return 0;
    }

    case USB_REQ_SET_INTERFACE:
    {
        _ep0_send_status();
        _ep0.ep0state = EP0_STATE_STATUS_IN;
        return 0;
    }

    case USB_REQ_SET_DESCRIPTOR:
    default:
        _ep0_stall();
        return 0;
    }
}

/* ========================================================================== */
/* Internal: Handle setup packet from RX FIFO                                */
/* ========================================================================== */

static void _ep0_handle_setup(void)
{
    /* Read setup packet from the internal buffer (already copied from RX FIFO)
     * The setup packet is already in _ep0.setup[8] from otg_rxfifo_handler */
    uint8_t bmReqType = _ep0.setup[0];
    uint16_t wLength  = _ep0.setup[6] | ((uint16_t)_ep0.setup[7] << 8);

    /* Clear any previous state */
    _ep0.ep0state = EP0_STATE_IDLE;
    _ep0.data_ptr = NULL;
    _ep0.data_len = 0;
    _ep0.data_sent = 0;

    /* If there's a pending setup on EP0, handle it */
    int handled = _ep0_handle_std_request();

    if (handled != 0) {
        /* Not a standard request (or class request not yet handled) */
        _ep0_stall();
    }
}

/* ========================================================================== */
/* Internal: RX FIFO handler                                                  */
/* ========================================================================== */

static void _otg_rxfifo_handler(void)
{
    /* Pop all entries from RX FIFO */
    while ((_OTG->GINTSTS & GINTMSK_RXFLVLM) != 0) {
        uint32_t sts = _OTG->GRXSTSP;
        (void)_OTG->GRXSTSP; /* Consume */

        uint32_t cnt  = (sts & GRXSTSP_BCNT_MASK) >> GRXSTSP_BCNT_SHIFT;
        uint32_t ep   = (sts & GRXSTSP_EPNUM_MASK) >> GRXSTSP_EPNUM_SHIFT;
        uint32_t pkt  = (sts & GRXSTSP_PKTSTS_MASK) >> GRXSTSP_PKTSTS_SHIFT;

        switch (pkt) {
        case GRXSTSP_SETUP_DATA: {
            /* Read 8-byte setup packet from RX FIFO */
            _otg_fifo_read(&_FIFO(0), _ep0.setup, 8);
            /* Track what we just received */
            _ep0_setup_term();
            break;
        }
        case GRXSTSP_SETUP_COMP: {
            /* Setup completed — handle the request now */
            _ep0_handle_setup();
            break;
        }
        case GRXSTSP_OUT_DATA: {
            if (ep == 2) {
                /* CDC data from host — read into bounce buffer */
                if (_rx_cb != NULL) {
                    uint8_t buf[EP2_MAX_PACKET];
                    _otg_fifo_read(&_FIFO(0), buf, (cnt > sizeof(buf)) ? sizeof(buf) : cnt);
                    _rx_cb(buf, (cnt > EP2_MAX_PACKET) ? EP2_MAX_PACKET : cnt, _rx_cb_arg);
                } else {
                    /* No callback — discard */
                    _otg_fifo_read(&_FIFO(0), NULL, cnt);
                }
            } else {
                /* Unknown EP — discard */
                _otg_fifo_read(&_FIFO(0), NULL, cnt);
            }
            break;
        }
        case GRXSTSP_OUT_COMP: {
            /* OUT transfer complete on EP2 — re-arm for next packet */
            if (ep == 2 && _usb.configured) {
                _OUT_EP(2)->DOEPTSIZ = DOEPTSIZ_STUPCNT(3) |
                                       DOEPTSIZ_PKTCNT(1) |
                                       DOEPTSIZ_XFRSIZ(EP2_MAX_PACKET);
                _OUT_EP(2)->DOEPCTL |= DIEPCTL_EPENA | DIEPCTL_CNAK;
                __DSB();
            }
            break;
        }
        default:
            break;
        }
    }
}

/* ========================================================================== */
/* Internal: EP0 control transfer completions                                 */
/* ========================================================================== */

static void _ep0_setup_term(void)
{
    /* Setup packet received — the RX FIFO handler called this.
     * We defer actual handling to SETUP_COMP completion. */
}

static void _ep0_in_term(bool success)
{
    if (!success) {
        _ep0.ep0state = EP0_STATE_IDLE;
        return;
    }

    switch (_ep0.ep0state) {
    case EP0_STATE_DATA_IN: {
        /* Data stage completed.
         * Check if we need to send more data (multi-packet). */
        if (_ep0.data_sent < _ep0.data_len) {
            uint32_t remaining = _ep0.data_len - _ep0.data_sent;
            uint32_t chunk = (remaining > EP0_MAX_PACKET) ? EP0_MAX_PACKET : remaining;
            _ep0_send_data(_ep0.data_ptr + _ep0.data_sent, chunk);
            _ep0.data_sent += chunk;
        } else {
            /* All data sent — transition to status OUT */
            // Status OUT will be handled by host — just wait
            _ep0.ep0state = EP0_STATE_STATUS_OUT;
        }
        break;
    }
    case EP0_STATE_STATUS_IN: {
        /* Status stage completed for SET_ADDRESS — apply address now */
        if (_usb.device_addr > 0) {
            _DEV->DCFG = (_DEV->DCFG & ~DCFG_DAD_MASK) |
                         DCFG_DAD(_usb.device_addr);
            __DSB();
        }
        _ep0.ep0state = EP0_STATE_IDLE;
        break;
    }
    default:
        _ep0.ep0state = EP0_STATE_IDLE;
        break;
    }
}

static void _ep0_out_term(bool success)
{
    (void)success;
    /* OUT transfer complete on EP0 = status OUT from host.
     * This happens during control write transfers (host sends data,
     * device receives, then host sends ZLP status). */
    _ep0.ep0state = EP0_STATE_IDLE;
}

/* ========================================================================== */
/* Internal: Endpoint IN handler (DIEPINT)                                   */
/* ========================================================================== */

static void _otg_epin_handler(uint32_t ep)
{
    uint32_t epint = _IN_EP(ep)->DIEPINT;
    _IN_EP(ep)->DIEPINT = epint;

    if (epint & DIEPINT_TOC) {
        /* Timeout — clear and disable */
        _IN_EP(ep)->DIEPCTL |= DIEPCTL_EPDIS;
    }

    if (epint & DIEPINT_XFRC) {
        /* Transfer complete */
        if (ep == 0) {
            _ep0_in_term(true);
        }
        /* EP1 (CDC data IN) is handled via usb_lld_send_rtt polling */
        if (ep == 3) {
            /* EP3 notification — nothing special needed */
        }
    }
}

/* ========================================================================== */
/* Internal: Endpoint OUT handler (DOEPINT)                                  */
/* ========================================================================== */

static void _otg_epout_handler(uint32_t ep)
{
    uint32_t epint = _OUT_EP(ep)->DOEPINT;
    _OUT_EP(ep)->DOEPINT = epint;

    if (epint & DOEPINT_STUP) {
        /* Setup packet received on EP0.
         * The RX FIFO handler already read it; no need to read again.
         * But the STUP interrupt signals the host completed the setup. */
        /* Some cores fire STUP before RXFLVL — we need to handle this.
         * The setup packet is already in _ep0.setup[], so just call handler. */
    }

    if (epint & DOEPINT_XFRC) {
        /* OUT transfer complete */
        if (ep == 0) {
            _ep0_out_term(true);
        }
        if (ep == 2) {
            /* CDC OUT complete — re-arm */
            if (_usb.configured) {
                _OUT_EP(2)->DOEPTSIZ = DOEPTSIZ_STUPCNT(3) |
                                       DOEPTSIZ_PKTCNT(1) |
                                       DOEPTSIZ_XFRSIZ(EP2_MAX_PACKET);
                _OUT_EP(2)->DOEPCTL |= DIEPCTL_EPENA | DIEPCTL_CNAK;
                __DSB();
            }
        }
    }
}

/* ========================================================================== */
/* Exported: usb_lld_init_rtt                                                 */
/* ========================================================================== */

bool usb_lld_init_rtt(void)
{
    /* DEBUG: magic 0xDEAD0001 = function entered */
    *(volatile uint32_t *)0x2001fff0 = 0xDEAD0001;
    if (_usb.initialized) {
        return true;
    }

    /* ---- Step 0: Ensure VTOR points to firmware vector table ---- */
    /* The bootloader may overwrite VTOR during interrupts. Re-set to
     * the firmware's vector table base to guarantee correct dispatch
     * for all exception vectors (SysTick, PendSV, USB, etc.). */
    extern uint32_t g_pfnVectors[];
    SCB->VTOR = (uint32_t)g_pfnVectors;
    __DSB();
    __ISB();

    /* ---- Step 1: Enable OTG_FS clock and reset ---- */
    RCC->AHB2ENR |= RCC_AHB2ENR_OTGFSEN;
    (void)RCC->AHB2ENR;
    __DSB();

    RCC->AHB2RSTR |= RCC_AHB2RSTR_OTGFSRST;
    __DSB();
    RCC->AHB2RSTR &= ~RCC_AHB2RSTR_OTGFSRST;
    __DSB();

    /* ---- Step 1b: GPIO AF10 configuration (bypassed HAL_PCD_MspInit) ---- */
    /* PA11=OTG_FS_DM, PA12=OTG_FS_DP, PA9=OTG_FS_VBUS */
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;
    (void)RCC->AHB1ENR;
    __DSB();

    /* PA11 AF10 (OTG_FS_DM) */
    GPIOA->MODER = (GPIOA->MODER & ~(3U << 22)) | (2U << 22);
    GPIOA->AFR[1] = (GPIOA->AFR[1] & ~(0xFU << 12)) | (10U << 12);
    GPIOA->OSPEEDR |= (3U << 22);  /* Very High */
    GPIOA->PUPDR &= ~(3U << 22);   /* No pull */

    /* PA12 AF10 (OTG_FS_DP) */
    GPIOA->MODER = (GPIOA->MODER & ~(3U << 24)) | (2U << 24);
    GPIOA->AFR[1] = (GPIOA->AFR[1] & ~(0xFU << 16)) | (10U << 16);
    GPIOA->OSPEEDR |= (3U << 24);  /* Very High */
    GPIOA->PUPDR &= ~(3U << 24);   /* No pull */

    /* PA9 OTG_FS_VBUS (input) */
    GPIOA->MODER &= ~(3U << 18);  /* Input */

    /* ---- Step 2: GUSBCFG — forced device mode, FS 1.1 PHY ---- */
    _OTG->GUSBCFG = GUSBCFG_FDMOD | GUSBCFG_TRDT(TRDT_VALUE_FS) |
                    GUSBCFG_PHYSEL;
    (void)_OTG->GUSBCFG;
    __DSB();

    /* ---- Step 3: DCFG — FS 1.1 PHY, 48MHz ---- */
    _DEV->DCFG = 0x02200000UL | DCFG_DSPD_FS11;
    __DSB();

    /* ---- Step 4: PCGCCTL — enable PHY ---- */
    _PCGCCTL = 0;
    __DSB();

    /* ---- Step 5: Core reset ---- */
    _otg_core_reset();

    /* ---- Step 5b: Re-program GUSBCFG after core reset ---- */
    /* Core soft reset (GRSTCTL_CSRST) resets GUSBCFG back to default
     * (0x00001440), losing FDMOD and PHYSEL. These bits MUST be re-set
     * for proper internal FS PHY operation. (2026-05-26 debug) */
    _OTG->GUSBCFG = GUSBCFG_FDMOD | GUSBCFG_TRDT(TRDT_VALUE_FS) |
                    GUSBCFG_PHYSEL;
    (void)_OTG->GUSBCFG;
    __DSB();

    /* ---- Step 6: Soft disconnect → reconnect cycle ---- */
    /* Force D+ low so the host detects a clean disconnect. Without this
     * cycle the host may never see the device after bootloader hand-off. */
    _DEV->DCTL = DCTL_SDIS;                    /* pull D+ low */
    __DSB();
    { volatile uint32_t _d = 50000; while (_d--) { __NOP(); } }
    _DEV->DCTL = 0;                            /* release D+ pull-up */
    __DSB();
    { volatile uint32_t _d = 50000; while (_d--) { __NOP(); } }

    /* ---- Step 7: VBUS sensing + transceiver (AFTER core reset) ---- */
    /* GCCFG = VBDEN | VBUSBSEN (NOT PWRDWN! PWRDWN powers down the PHY). */
    _OTG->GOTGCTL = GOTGCTL_BVALOEN | GOTGCTL_BVALOVAL;
    _OTG->GCCFG = GCCFG_VBDEN | GCCFG_VBUSBSEN;
    __DSB();

    /* ---- Step 8: GAHBCFG — no DMA, no global int yet ---- */
    _OTG->GAHBCFG = 0;
    __DSB();

    /* ---- Step 8: Disable endpoints + clear pending ---- */
    _otg_disable_endpoints();
    _DEV->DIEPMSK = 0;
    _DEV->DOEPMSK = 0;
    _DEV->DAINTMSK = 0;

    /* Set GINTMSK */
    _OTG->GINTMSK = GINTMSK_ENUMDNEM | GINTMSK_USBRSTM |
                    GINTMSK_USBSUSPM | GINTMSK_ESUSPM |
                    GINTMSK_SRQM | GINTMSK_WKUPM |
                    GINTMSK_IISOIXFRM | GINTMSK_IISOOXFRM;

    /* Clear all pending interrupts */
    _OTG->GINTSTS = 0xFFFFFFFFU;
    (void)_OTG->GINTSTS;

    /* ---- Step 9: Enable global interrupt ---- */
    _OTG->GAHBCFG |= GAHBCFG_GINTMSK;
    __DSB();

    _usb.initialized = true;
    /* DEBUG: magic 0xDEAD0002 = function completed */
    *(volatile uint32_t *)0x2001fff0 = 0xDEAD0002;
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

    /* ---- USB Reset ---- */
    if (sts & GINTSTS_USBRST) {
        _usb_reset();
        return;
    }

    /* ---- Wakeup ---- */
    if (sts & GINTSTS_WKUPINT) {
        if (_PCGCCTL & (PCGCCTL_STPPCLK | PCGCCTL_GATEHCLK)) {
            _PCGCCTL &= ~(PCGCCTL_STPPCLK | PCGCCTL_GATEHCLK);
        }
        _DEV->DCTL &= ~DCTL_RWUSIG;
    }

    /* ---- Suspend ---- */
    if (sts & GINTSTS_USBSUSP) {
        _otg_disable_endpoints();
    }

    /* ---- Enumeration done ---- */
    if (sts & GINTSTS_ENUMDNE) {
        uint32_t spd = _DEV->DSTS & DSTS_ENUMSPD_MASK;
        if (spd == DSTS_ENUMSPD_HS) {
            _OTG->GUSBCFG = (_OTG->GUSBCFG & ~GUSBCFG_TRDT_MASK) |
                            GUSBCFG_TRDT(9);
        } else {
            _OTG->GUSBCFG = (_OTG->GUSBCFG & ~GUSBCFG_TRDT_MASK) |
                            GUSBCFG_TRDT(TRDT_VALUE_FS);
        }
        _usb.enumerated = true;
    }

    /* ---- SOF ---- */
    if (sts & GINTSTS_SOF) {
        /* Not used for polling mode */
    }

    /* ---- Iso IN/OUT failed ---- */
    if (sts & GINTSTS_IISOIXFR) { /* Iso IN failed */ }
    if (sts & GINTSTS_IISOOXFR) { /* Iso OUT failed */ }

    /* ---- RX FIFO data available ---- */
    if (sts & GINTSTS_RXFLVL) {
        _otg_rxfifo_handler();
    }

    /* ---- IN endpoint interrupts ---- */
    if (sts & GINTSTS_IEPINT) {
        uint32_t daint = _DEV->DAINT;
        /* Check EP0 IN */
        if (daint & (1 << 0)) {
            _otg_epin_handler(0);
        }
        /* Check EP1 IN (CDC data TX — normally completion handled by usb_lld_send_rtt polling,
         * but we check here too in case of interrupt-style completion) */
        if (daint & (1 << 1)) {
            uint32_t epint = _IN_EP(1)->DIEPINT;
            if (epint & DIEPINT_XFRC) {
                _IN_EP(1)->DIEPINT = DIEPINT_XFRC;
            } else {
                _IN_EP(1)->DIEPINT = epint;
            }
        }
        /* Check EP3 IN (CDC notification) */
        if (daint & (1 << 3)) {
            _otg_epin_handler(3);
        }
    }

    /* ---- OUT endpoint interrupts ---- */
    if (sts & GINTSTS_OEPINT) {
        uint32_t daint = _DEV->DAINT;
        /* OEP 0 */
        if (daint & (1 << 16)) {
            _otg_epout_handler(0);
        }
        /* OEP 2 (CDC data RX) */
        if (daint & (1 << 18)) {
            _otg_epout_handler(2);
        }
    }
}

/* ========================================================================== */
/* Exported: usb_lld_send_rtt                                                 */
/* ========================================================================== */

bool usb_lld_send_rtt(uint8_t ep, const uint8_t *data, uint32_t len)
{
    if (!_usb.initialized || ep > 3 || ep == 0) {
        return false;
    }

    uint32_t max_pkt;
    switch (ep) {
    case 1: max_pkt = EP1_MAX_PACKET; break;
    case 3: max_pkt = 8; break;
    default: return false;
    }

    if (len > max_pkt) {
        len = max_pkt;
    }

    /* Check TX FIFO space */
    uint32_t fifo_avail = _IN_EP(ep)->DTXFSTS & DTXFSTS_INEPTFSAV_MASK;
    uint32_t needed = (len + 3) / 4;
    if (fifo_avail < needed) {
        return false;
    }

    /* Write data to FIFO */
    _otg_fifo_write(&_FIFO(ep), data, len);

    /* Set DIEPTSIZ */
    uint32_t pcnt = (len > 0) ? 1 : 0;
    _IN_EP(ep)->DIEPTSIZ = DIEPTSIZ_MCNT(1) |
                           DIEPTSIZ_PKTCNT(pcnt) |
                           DIEPTSIZ_XFRSIZ(len);
    __DSB();

    /* Enable endpoint: EPENA | CNAK */
    _IN_EP(ep)->DIEPCTL |= DIEPCTL_EPENA | DIEPCTL_CNAK;
    __DSB();

    /* Poll DIEPINT for XFRC */
    uint32_t timeout = SEND_TIMEOUT;
    while (!(_IN_EP(ep)->DIEPINT & DIEPINT_XFRC)) {
        if (--timeout == 0) {
            _IN_EP(ep)->DIEPCTL |= DIEPCTL_EPDIS;
            _IN_EP(ep)->DIEPINT = 0xFFFFFFFFU;
            return false;
        }
        __NOP();
    }

    /* Clear XFRC */
    _IN_EP(ep)->DIEPINT = DIEPINT_XFRC;

    return true;
}

/* ========================================================================== */
/* Exported: usb_lld_send_cdc_notification                                    */
/* ========================================================================== */

bool usb_lld_send_cdc_notification(uint16_t serial_state)
{
    /* CDC Notification header (8 bytes) + 2 bytes serial state = 10 bytes */
    uint8_t notify[10];
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

/* ========================================================================== */
/* Exported: usb_lld_set_address_rtt                                          */
/* ========================================================================== */

void usb_lld_set_address_rtt(uint8_t addr)
{
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

/* ========================================================================== */
/* Exported: usb_lld_is_configured_rtt                                       */
/* ========================================================================== */

bool usb_lld_is_configured_rtt(void)
{
    return _usb.configured;
}

/* ========================================================================== */
/* Exported: usb_lld_set_rx_callback                                          */
/* ========================================================================== */

void usb_lld_set_rx_callback(usb_rx_callback_t cb, void *arg)
{
    _rx_cb = cb;
    _rx_cb_arg = arg;
}

/* ========================================================================== */
/* Exported: usb_lld_send_cdc_rx_ready                                        */
/* ========================================================================== */

void usb_lld_rearm_cdc_out(void)
{
    /* Re-arm EP2 OUT for next CDC data packet from host */
    if (_usb.configured) {
        /* Check if endpoint is already enabled and busy */
        if ((_OUT_EP(2)->DOEPCTL & DIEPCTL_EPENA) == 0) {
            _OUT_EP(2)->DOEPTSIZ = DOEPTSIZ_STUPCNT(3) |
                                   DOEPTSIZ_PKTCNT(1) |
                                   DOEPTSIZ_XFRSIZ(EP2_MAX_PACKET);
            _OUT_EP(2)->DOEPCTL |= DIEPCTL_EPENA | DIEPCTL_CNAK;
            __DSB();
        }
    }
}
