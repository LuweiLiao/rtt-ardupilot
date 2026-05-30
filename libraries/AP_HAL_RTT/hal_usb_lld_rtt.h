/*
 * RTT adaptation of ChibiOS STM32 OTGv1 USB LLD — Header
 *
 * 1:1 port of modules/ChibiOS/os/hal/ports/STM32/LLD/OTGv1/hal_usb_lld.{c,h}
 * Provides the 14 standard usb_lld_* functions plus compatibility API.
 *
 * Self-contained, no ChibiOS/HAL dependency.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================== */
/* Type definitions                                                           */
/* ========================================================================== */

typedef uint8_t   usbep_t;
typedef uint16_t  usbepstatus_t;

typedef void (*usbcallback_t)(void *usbp);
typedef void (*usbepcallback_t)(void *usbp, usbep_t ep);
typedef void (*usbeventcb_t)(void *usbp, uint8_t event);

/* CDC data receive callback for the compatibility layer */
typedef void (*usb_rx_callback_t)(const uint8_t *data, uint32_t len, void *arg);

/* ========================================================================== */
/* USB driver states                                                          */
/* ========================================================================== */

#define USB_STATE_UNINIT     0
#define USB_STATE_STOP       1
#define USB_STATE_READY      2
#define USB_STATE_SELECTED   3
#define USB_STATE_ACTIVE     4
#define USB_STATE_SUSPENDED  5

/* ========================================================================== */
/* USB events                                                                 */
/* ========================================================================== */

#define USB_EVENT_RESET      0
#define USB_EVENT_ADDRESS    1
#define USB_EVENT_CONFIGURED 2
#define USB_EVENT_SUSPEND    3
#define USB_EVENT_WAKEUP     4
#define USB_EVENT_STALLED    5

/* ========================================================================== */
/* USB endpoint mode types                                                    */
/* ========================================================================== */

#define USB_EP_MODE_TYPE_CTRL  0x00
#define USB_EP_MODE_TYPE_ISO   0x01
#define USB_EP_MODE_TYPE_BULK  0x02
#define USB_EP_MODE_TYPE_INTR  0x03
#define USB_EP_MODE_TYPE       0x03

/* ========================================================================== */
/* Endpoint status                                                            */
/* ========================================================================== */

#define EP_STATUS_DISABLED   0
#define EP_STATUS_STALLED    1
#define EP_STATUS_ACTIVE     2

/* ========================================================================== */
/* EP0 state machine                                                          */
/* ========================================================================== */

#define USB_EP0_STATE_IDLE               0
#define USB_EP0_STATE_WAITING_DATA_IN    1
#define USB_EP0_STATE_WAITING_DATA_OUT   2
#define USB_EP0_STATE_WAITING_STATUS_IN  3
#define USB_EP0_STATE_WAITING_STATUS_OUT 4
#define USB_EP0_STATE_STALL              5
#define USB_EP0_STATE_WAITING_IN_ZLP     6  /* ZLP IN after MPS-aligned data stage */

/* ========================================================================== */
/* Setup packet field masks                                                   */
/* ========================================================================== */

#define USB_DIR_MASK            0x80U
#define USB_DIR_HOST_TO_DEVICE  0x00U
#define USB_DIR_DEVICE_TO_HOST  0x80U
#define USB_TYPE_MASK           0x60U
#define USB_TYPE_STANDARD       0x00U
#define USB_TYPE_CLASS          0x20U
#define USB_TYPE_VENDOR         0x40U
#define USB_RECIPIENT_MASK      0x1FU
#define USB_RECIPIENT_DEVICE    0
#define USB_RECIPIENT_INTERFACE 1
#define USB_RECIPIENT_ENDPOINT  2
#define USB_RECIPIENT_OTHER     3

/* ========================================================================== */
/* Standard USB requests                                                      */
/* ========================================================================== */

#define USB_REQ_GET_STATUS           0
#define USB_REQ_CLEAR_FEATURE        1
#define USB_REQ_SET_FEATURE          3
#define USB_REQ_SET_ADDRESS          5
#define USB_REQ_GET_DESCRIPTOR       6
#define USB_REQ_SET_DESCRIPTOR       7
#define USB_REQ_GET_CONFIGURATION    8
#define USB_REQ_SET_CONFIGURATION    9
#define USB_REQ_GET_INTERFACE        10
#define USB_REQ_SET_INTERFACE        11
#define USB_REQ_SYNCH_FRAME          12

/* Feature selectors */
#define USB_FEATURE_ENDPOINT_HALT    0
#define USB_FEATURE_REMOTE_WAKEUP    1

/* Descriptor types */
#define USB_DTYPE_DEVICE             1
#define USB_DTYPE_CONFIGURATION      2
#define USB_DTYPE_STRING             3
#define USB_DTYPE_INTERFACE          4
#define USB_DTYPE_ENDPOINT           5
#define USB_DTYPE_DEVICE_QUALIFIER   6
#define USB_DTYPE_OTHER_SPEED        7
#define USB_DTYPE_INTERFACE_POWER    8

/* ChibiOS OTGv1 template: late set-address after EP0 status IN (USB 2.0 §9.4.6). */
#ifndef USB_EARLY_SET_ADDRESS
#define USB_EARLY_SET_ADDRESS        0
#endif
#ifndef USB_LATE_SET_ADDRESS
#define USB_LATE_SET_ADDRESS         1
#endif
#ifndef USB_SET_ADDRESS_MODE
#define USB_SET_ADDRESS_MODE         USB_LATE_SET_ADDRESS
#endif

/* ========================================================================== */
/* Data structures                                                            */
/* ========================================================================== */

/**
 * @brief   IN endpoint state.
 */
typedef struct {
    const uint8_t *txbuf;           /**< Transmit buffer pointer.            */
    uint16_t      txsize;           /**< Transaction transmit size.          */
    uint16_t      txcnt;            /**< Transmitted bytes in this xact.     */
    uint16_t      totsize;          /**< Total size (for multi-packet).      */
} USBInEndpointState;

/**
 * @brief   OUT endpoint state.
 */
typedef struct {
    uint8_t  *rxbuf;                /**< Receive buffer pointer.             */
    uint16_t rxsize;                /**< Transaction receive size.           */
    uint16_t rxcnt;                 /**< Received bytes in this xact.        */
    uint16_t totsize;               /**< Total size (for multi-packet).      */
} USBOutEndpointState;

/**
 * @brief   Endpoint configuration.
 */
typedef struct {
    uint8_t       ep_mode;          /**< USB_EP_MODE_TYPE_CTRL/BULK/ISO/INTR */
    void          *in_state;        /**< USBInEndpointState (or NULL).       */
    void          *out_state;       /**< USBOutEndpointState (or NULL).      */
    uint16_t      in_maxsize;       /**< IN MPS (0 = unidirectional OUT).    */
    uint16_t      out_maxsize;      /**< OUT MPS (0 = unidirectional IN).    */
    uint8_t       in_multiplier;    /**< TX FIFO multiplier.                 */
    uint8_t       *setup_buf;       /**< Pointer to 8-byte SETUP buffer.     */
    void (*setup_cb)(void *usbp, uint8_t ep);
    void (*in_cb)(void *usbp, uint8_t ep);
    void (*out_cb)(void *usbp, uint8_t ep);
} RT_USBEndpointConfig;

/**
 * @brief   USB driver configuration.
 */
typedef struct {
    void (*event_cb)(void *usbp, uint8_t event);
    void (*sof_cb)(void *usbp);
    void (*requests_hook_cb)(void *usbp);
    const uint8_t *(*get_descriptor_cb)(void *usbp, uint8_t dtype,
                                        uint8_t dindex, uint16_t lang);
} RTT_USBConfig;

/**
 * @brief   Main USB driver structure.
 */
typedef struct {
    const void       *config;       /**< RTT_USBConfig pointer.              */
    uint16_t         state;         /**< USB_STATE_*.                        */
    uint16_t         status;        /**< Status flags (remote wakeup etc).   */
    uint16_t         transmitting;  /**< IN-transmission bitmap.             */
    uint16_t         receiving;     /**< OUT-reception bitmap.               */
    uint8_t          address;       /**< Assigned USB address.               */
    uint8_t          configuration; /**< Current configuration value.        */
    void             *otg;          /**< OTG_FS register base.               */
    void             *otgparams;    /**< Parameter block pointer.            */
    uint32_t         pmnext;        /**< FIFO RAM alloc ptr (words).         */
    RT_USBEndpointConfig *epc[8];  /**< Endpoint config pointers.            */
    uint8_t          setup[8];      /**< Current SETUP packet buffer.        */
    uint8_t          ep0state;      /**< EP0 state machine state.            */
    const uint8_t    *ep0data;      /**< Current EP0 data pointer.           */
    uint16_t         ep0len;        /**< EP0 data length remaining.          */
    uint16_t         ep0max;        /**< EP0 MPS.                            */
    void (*ep0endcb)(void *usbp);  /**< EP0 transfer end callback.           */
    void (*event_cb)(void *usbp, uint8_t event);
} RT_USBDriver;

/** Global USB device driver (LLD + CDC share this instance). */
extern RT_USBDriver rtt_usb;

/* ========================================================================== */
/* Public API — 14 standard LLD functions + serve_interrupt                  */
/* ========================================================================== */

void usb_lld_init(void *usbp);
void usb_lld_start(void *usbp);
void usb_lld_stop(void *usbp);
void usb_lld_reset(void *usbp);
void usb_lld_set_address(void *usbp);
void usb_lld_init_endpoint(void *usbp, usbep_t ep);
void usb_lld_disable_endpoints(void *usbp);
usbepstatus_t usb_lld_get_status_out(void *usbp, usbep_t ep);
usbepstatus_t usb_lld_get_status_in(void *usbp, usbep_t ep);
void usb_lld_read_setup(void *usbp, usbep_t ep, uint8_t *buf);
void usb_lld_start_out(void *usbp, usbep_t ep);
void usb_lld_start_in(void *usbp, usbep_t ep);
void usb_lld_stall_out(void *usbp, usbep_t ep);
void usb_lld_stall_in(void *usbp, usbep_t ep);

void usb_setup_transfer(void *usbp, const void *buf, size_t len,
                        void (*callback)(void *usbp));
void usb_lld_clear_out(void *usbp, usbep_t ep);
void usb_lld_clear_in(void *usbp, usbep_t ep);
void usb_lld_serve_interrupt(void *usbp);

/* ========================================================================== */
/* Macro-like API — implemented as inline functions here,                    */
/* or as regular functions if the implementation requires .c access.         */
/* ========================================================================== */

/**
 * @brief   Returns the exact size of a completed receive transaction.
 */
static inline uint16_t usb_lld_get_transaction_size(void *usbp, usbep_t ep) {
    USBOutEndpointState *osp = (USBOutEndpointState *)
        ((RT_USBDriver *)usbp)->epc[ep]->out_state;
    return osp->rxcnt;
}

/* These are declared here and implemented in hal_usb_lld_rtt.c
 * because they access OTG register definitions private to the .c file. */
void usb_lld_connect_bus(void *usbp);
void usb_lld_disconnect_bus(void *usbp);

/* ========================================================================== */
/* Higher-level HAL functions (called by usb_lld_serve_interrupt)            */
/* Should be declared in the HAL layer but included here for self-containment*/
/* ========================================================================== */

void usb_object_init(void *usbp);
void usb_setup_transfer(void *usbp, const void *buf, size_t len,
                        void (*callback)(void *usbp));

/* ========================================================================== */
/* Compatibility API — for UARTDriver.cpp (existing callers)                  */
/* Must remain stable.                                                        */
/* ========================================================================== */

bool usb_lld_send_rtt(uint8_t ep, const uint8_t *data, uint32_t len);
void usb_lld_set_rx_callback(usb_rx_callback_t cb, void *arg);
void usb_lld_rearm_cdc_out(void);
bool usb_lld_is_configured_rtt(void);
bool usb_lld_get_connected_rtt(void);
void usb_lld_poll_rtt(void);
void usb_lld_set_address_rtt(uint8_t addr);
bool usb_lld_send_cdc_notification(uint16_t serial_state);

/* Single-call initialization (clock, GPIO, NVIC, core reset) */
bool usb_lld_init_rtt(void);

#ifdef __cplusplus
}
#endif
