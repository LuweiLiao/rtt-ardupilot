/*
 * RT-Thread board init for AP_HAL_RTT.
 * SPI device attach is driven by HAL_RTT_SPI_ATTACH_LIST from hwdef.h.
 */
#include <rtthread.h>
#include "board.h"
#include "drv_gpio.h"

#ifdef BSP_USING_SPI
#include "drv_spi.h"
#endif

#include "hwdef.h"

extern int rt_hw_pin_init(void);
extern int rt_hw_usart_init(void);
extern void __libc_init_array(void);

#if defined(BSP_USING_SPI) && defined(HAL_RTT_SPI_ATTACH_LIST)
struct spi_attach_entry {
    const char *bus_name;
    const char *dev_name;
    rt_base_t cs_pin;
};

static const struct spi_attach_entry _spi_attach_table[] = {
    HAL_RTT_SPI_ATTACH_LIST
};

static void _spi_device_init(void)
{
    for (unsigned i = 0; i < sizeof(_spi_attach_table) / sizeof(_spi_attach_table[0]); i++) {
        rt_hw_spi_device_attach(_spi_attach_table[i].bus_name,
                                _spi_attach_table[i].dev_name,
                                _spi_attach_table[i].cs_pin);
    }
}
#endif

static void _mpu_config(void)
{
    HAL_MPU_Disable();

    MPU_Region_InitTypeDef mpu;

    /*
     * Region 0: SRAM1+SRAM2 (0x20020000, 384KB)
     * Normal memory, Write-Through, No Write-Allocate, Shareable
     * DMA reads from RAM directly; CPU reads through D-Cache.
     * Write-through ensures invalidate never discards dirty data.
     */
    mpu.Enable           = MPU_REGION_ENABLE;
    mpu.Number           = MPU_REGION_NUMBER0;
    mpu.BaseAddress      = 0x20020000;
    mpu.Size             = MPU_REGION_SIZE_512KB;
    mpu.SubRegionDisable = 0x00;
    mpu.TypeExtField     = MPU_TEX_LEVEL0;
    mpu.AccessPermission = MPU_REGION_FULL_ACCESS;
    mpu.DisableExec      = MPU_INSTRUCTION_ACCESS_ENABLE;
    mpu.IsShareable      = MPU_ACCESS_SHAREABLE;
    mpu.IsCacheable      = MPU_ACCESS_CACHEABLE;
    mpu.IsBufferable     = MPU_ACCESS_NOT_BUFFERABLE;
    HAL_MPU_ConfigRegion(&mpu);

    /*
     * Region 1: Peripheral space (0x40000000, 512MB)
     * Device memory, non-cacheable, non-bufferable
     */
    mpu.Enable           = MPU_REGION_ENABLE;
    mpu.Number           = MPU_REGION_NUMBER1;
    mpu.BaseAddress      = 0x40000000;
    mpu.Size             = MPU_REGION_SIZE_512MB;
    mpu.SubRegionDisable = 0x00;
    mpu.TypeExtField     = MPU_TEX_LEVEL0;
    mpu.AccessPermission = MPU_REGION_FULL_ACCESS;
    mpu.DisableExec      = MPU_INSTRUCTION_ACCESS_DISABLE;
    mpu.IsShareable      = MPU_ACCESS_SHAREABLE;
    mpu.IsCacheable      = MPU_ACCESS_NOT_CACHEABLE;
    mpu.IsBufferable     = MPU_ACCESS_BUFFERABLE;
    HAL_MPU_ConfigRegion(&mpu);

    HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT);
}

void rt_hw_board_init(void)
{
#ifdef FLASH_ORIGIN
    SCB->VTOR = FLASH_ORIGIN;
#else
    SCB->VTOR = 0x08008000U;
#endif

    _mpu_config();
    SCB_EnableICache();
    // SCB_EnableDCache();  // temporarily disabled to diagnose HardFault

    if (HAL_Init() != HAL_OK) {
        while (1) { }
    }
    SystemClock_Config();
    rt_hw_pin_init();
    rt_hw_usart_init();
#ifdef RT_USING_HEAP
    rt_system_heap_init(HEAP_BEGIN, HEAP_END);
#endif
}

#ifdef BSP_USING_SPI
extern int rt_hw_spi_init(void);

static int _spi_device_board_init(void)
{
    rt_hw_spi_init();
#ifdef HAL_RTT_SPI_ATTACH_LIST
    _spi_device_init();
#endif
    return 0;
}
INIT_PREV_EXPORT(_spi_device_board_init);
#endif

static int rtt_run_cpp_ctors(void)
{
    __libc_init_array();
    return 0;
}
INIT_COMPONENT_EXPORT(rtt_run_cpp_ctors);
