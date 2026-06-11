/*
 * RTT USB CDC ACM class layer — Implementation
 *
 * Provides CDC-ACM descriptors, class request handling, and
 * dual CDC ACM management on top of the DWC2 low-level driver.
 *
 * Extracted from hal_usb_lld_rtt.c (the monolithic first port) into
 * a separate layer per the porting plan.
 *
 * This file calls LLD functions (usb_lld_start_in, usb_lld_start_out,
 * usb_lld_init_endpoint, etc.) via forward declarations.  The HAL
 * state machine callbacks (event_cb, requests_hook_cb) are registered
 * in the rtt_usb driver's RTT_USBConfig by usb_cdc_init().
 *
 * ChibiOS Copyright (C) 2006..2018 Giovanni Di Sirio
 *   Licensed under the Apache License, Version 2.0
 */

#include "usb_cdc_rtt.h"
#include "hal_usb_lld_rtt.h"
#include <string.h>
#include <rtthread.h>

/* ========================================================================== */
/* Forward declarations of LLD functions                                      */
/* ========================================================================== */

extern void usb_lld_init_endpoint(void *usbp, usbep_t ep);
extern void usb_lld_start_in(void *usbp, usbep_t ep);
extern void usb_lld_start_out(void *usbp, usbep_t ep);
extern void usb_lld_stall_in(void *usbp, usbep_t ep);
extern void usb_lld_stall_out(void *usbp, usbep_t ep);

/* ========================================================================== */
/* Constants                                                                  */
/* ========================================================================== */

#define CDC_PORT_COUNT          2U
#define EP0_MAX_PACKET          64
#define CDC_DATA_MAX_PACKET     64
#define CDC_NOTIFY_MAX_PACKET   8

#define CDC0_DATA_IN_EP         1
#define CDC0_DATA_OUT_EP        2
#define CDC0_NOTIFY_IN_EP       3
#define CDC1_DATA_IN_EP         4
#define CDC1_DATA_OUT_EP        4
#define CDC1_NOTIFY_IN_EP       5

#define CDC_DESC_SET_LEN        66
#define CDC_CFG_DESC_LEN        (9 + CDC_DESC_SET_LEN * CDC_PORT_COUNT)

/* ========================================================================== */
/* USB descriptors (CDC ACM with IAD)                                         */
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
    /* ---- Configuration descriptor ---- */
    9,                     /* bLength */
    2,                     /* bDescriptorType = CONFIGURATION */
    (uint8_t)(CDC_CFG_DESC_LEN & 0xFF), (uint8_t)(CDC_CFG_DESC_LEN >> 8),
    4,                     /* bNumInterfaces */
    1,                     /* bConfigurationValue */
    0,                     /* iConfiguration */
    0xC0,                  /* bmAttributes: Self-powered */
    50,                    /* bMaxPower = 100 mA */

    /* ---- CDC0 IAD (Interface Association Descriptor) ---- */
    8,                     /* bLength */
    0x0B,                  /* bDescriptorType = IAD */
    0,                     /* bFirstInterface */
    2,                     /* bInterfaceCount */
    2,                     /* bFunctionClass = CDC Comm */
    2,                     /* bFunctionSubClass = ACM */
    1,                     /* bFunctionProtocol = AT */
    0,                     /* iFunction */

    /* ---- CDC0 Interface 0: CDC Communication ---- */
    9, 4,                  /* bLength, bDescriptorType = INTERFACE */
    0, 0,                  /* bInterfaceNumber, bAlternateSetting */
    1,                     /* bNumEndpoints */
    2, 2, 1,               /* bInterfaceClass/SubClass/Protocol */
    0,                     /* iInterface */

    /* CDC Header Functional Descriptor */
    5, 0x24, 0x00,         /* bLength, CS_INTERFACE, HEADER */
    0x10, 0x01,            /* bcdCDC = 1.10 */

    /* CDC Call Management Functional Descriptor */
    5, 0x24, 0x01,         /* bLength, CS_INTERFACE, CALL_MGMT */
    0x01,                  /* bmCapabilities */
    1,                     /* bDataInterface */

    /* CDC ACM Functional Descriptor */
    4, 0x24, 0x02,         /* bLength, CS_INTERFACE, ACM */
    0x02,                  /* bmCapabilities */

    /* CDC Union Functional Descriptor */
    5, 0x24, 0x06,         /* bLength, CS_INTERFACE, UNION */
    0,                     /* bMasterInterface */
    1,                     /* bSlaveInterface */

    /* EP3 IN: Interrupt (CDC0 notifications) */
    7, 5,                  /* bLength, bDescriptorType = ENDPOINT */
    0x83,                  /* bEndpointAddress: IN EP3 */
    0x03,                  /* bmAttributes: Interrupt */
    CDC_NOTIFY_MAX_PACKET, 0x00,
    0x10,                  /* bInterval = 16 ms */

    /* ---- CDC0 Interface 1: CDC Data ---- */
    9, 4,
    1, 0,                  /* bInterfaceNumber, bAlternateSetting */
    2,                     /* bNumEndpoints */
    0x0A, 0x00, 0x00,      /* bInterfaceClass/SubClass/Protocol = CDC Data */
    0,                     /* iInterface */

    /* EP1 IN: Bulk (CDC data device -> host) */
    7, 5,
    0x81,                  /* bEndpointAddress: IN EP1 */
    0x02,                  /* bmAttributes: Bulk */
    CDC_DATA_MAX_PACKET, 0x00,
    0x00,                  /* bInterval */

    /* EP2 OUT: Bulk (CDC data host -> device) */
    7, 5,
    0x02,                  /* bEndpointAddress: OUT EP2 */
    0x02,                  /* bmAttributes: Bulk */
    CDC_DATA_MAX_PACKET, 0x00,
    0x00,                  /* bInterval */

    /* ---- CDC1 IAD ---- */
    8, 0x0B,
    2,                     /* bFirstInterface */
    2,                     /* bInterfaceCount */
    2, 2, 1,
    0,

    /* ---- CDC1 Interface 2: CDC Communication ---- */
    9, 4,
    2, 0,
    1,
    2, 2, 1,
    0,

    /* CDC Header Functional Descriptor */
    5, 0x24, 0x00,
    0x10, 0x01,

    /* CDC Call Management Functional Descriptor */
    5, 0x24, 0x01,
    0x01,
    3,                     /* bDataInterface */

    /* CDC ACM Functional Descriptor */
    4, 0x24, 0x02,
    0x02,

    /* CDC Union Functional Descriptor */
    5, 0x24, 0x06,
    2,                     /* bMasterInterface */
    3,                     /* bSlaveInterface */

    /* EP5 IN: Interrupt (CDC1 notifications) */
    7, 5,
    0x85,
    0x03,
    CDC_NOTIFY_MAX_PACKET, 0x00,
    0x10,

    /* ---- CDC1 Interface 3: CDC Data ---- */
    9, 4,
    3, 0,
    2,
    0x0A, 0x00, 0x00,
    0,

    /* EP4 IN: Bulk (CDC1 data device -> host) */
    7, 5,
    0x84,
    0x02,
    CDC_DATA_MAX_PACKET, 0x00,
    0x00,

    /* EP4 OUT: Bulk (CDC1 data host -> device) */
    7, 5,
    0x04,
    0x02,
    CDC_DATA_MAX_PACKET, 0x00,
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
/* CDC ACM state                                                              */
/* ========================================================================== */

/* Line coding: 57600 8N1 (default), one per CDC ACM function. */
static uint8_t cdc_line_coding[CDC_PORT_COUNT][7] = {
    { 0x00, 0xE1, 0x00, 0x00, 0, 0, 8 },
    { 0x00, 0xE1, 0x00, 0x00, 0, 0, 8 },
};

/* CDC serial state bits for notification */
static volatile uint16_t cdc_serial_state[CDC_PORT_COUNT] = {0};

/* RX callback (registered by UARTDriver.cpp) */
static usb_rx_callback_t cdc_rx_cb[CDC_PORT_COUNT] = {NULL, NULL};
static void *cdc_rx_cb_arg[CDC_PORT_COUNT] = {NULL, NULL};

/* Configured/connected flags */
static bool _cdc_configured[CDC_PORT_COUNT] = {false, false};
static bool _cdc_connected[CDC_PORT_COUNT] = {false, false};

static const uint8_t cdc_data_in_eps[CDC_PORT_COUNT] = {
    CDC0_DATA_IN_EP, CDC1_DATA_IN_EP,
};

static const uint8_t cdc_data_out_eps[CDC_PORT_COUNT] = {
    CDC0_DATA_OUT_EP, CDC1_DATA_OUT_EP,
};

static const uint8_t cdc_notify_in_ep[CDC_PORT_COUNT] = {
    CDC0_NOTIFY_IN_EP, CDC1_NOTIFY_IN_EP,
};

static const uint8_t cdc_comm_if[CDC_PORT_COUNT] = {0, 2};

static uint8_t cdc_port_from_interface(uint8_t iface)
{
    if (iface <= 1) {
        return 0;
    }
    if (iface <= 3) {
        return 1;
    }
    return 0xFF;
}

/* ========================================================================== */
/* EP1 (CDC Data IN — Bulk 64 bytes)                                          */
/* ========================================================================== */

static USBInEndpointState ep1_in_state;

static void cdc_ep1_in_cb(void *usbp, usbep_t ep)
{
    (void)usbp;
    (void)ep;
    /* Transfer completed — no action needed for now.
     * The caller can check transmission status via the transmitting bitmap. */
}

static RT_USBEndpointConfig ep1_config = {
    .ep_mode       = USB_EP_MODE_TYPE_BULK,
    .in_state      = (void *)&ep1_in_state,
    .out_state     = NULL,
    .in_maxsize    = CDC_DATA_MAX_PACKET,
    .out_maxsize   = 0,
    .in_multiplier = 1,
    .setup_buf     = {0},
    .setup_cb      = NULL,
    .in_cb         = cdc_ep1_in_cb,
    .out_cb        = NULL,
};

/* ========================================================================== */
/* EP2 (CDC Data OUT — Bulk 64 bytes)                                         */
/* ========================================================================== */

static uint8_t ep2_rx_buf[CDC_DATA_MAX_PACKET];
static USBOutEndpointState ep2_out_state;

static void cdc_ep2_out_cb(void *usbp, usbep_t ep)
{
    (void)usbp;
    (void)ep;

    if (cdc_rx_cb[0] && (ep2_out_state.rxcnt > 0))
        cdc_rx_cb[0](ep2_rx_buf, ep2_out_state.rxcnt, cdc_rx_cb_arg[0]);
}

static RT_USBEndpointConfig ep2_config = {
    .ep_mode       = USB_EP_MODE_TYPE_BULK,
    .in_state      = NULL,
    .out_state     = (void *)&ep2_out_state,
    .in_maxsize    = 0,
    .out_maxsize   = CDC_DATA_MAX_PACKET,
    .in_multiplier = 0,
    .setup_buf     = {0},
    .setup_cb      = NULL,
    .in_cb         = NULL,
    .out_cb        = cdc_ep2_out_cb,
};

/* ========================================================================== */
/* EP3 (CDC Notification IN — Interrupt 8 bytes)                              */
/* ========================================================================== */

static USBInEndpointState ep3_in_state;

static void cdc_ep3_in_cb(void *usbp, usbep_t ep)
{
    (void)usbp;
    (void)ep;
    /* Notification sent. */
}

static RT_USBEndpointConfig ep3_config = {
    .ep_mode       = USB_EP_MODE_TYPE_INTR,
    .in_state      = (void *)&ep3_in_state,
    .out_state     = NULL,
    .in_maxsize    = CDC_NOTIFY_MAX_PACKET,
    .out_maxsize   = 0,
    .in_multiplier = 1,
    .setup_buf     = {0},
    .setup_cb      = NULL,
    .in_cb         = cdc_ep3_in_cb,
    .out_cb        = NULL,
};

/* ========================================================================== */
/* EP4 (CDC1 Data IN/OUT — Bulk 64 bytes)                                     */
/* ========================================================================== */

static USBInEndpointState ep4_in_state;
static uint8_t ep4_rx_buf[CDC_DATA_MAX_PACKET];
static USBOutEndpointState ep4_out_state;

static void cdc_ep4_in_cb(void *usbp, usbep_t ep)
{
    (void)usbp;
    (void)ep;
}

static void cdc_ep4_out_cb(void *usbp, usbep_t ep)
{
    (void)usbp;
    (void)ep;

    if (cdc_rx_cb[1] && (ep4_out_state.rxcnt > 0))
        cdc_rx_cb[1](ep4_rx_buf, ep4_out_state.rxcnt, cdc_rx_cb_arg[1]);
}

static RT_USBEndpointConfig ep4_config = {
    .ep_mode       = USB_EP_MODE_TYPE_BULK,
    .in_state      = (void *)&ep4_in_state,
    .out_state     = (void *)&ep4_out_state,
    .in_maxsize    = CDC_DATA_MAX_PACKET,
    .out_maxsize   = CDC_DATA_MAX_PACKET,
    .in_multiplier = 1,
    .setup_buf     = {0},
    .setup_cb      = NULL,
    .in_cb         = cdc_ep4_in_cb,
    .out_cb        = cdc_ep4_out_cb,
};

/* ========================================================================== */
/* EP5 (CDC1 Notification IN — Interrupt 8 bytes)                             */
/* ========================================================================== */

static USBInEndpointState ep5_in_state;

static void cdc_ep5_in_cb(void *usbp, usbep_t ep)
{
    (void)usbp;
    (void)ep;
}

static RT_USBEndpointConfig ep5_config = {
    .ep_mode       = USB_EP_MODE_TYPE_INTR,
    .in_state      = (void *)&ep5_in_state,
    .out_state     = NULL,
    .in_maxsize    = CDC_NOTIFY_MAX_PACKET,
    .out_maxsize   = 0,
    .in_multiplier = 1,
    .setup_buf     = {0},
    .setup_cb      = NULL,
    .in_cb         = cdc_ep5_in_cb,
    .out_cb        = NULL,
};

/* ========================================================================== */
/* CDC descriptor callback (for HAL _usb_default_handler)                     */
/* ========================================================================== */

static const uint8_t *cdc_get_descriptor(void *usbp, uint8_t dtype,
                                          uint8_t dindex, uint16_t lang)
{
    (void)usbp;
    (void)lang;

    switch (dtype) {

    case USB_DTYPE_DEVICE:
        return usb_dev_desc;

    case USB_DTYPE_CONFIGURATION:
        return usb_cfg_desc;

    case USB_DTYPE_STRING:
        switch (dindex) {
        case 0:  return usb_str_lang;
        case 1:  return usb_str_manufacturer;
        case 2:  return usb_str_product;
        case 3:  return usb_str_serial;
        default: return NULL;
        }

    case USB_DTYPE_DEVICE_QUALIFIER:
    case USB_DTYPE_OTHER_SPEED:
    default:
        return NULL;
    }
}

/* ========================================================================== */
/* CDC event callback (USB_EVENT_* notifications)                             */
/* ========================================================================== */

static void cdc_event_cb(void *usbp, uint8_t event)
{
    RT_USBDriver *drv = (RT_USBDriver *)usbp;

    switch (event) {

    case USB_EVENT_CONFIGURED:
        /* Configure CDC0 and CDC1 endpoints. */
        drv->epc[1] = &ep1_config;
        drv->epc[2] = &ep2_config;
        drv->epc[3] = &ep3_config;
        drv->epc[4] = &ep4_config;
        drv->epc[5] = &ep5_config;

        usb_lld_init_endpoint(drv, 1);
        usb_lld_init_endpoint(drv, 2);
        usb_lld_init_endpoint(drv, 3);
        usb_lld_init_endpoint(drv, 4);
        usb_lld_init_endpoint(drv, 5);

        /* Arm OUT endpoints for the first data reception. */
        {
            USBOutEndpointState *osp = (USBOutEndpointState *)ep2_config.out_state;
            osp->rxbuf  = ep2_rx_buf;
            osp->rxsize = CDC_DATA_MAX_PACKET;
            osp->rxcnt  = 0;
            osp->totsize = CDC_DATA_MAX_PACKET;
        }
        usb_lld_start_out(drv, 2);

        {
            USBOutEndpointState *osp = (USBOutEndpointState *)ep4_config.out_state;
            osp->rxbuf  = ep4_rx_buf;
            osp->rxsize = CDC_DATA_MAX_PACKET;
            osp->rxcnt  = 0;
            osp->totsize = CDC_DATA_MAX_PACKET;
        }
        usb_lld_start_out(drv, 4);

        for (uint8_t i = 0; i < CDC_PORT_COUNT; i++) {
            _cdc_configured[i] = true;
            _cdc_connected[i]  = true;
        }
        break;

    case USB_EVENT_RESET:
        /* Invalidate CDC endpoints. */
        drv->epc[1] = NULL;
        drv->epc[2] = NULL;
        drv->epc[3] = NULL;
        drv->epc[4] = NULL;
        drv->epc[5] = NULL;

        for (uint8_t i = 0; i < CDC_PORT_COUNT; i++) {
            _cdc_configured[i] = false;
            _cdc_connected[i]  = false;
        }
        break;

    case USB_EVENT_ADDRESS:
    case USB_EVENT_SUSPEND:
    case USB_EVENT_WAKEUP:
    case USB_EVENT_STALLED:
    default:
        break;
    }
}

/* ========================================================================== */
/* CDC class request handler (SETUP packets with bmReqType = CLASS)          */
/* ========================================================================== */

static void cdc_requests_hook(void *usbp)
{
    RT_USBDriver *drv = (RT_USBDriver *)usbp;
    uint8_t *s = drv->setup;
    uint8_t  bmReqType = s[0];
    uint8_t  bRequest  = s[1];
    uint16_t wValue    = (uint16_t)s[2] | ((uint16_t)s[3] << 8);
    uint16_t wIndex    = (uint16_t)s[4] | ((uint16_t)s[5] << 8);
    uint16_t wLength   = (uint16_t)s[6] | ((uint16_t)s[7] << 8);
    uint8_t port = cdc_port_from_interface((uint8_t)(wIndex & 0xFF));

    (void)wValue;

    /* Only class requests. */
    if ((bmReqType & USB_TYPE_MASK) != USB_TYPE_CLASS)
        return;
    if (port >= CDC_PORT_COUNT)
        return;

    switch (bRequest) {

    case CDC_REQ_SET_LINE_CODING: {
        /* Data stage OUT (host -> device), same as ChibiOS/usb_cdc. */
        uint16_t len = (wLength < 7) ? wLength : 7;
        drv->ep0state = USB_EP0_STATE_WAITING_DATA_OUT;
        drv->ep0data  = cdc_line_coding[port];
        drv->ep0len   = len;
        {
            USBOutEndpointState *osp =
                (USBOutEndpointState *)drv->epc[0]->out_state;
            osp->rxbuf   = cdc_line_coding[port];
            osp->rxsize  = len;
            osp->rxcnt   = 0;
            osp->totsize = len;
        }
        usb_lld_start_out(drv, 0);
        return;
    }

    case CDC_REQ_GET_LINE_CODING: {
        /* Device -> host, IN with data. */
        uint16_t len = (wLength < 7) ? wLength : 7;
        usb_setup_transfer(drv, cdc_line_coding[port], len, NULL);
        break;
    }

    case CDC_REQ_SET_CONTROL_LINE_STATE: {
        /* Update DTR and RTS flags. */
        if (wValue & 0x01)
            cdc_serial_state[port] |= 0x0002;   /* DTR active */
        else
            cdc_serial_state[port] &= ~0x0002;
        if (wValue & 0x02)
            cdc_serial_state[port] |= 0x0001;   /* RTS active */
        else
            cdc_serial_state[port] &= ~0x0001;
        usb_setup_transfer(drv, NULL, 0, NULL);
        break;
    }

    case CDC_REQ_SEND_BREAK:
        /* Acknowledge with ZLP status. */
        usb_setup_transfer(drv, NULL, 0, NULL);
        break;

    default:
        /* Unknown CDC request — stall EP0. */
        usb_lld_stall_in(drv, 0);
        usb_lld_stall_out(drv, 0);
        break;
    }
}

/* ========================================================================== */
/* RTT_USBConfig instance for CDC                                             */
/* ========================================================================== */

static const RTT_USBConfig cdc_usb_config = {
    .event_cb          = cdc_event_cb,
    .sof_cb            = NULL,
    .requests_hook_cb  = cdc_requests_hook,
    .get_descriptor_cb = cdc_get_descriptor,
};

/* ========================================================================== */
/* Public API                                                                 */
/* ========================================================================== */

bool usb_cdc_init(void)
{
    /* Register the CDC configuration callbacks with the USB driver. */
    rtt_usb.config = &cdc_usb_config;

    for (uint8_t i = 0; i < CDC_PORT_COUNT; i++) {
        _cdc_configured[i] = false;
        _cdc_connected[i]  = false;
        cdc_rx_cb[i]       = NULL;
        cdc_rx_cb_arg[i]   = NULL;
        cdc_serial_state[i] = 0;
    }

    return true;
}

bool usb_cdc_send_data(const uint8_t *data, uint32_t len)
{
    return usb_cdc_send_data_idx(0, data, len);
}

bool usb_cdc_send_data_idx(uint8_t idx, const uint8_t *data, uint32_t len)
{
    RT_USBDriver *drv = &rtt_usb;
    USBInEndpointState *isp;
    uint8_t ep;

    if (idx >= CDC_PORT_COUNT || !_cdc_configured[idx])
        return false;
    if (data == NULL || len == 0)
        return false;
    if (len > CDC_DATA_MAX_PACKET)
        len = CDC_DATA_MAX_PACKET;

    ep = cdc_data_in_eps[idx];
    if (drv->transmitting & (1U << ep))
        return false;

    isp = (idx == 0) ? &ep1_in_state : &ep4_in_state;

    isp->txbuf   = data;
    isp->txsize  = (uint16_t)len;
    isp->txcnt   = 0;
    isp->totsize = (uint16_t)len;

    drv->transmitting |= (1U << ep);
    usb_lld_start_in(drv, ep);
    return true;
}

void usb_cdc_set_rx_callback(usb_rx_callback_t cb, void *arg)
{
    usb_cdc_set_rx_callback_idx(0, cb, arg);
}

void usb_cdc_set_rx_callback_idx(uint8_t idx, usb_rx_callback_t cb, void *arg)
{
    if (idx >= CDC_PORT_COUNT)
        return;
    cdc_rx_cb[idx]     = cb;
    cdc_rx_cb_arg[idx] = arg;
}

void usb_cdc_rearm_out(void)
{
    usb_cdc_rearm_out_idx(0);
}

void usb_cdc_rearm_out_idx(uint8_t idx)
{
    RT_USBDriver *drv = &rtt_usb;
    USBOutEndpointState *osp;
    uint8_t *rxbuf;
    uint8_t ep;

    if (idx >= CDC_PORT_COUNT || !_cdc_configured[idx])
        return;

    ep = cdc_data_out_eps[idx];
    if (drv->receiving & (1U << ep))
        return;   /* already armed */

    osp = (idx == 0) ? &ep2_out_state : &ep4_out_state;
    rxbuf = (idx == 0) ? ep2_rx_buf : ep4_rx_buf;

    osp->rxbuf  = rxbuf;
    osp->rxsize = CDC_DATA_MAX_PACKET;
    osp->rxcnt  = 0;
    osp->totsize = CDC_DATA_MAX_PACKET;

    drv->receiving |= (1U << ep);
    usb_lld_start_out(drv, ep);
}

bool usb_cdc_send_notification(uint16_t serial_state)
{
    return usb_cdc_send_notification_idx(0, serial_state);
}

bool usb_cdc_send_notification_idx(uint8_t idx, uint16_t serial_state)
{
    static uint8_t notify[CDC_PORT_COUNT][10];
    USBInEndpointState *isp;
    uint8_t ep;

    if (idx >= CDC_PORT_COUNT || !_cdc_configured[idx])
        return false;

    /* CDC Notification header (SERIAL_STATE). */
    notify[idx][0] = 0xA1;       /* bmRequestType: Device->Host, Interface, Class */
    notify[idx][1] = 0x20;       /* bNotification: Serial State */
    notify[idx][2] = 0x00;       /* wValue */
    notify[idx][3] = 0x00;
    notify[idx][4] = cdc_comm_if[idx];
    notify[idx][5] = 0x00;
    notify[idx][6] = 0x02;       /* wLength = 2 */
    notify[idx][7] = 0x00;
    notify[idx][8] = (uint8_t)(serial_state & 0xFF);
    notify[idx][9] = (uint8_t)((serial_state >> 8) & 0xFF);

    ep = cdc_notify_in_ep[idx];
    isp = (idx == 0) ? &ep3_in_state : &ep5_in_state;

    isp->txbuf   = notify[idx];
    isp->txsize  = 10;
    isp->txcnt   = 0;
    isp->totsize = 10;

    usb_lld_start_in(&rtt_usb, ep);
    return true;
}

bool usb_cdc_is_configured(void)
{
    return usb_cdc_is_configured_idx(0);
}

bool usb_cdc_is_configured_idx(uint8_t idx)
{
    return idx < CDC_PORT_COUNT && _cdc_configured[idx];
}

bool usb_cdc_is_connected(void)
{
    return usb_cdc_is_connected_idx(0);
}

bool usb_cdc_is_connected_idx(uint8_t idx)
{
    return idx < CDC_PORT_COUNT &&
        (_cdc_connected[idx] || (rtt_usb.state >= USB_STATE_SELECTED));
}

uint8_t usb_cdc_data_in_ep(uint8_t idx)
{
    return idx < CDC_PORT_COUNT ? cdc_data_in_eps[idx] : 0;
}

uint8_t usb_cdc_data_out_ep(uint8_t idx)
{
    return idx < CDC_PORT_COUNT ? cdc_data_out_eps[idx] : 0;
}
