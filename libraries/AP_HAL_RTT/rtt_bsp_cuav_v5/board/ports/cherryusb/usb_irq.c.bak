/*
 * USB OTG_FS IRQ for CUAV V5 (CherryUSB DWC2).
 *
 * The upstream usb_glue_st.c OTG_FS_IRQHandler unconditionally calls
 * g_usb_dwc2_irq[0]() without a null check. If the VBUS/OTG_FS interrupt
 * fires before cdc_acm_chardev_init() has registered the handler (e.g. the
 * USB cable was already plugged in at power-on), it dereferences a null
 * function pointer and causes a HardFault.
 *
 * This file provides a strong OTG_FS_IRQHandler that calls USBD_IRQHandler(0)
 * (safe to call at any time).
 */
#include "board.h"

extern void USBD_IRQHandler(uint8_t busid);

void OTG_FS_IRQHandler(void)
{
    USBD_IRQHandler(0);
}
