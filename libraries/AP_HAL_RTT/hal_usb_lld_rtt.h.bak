/*
 * RTT adaptation of ChibiOS STM32 OTGv1 LLD (hal_usb_lld_rtt.h).
 *
 * Direct DWC2 register access for polling-mode USB device operation.
 * Bypasses CherryUSB CDC ACM stack entirely.
 *
 * ChibiOS reference:
 *   modules/ChibiOS/os/hal/ports/STM32/LLD/OTGv1/hal_usb_lld.c
 *   modules/ChibiOS/os/hal/ports/STM32/LLD/OTGv1/stm32_otg.h
 *
 * Key differences from ChibiOS OTGv1:
 *  - Polling mode (no interrupt handler)
 *  - Direct register operations vs USBDriver struct
 *  - No OSAL / chSysLock dependencies
 *  - Simplified: EP0 for enumeration + IN endpoint 1 for CDC data
 *
 * STM32F767 uses DWC2 OTG controller (OTG_FS @ 0x50000000).
 * Reference: RM0410 §45 (USB On-The-Go)
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * usb_lld_init_rtt — Initialize DWC2 core in device mode.
 *
 * Steps (matching ChibiOS OTGv1 hal_usb_lld.c: usb_lld_start):
 *   1. Enable OTG_FS clock (RCC AHB1ENR) and reset
 *   2. Configure GUSBCFG: forced device mode, FS PHY, TRDT=5
 *   3. Configure DCFG: full speed 1.1 PHY (48MHz)
 *   4. Enable PHY (PCGCCTL=0)
 *   5. VBUS sensing + transceiver (GOTGCTL, GCCFG)
 *   6. Core reset (GRSTCTL CSRST)
 *   7. GAHBCFG config, disable ep's, clear interrupts
 *   8. Set GINTMSK for reset/wakeup/suspend/SOF
 *   9. Enable global interrupt in GAHBCFG
 *
 * After init, call usb_lld_poll_rtt() periodically to handle
 * USB bus events (reset, enumeration, SOF).
 *
 * @return  true on success
 */
bool usb_lld_init_rtt(void);

/*
 * usb_lld_send_rtt — Send data on IN endpoint (polling mode).
 *
 * Writes data directly to DWC2 TX FIFO, sets DIEPTSIZ and
 * enables endpoint via DIEPCTL, then polls DIEPINT for XFRC.
 *
 * Steps (matching ChibiOS OTGv1 usb_lld_start_in):
 *   1. Write data bytes to FIFO[ep] (word-aligned)
 *   2. Set DIEPTSIZ: PKTCNT + XFRSIZ
 *   3. Set DIEPCTL: EPENA | CNAK
 *   4. Poll DIEPINT for XFRC
 *   5. Clear DIEPINT
 *
 * @param ep      IN endpoint number (1 for CDC bulk IN)
 * @param data    Pointer to data buffer
 * @param len     Number of bytes to send (max 64 for FS bulk)
 * @return        true on successful transfer, false on timeout
 */
bool usb_lld_send_rtt(uint8_t ep, const uint8_t *data, uint32_t len);

/*
 * usb_lld_poll_rtt — Poll for USB bus events.
 *
 * Must be called periodically (>= 1kHz) to handle:
 *  - USB reset (re-init endpoints)
 *  - Enumeration done (speed detection)
 *  - Suspend / Wakeup
 *  - SOF
 *  - RX FIFO data (setup packets, OUT data)
 *
 * ChibiOS reference: usb_lld_serve_interrupt()
 */
void usb_lld_poll_rtt(void);

/*
 * usb_lld_set_address_rtt — Set USB device address.
 *
 * ChibiOS reference: usb_lld_set_address (writes DCFG.DAD)
 */
void usb_lld_set_address_rtt(uint8_t addr);

/*
 * usb_lld_get_connected_rtt — Check if USB is connected/enumerated.
 *
 * Returns true once enumeration is complete (device has an address).
 */
bool usb_lld_get_connected_rtt(void);

#ifdef __cplusplus
}
#endif
