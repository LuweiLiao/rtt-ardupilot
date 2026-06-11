/*
 * CherryUSB device stack config for L7 CUAV V5 OTG_FS PoC (RT-Thread).
 */
#ifndef CHERRYUSB_CONFIG_H
#define CHERRYUSB_CONFIG_H

#include <rtthread.h>

#define CONFIG_USB_PRINTF(...) rt_kprintf(__VA_ARGS__)
#define CONFIG_USB_DBG_LEVEL   USB_DBG_INFO

#define CONFIG_USB_ALIGN_SIZE 4
#define USB_NOCACHE_RAM_SECTION
#define USB_MEM_ALIGNX          __attribute__((aligned(CONFIG_USB_ALIGN_SIZE)))

#define CONFIG_USBDEV_REQUEST_BUFFER_LEN 512
#define CONFIG_USBDEV_MAX_BUS            1
#define CONFIG_USBDEV_EP_NUM             4

/* Full-speed OTG FS on STM32F767 — no VBUS sensing */
#undef CONFIG_USB_HS
#undef CONFIG_DWC2_VBUS_SENSING

#define CONFIG_USB_DWC2_RXALL_FIFO_SIZE (128)
#define CONFIG_USB_DWC2_TX0_FIFO_SIZE   (16)
#define CONFIG_USB_DWC2_TX1_FIFO_SIZE   (64)
#define CONFIG_USB_DWC2_TX2_FIFO_SIZE   (4)
#define CONFIG_USB_DWC2_TX3_FIFO_SIZE   (16)

#endif /* CHERRYUSB_CONFIG_H */
