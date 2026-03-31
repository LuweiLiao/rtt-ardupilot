/*
 * LL SPI driver for STM32F767 — polled master mode.
 * Direct register access, no HAL dependency.
 */
#ifndef __DRV_SPI_LL_H__
#define __DRV_SPI_LL_H__

#include <stdint.h>
#include "stm32f7xx.h"

typedef struct {
    SPI_TypeDef *Instance;
    uint8_t sck_port_idx, sck_pin_no;
    uint8_t miso_port_idx, miso_pin_no;
    uint8_t mosi_port_idx, mosi_pin_no;
    uint8_t af;        /* GPIO alternate function number */
    uint8_t mode;      /* SPI mode 0-3 (bit1=CPOL, bit0=CPHA) */
    uint8_t prescaler; /* CR1.BR[2:0]: 0=/2 .. 7=/256 */
} spi_ll_config_t;

extern const spi_ll_config_t spi1_ll_cfg;
extern const spi_ll_config_t spi4_ll_cfg;

void spi_ll_init(const spi_ll_config_t *cfg);
void spi_ll_set_speed(SPI_TypeDef *spi, uint8_t prescaler);
int  spi_ll_xfer_poll(SPI_TypeDef *spi, const uint8_t *tx, uint8_t *rx, uint16_t len);
uint32_t spi_ll_get_pclk(SPI_TypeDef *spi);

#endif /* __DRV_SPI_LL_H__ */
