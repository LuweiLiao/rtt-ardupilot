/*
 * CherryUSB usb_config.h for CUAV V5 (STM32F7 OTG_FS DWC2)
 */
#ifndef CHERRYUSB_CONFIG_H
#define CHERRYUSB_CONFIG_H

#include "rtthread.h"

#define CONFIG_USB_PRINTF(...) rt_kprintf(__VA_ARGS__)

#ifndef CONFIG_USB_DBG_LEVEL
#define CONFIG_USB_DBG_LEVEL USB_DBG_INFO
#endif

#define CONFIG_USB_PRINTF_COLOR_ENABLE

#ifndef CONFIG_USB_ALIGN_SIZE
#define CONFIG_USB_ALIGN_SIZE 32
#endif

/* STM32F7 Cortex-M7 has D-Cache; use dcache clean/invalidate (usb_glue_st.c) */
#define CONFIG_USB_DCACHE_ENABLE
/* Buffers in normal RAM, coherence via dcache ops */
#define USB_NOCACHE_RAM_SECTION

/* USB Device Stack */
#ifndef CONFIG_USBDEV_MAX_BUS
#define CONFIG_USBDEV_MAX_BUS 1
#endif

#ifndef CONFIG_USBDEV_REQUEST_BUFFER_LEN
#define CONFIG_USBDEV_REQUEST_BUFFER_LEN 512
#endif

#ifndef CONFIG_USBDEV_EP_NUM
#define CONFIG_USBDEV_EP_NUM 6
#endif

/* DWC2 Configuration for STM32F7 OTG_FS (total FIFO 320 words)
 * TX1 doubled to allow two 64-byte packets queued for CDC IN endpoint */
#define CONFIG_USB_DWC2_TX0_FIFO_SIZE (64 / 4)
#define CONFIG_USB_DWC2_TX1_FIFO_SIZE (128 / 4)
#define CONFIG_USB_DWC2_TX2_FIFO_SIZE (64 / 4)
#define CONFIG_USB_DWC2_TX3_FIFO_SIZE (64 / 4)

/* Raised from 4K to 32K so PARAM_REQUEST_LIST (~10KB of PARAM_VALUE burst)
 * fits without filling the CherryUSB ring buffer. The old 4K buffer would
 * fill, rt_device_write would return 0, and after 500 failed drain ticks
 * (500 ms) the entire write buffer would be cleared — dropping parameter
 * data mid-transfer, causing QGC to time out and disconnect. */
#define CONFIG_USBDEV_SERIAL_TX_BUFSIZE 32768
#define CONFIG_USBDEV_SERIAL_RX_BUFSIZE 4096

/* USB Host (required by usbh_core.h included from glue) */
#ifndef CONFIG_USBHOST_DEV_NAMELEN
#define CONFIG_USBHOST_DEV_NAMELEN 16
#endif
#define CONFIG_USBHOST_MAX_RHPORTS          1
#define CONFIG_USBHOST_MAX_EXTHUBS          1
#define CONFIG_USBHOST_MAX_EHPORTS          4
#define CONFIG_USBHOST_MAX_INTERFACES       8
#define CONFIG_USBHOST_MAX_INTF_ALTSETTINGS 8
#define CONFIG_USBHOST_MAX_ENDPOINTS        4

#endif
