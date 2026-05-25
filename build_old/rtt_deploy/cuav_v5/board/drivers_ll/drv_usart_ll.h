/*
 * LL USART driver for STM32F767.
 * Polled TX, interrupt RX, direct register access.
 * Designed as a drop-in for AP_HAL_RTT UART paths in Phase 4.
 */
#ifndef __DRV_USART_LL_H__
#define __DRV_USART_LL_H__

#include <stdint.h>
#include "stm32f7xx.h"

typedef struct {
    USART_TypeDef *Instance;
    uint32_t      baudrate;
    uint8_t       tx_port_idx;   /* 0=A, 1=B, ..., GPIO_LL encoding */
    uint8_t       tx_pin_no;     /* 0..15 */
    uint8_t       tx_af;         /* Alternate function number */
    uint8_t       rx_port_idx;
    uint8_t       rx_pin_no;
    uint8_t       rx_af;
    IRQn_Type     irqn;
} usart_ll_config_t;

/* USART3 (PD8/PD9 AF7) and UART7 (PE8/PF6 AF8) predefined configs */
extern const usart_ll_config_t usart3_ll_cfg;
extern const usart_ll_config_t uart7_ll_cfg;

void     usart_ll_init(const usart_ll_config_t *cfg);
void     usart_ll_putc(USART_TypeDef *uart, uint8_t ch);
int      usart_ll_getc_nb(USART_TypeDef *uart);
int      usart_ll_tx_poll(USART_TypeDef *uart, const uint8_t *buf, int len);
uint32_t usart_ll_get_pclk(USART_TypeDef *uart);

#endif /* __DRV_USART_LL_H__ */
