/*
 * L7 CherryUSB DWC2 glue for CUAV V5 OTG_FS (PA11/12, 0x50000000).
 * Single OTG_FS_IRQHandler — does not link usb_glue_st.c or hal_usb_lld_rtt.c.
 */
#include <stm32f7xx.h>
#include <rthw.h>
#include <rtthread.h>
#include "usbd_core.h"

#define L7_USB_OTG_FS_BASE  0x50000000UL
#define L7_OTG_FS_IRQn      67
#define L7_PWR_CR2_USV       (1U << 24)

extern uint32_t SystemCoreClock;

#if !defined(RTT_USB_PREINIT_DISCONNECT_US) && defined(RTT_USB_PREINIT_DISCONNECT_MS)
#define RTT_USB_PREINIT_DISCONNECT_US ((uint32_t)RTT_USB_PREINIT_DISCONNECT_MS * 1000U)
#endif

#ifndef RTT_USB_PREINIT_DISCONNECT_US
#define RTT_USB_PREINIT_DISCONNECT_US 1500U
#endif

static USB_OTG_DeviceTypeDef *l7_usb_device(void)
{
    return (USB_OTG_DeviceTypeDef *)(L7_USB_OTG_FS_BASE + USB_OTG_DEVICE_BASE);
}

static void l7_usb_clock_gpio(void)
{
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;
    RCC->AHB2ENR |= RCC_AHB2ENR_OTGFSEN;
    PWR->CR2 |= L7_PWR_CR2_USV;

    GPIOA->MODER &= ~((3U << 22) | (3U << 24));
    GPIOA->MODER |= (2U << 22) | (2U << 24);
    GPIOA->OSPEEDR |= (3U << 22) | (3U << 24);
    GPIOA->PUPDR &= ~((3U << 22) | (3U << 24));
    GPIOA->AFR[1] &= ~((0xFU << 12) | (0xFU << 16));
    GPIOA->AFR[1] |= (10U << 12) | (10U << 16);
}

static void l7_usb_soft_disconnect(void)
{
    l7_usb_device()->DCTL |= USB_OTG_DCTL_SDIS;
}

void l7_usb_hw_preinit(void)
{
    l7_usb_clock_gpio();
    l7_usb_soft_disconnect();
    /*
     * [Cybernetics Ch.4] Closed-loop: force the host to observe the handover
     * from the ArduPilot bootloader's single CDC device to the RTT app's
     * composite CDC device. Windows is less tolerant than Linux here and can
     * otherwise keep the old single-COM ArduPilot node alive.
     */
    if (RTT_USB_PREINIT_DISCONNECT_US > 0U) {
        rt_hw_us_delay(RTT_USB_PREINIT_DISCONNECT_US);
    }
}

void usb_dc_low_level_init(uint8_t busid)
{
    (void)busid;
    l7_usb_clock_gpio();

    NVIC_SetPriority(L7_OTG_FS_IRQn, 4);
    NVIC_EnableIRQ(L7_OTG_FS_IRQn);
}

void usb_dc_low_level_deinit(uint8_t busid)
{
    (void)busid;
    NVIC_DisableIRQ(L7_OTG_FS_IRQn);
    l7_usb_soft_disconnect();
}

uint32_t usbd_get_dwc2_gccfg_conf(uint32_t reg_base)
{
    USB_OTG_GlobalTypeDef *glb = (USB_OTG_GlobalTypeDef *)reg_base;

    glb->GOTGCTL |= USB_OTG_GOTGCTL_BVALOEN;
    glb->GOTGCTL |= USB_OTG_GOTGCTL_BVALOVAL;
    return USB_OTG_GCCFG_PWRDWN;
}

void usbd_dwc2_delay_ms(uint8_t ms)
{
    uint32_t count = (SystemCoreClock / 1000U) * (uint32_t)ms;

    while (count--) {
        __asm volatile("nop");
    }
}

void OTG_FS_IRQHandler(void)
{
    rt_interrupt_enter();
    USBD_IRQHandler(0);
    rt_interrupt_leave();
}
