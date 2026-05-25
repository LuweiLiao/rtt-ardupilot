/*
 * RTT adaptation of ChibiOS STM32 OTGv1 LLD (hal_usb_lld_rtt.h).
 *
 * Complete self-contained DWC2 USB device driver.
 * Supports: EP0 control, EP1 IN bulk (CDC data TX),
 *           EP2 OUT bulk (CDC data RX), EP3 IN interrupt (CDC notification).
 * No CherryUSB dependency.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Type for CDC data receive callback */
typedef void (*usb_rx_callback_t)(const uint8_t *data, uint32_t len, void *arg);

/*
 * usb_lld_init_rtt — Initialize DWC2 core in device mode.
 * Must be called once before any other USB operations.
 * Returns true on success.
 */
bool usb_lld_init_rtt(void);

/*
 * usb_lld_poll_rtt — Poll for USB bus events.
 * Must be called periodically (>= 1kHz) to handle USB enumeration,
 * data transfers, and control requests.
 */
void usb_lld_poll_rtt(void);

/*
 * usb_lld_send_rtt — Send data on an IN endpoint (polling mode).
 * Used for CDC data (EP1) and CDC notifications (EP3).
 * Writes data to DWC2 TX FIFO and polls for completion.
 * @param ep      IN endpoint number (1 for CDC bulk IN, 3 for CDC notify)
 * @param data    Pointer to data buffer
 * @param len     Number of bytes to send
 * @return        true on successful transfer, false on timeout
 */
bool usb_lld_send_rtt(uint8_t ep, const uint8_t *data, uint32_t len);

/*
 * usb_lld_set_address_rtt — Set USB device address (DCFG.DAD).
 */
void usb_lld_set_address_rtt(uint8_t addr);

/*
 * usb_lld_get_connected_rtt — Check if USB is enumerated.
 * Returns true once enumeration is complete (speed detected).
 */
bool usb_lld_get_connected_rtt(void);

/*
 * usb_lld_is_configured_rtt — Check if USB device is configured.
 * Returns true after SET_CONFIGURATION request has been received.
 */
bool usb_lld_is_configured_rtt(void);

/*
 * usb_lld_set_rx_callback — Register callback for CDC data received from host.
 * The callback is invoked from usb_lld_poll_rtt() context with received data
 * and length (max EP2_MAX_PACKET = 64 bytes per call).
 */
void usb_lld_set_rx_callback(usb_rx_callback_t cb, void *arg);

/*
 * usb_lld_send_cdc_notification — Send CDC serial state notification.
 * @param serial_state  CDC SERIAL_STATE_* bitmask
 * @return true on success
 */
bool usb_lld_send_cdc_notification(uint16_t serial_state);

/*
 * usb_lld_rearm_cdc_out — Re-arm EP2 OUT for receiving next CDC packet.
 * Called after processing received data to allow the next packet to arrive.
 */
void usb_lld_rearm_cdc_out(void);

#ifdef __cplusplus
}
#endif
