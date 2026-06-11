/*
 * RTT USB CDC ACM class layer — Header
 *
 * Provides the CDC-ACM abstraction for the RTT USB stack:
 *  - Descriptor set (device, configuration, string descriptors)
 *  - Class request handling (line coding, control line state, break)
 *  - Two CDC ACM functions for ChibiOS-style OTG1/OTG2 logical ports
 *
 * The CDC layer integrates with the USB stack by registering its
 * callbacks into RTT_USBConfig (accessed via the rtt_usb global driver).
 * Call usb_cdc_init() after the LLD is initialized but before USB
 * bus activity starts.
 *
 * ChibiOS Copyright (C) 2006..2018 Giovanni Di Sirio
 *   Licensed under the Apache License, Version 2.0
 */

#ifndef USB_CDC_RTT_H
#define USB_CDC_RTT_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief   Receive callback type.
 * @param[in] data    pointer to received bytes
 * @param[in] len     number of bytes received
 * @param[in] arg     user-supplied opaque argument
 */
typedef void (*usb_rx_callback_t)(const uint8_t *data, uint32_t len, void *arg);

/* CDC ACM class requests (USB CDC PSTN subclass). */
#define CDC_REQ_SET_LINE_CODING         0x20
#define CDC_REQ_GET_LINE_CODING         0x21
#define CDC_REQ_SET_CONTROL_LINE_STATE  0x22
#define CDC_REQ_SEND_BREAK              0x23

/* ========================================================================== */
/* Public API                                                                 */
/* ========================================================================== */

/**
 * @brief   Initialise the CDC ACM layer.
 *
 * Registers the descriptor, event and class-request callbacks into the
 * rtt_usb driver's RTT_USBConfig.  Must be called after the LLD and HAL
 * are initialised, before USB enumeration begins.
 *
 * @return              true on success
 */
bool usb_cdc_init(void);

/**
 * @brief   Send data on the CDC bulk-IN endpoint (EP1).
 *
 * @param[in] data      pointer to data buffer
 * @param[in] len       number of bytes to send (clamped to EP1 MPS = 64)
 * @return              true if the transfer was started
 */
bool usb_cdc_send_data(const uint8_t *data, uint32_t len);
bool usb_cdc_send_data_idx(uint8_t idx, const uint8_t *data, uint32_t len);

/**
 * @brief   Register a receive callback for CDC data (EP2 OUT).
 *
 * @param[in] cb        callback function (NULL to unregister)
 * @param[in] arg       opaque argument passed to the callback
 */
void usb_cdc_set_rx_callback(usb_rx_callback_t cb, void *arg);
void usb_cdc_set_rx_callback_idx(uint8_t idx, usb_rx_callback_t cb, void *arg);

/**
 * @brief   Re-arm EP2 OUT for the next CDC data reception.
 *
 * Must be called after the receive callback has consumed the data.
 */
void usb_cdc_rearm_out(void);
void usb_cdc_rearm_out_idx(uint8_t idx);

/**
 * @brief   Send a CDC SERIAL_STATE notification on EP3 (interrupt).
 *
 * @param[in] serial_state  CDC serial-state bitmask
 * @return              true if the notification was queued
 */
bool usb_cdc_send_notification(uint16_t serial_state);
bool usb_cdc_send_notification_idx(uint8_t idx, uint16_t serial_state);

/**
 * @brief   Check whether the USB device is configured (SET_CONFIGURATION
 *          received).
 */
bool usb_cdc_is_configured(void);
bool usb_cdc_is_configured_idx(uint8_t idx);

/**
 * @brief   Check whether a USB host is connected (device selected
 *          or active).
 */
bool usb_cdc_is_connected(void);
bool usb_cdc_is_connected_idx(uint8_t idx);

uint8_t usb_cdc_data_in_ep(uint8_t idx);
uint8_t usb_cdc_data_out_ep(uint8_t idx);

#ifdef __cplusplus
}
#endif

#endif /* USB_CDC_RTT_H */
