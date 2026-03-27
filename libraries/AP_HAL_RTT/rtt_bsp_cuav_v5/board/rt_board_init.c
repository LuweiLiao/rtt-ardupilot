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

/* SPI Low-Level DMA driver (STM32F7 only) */
#ifdef SOC_SERIES_STM32F7
#include "drv_spi_lld.h"
#endif

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

/*
 * SPI LLD context for SPI1 (primary IMU bus on CUAV V5).
 * Registered before rt_components_board_init() so that rt_hw_spi_init()
 * (INIT_BOARD_EXPORT) finds it during rt_hw_spi_bus_init().
 *
 * Pin/DMA assignments from board.h:
 *   SPI1 RX: DMA2_Stream2, CHANNEL_3
 *   SPI1 TX: DMA2_Stream5, CHANNEL_3
 */
#ifdef SOC_SERIES_STM32F7
#if defined(BSP_USING_SPI1) && defined(BSP_SPI1_TX_USING_DMA) && defined(BSP_SPI1_RX_USING_DMA)
static spi_lld_bus_t s_spi1_lld = {
    .spi     = SPI1,
    .dma_rx  = SPI1_RX_DMA_INSTANCE,   /* DMA2_Stream2 */
    .dma_tx  = SPI1_TX_DMA_INSTANCE,   /* DMA2_Stream5 */
    .ch_rx   = SPI1_RX_DMA_CHANNEL,    /* DMA_CHANNEL_3 */
    .ch_tx   = SPI1_TX_DMA_CHANNEL,    /* DMA_CHANNEL_3 */
    .irq_rx  = SPI1_RX_DMA_IRQ,        /* DMA2_Stream2_IRQn */
    .irq_tx  = SPI1_TX_DMA_IRQ,        /* DMA2_Stream5_IRQn */
};

static void _spi_lld_board_init(void)
{
    spi_lld_register(&s_spi1_lld);
    spi_lld_bus_init(&s_spi1_lld);
}
#endif /* BSP_USING_SPI1 && DMA */
#endif /* SOC_SERIES_STM32F7 */

void rt_hw_board_init(void)
{
#ifdef FLASH_ORIGIN
    SCB->VTOR = FLASH_ORIGIN;
#else
    SCB->VTOR = 0x08008000U;
#endif

    _mpu_config();
    SCB_EnableICache();
    SCB_EnableDCache();

    if (HAL_Init() != HAL_OK) {
        while (1) { }
    }
    SystemClock_Config();
    rt_hw_pin_init();
    rt_hw_usart_init();
#ifdef RT_USING_HEAP
    rt_system_heap_init(HEAP_BEGIN, HEAP_END);
#endif

#ifdef RT_USING_CONSOLE
    rt_console_set_device(RT_CONSOLE_DEVICE_NAME);
#endif

    /* Register SPI LLD contexts before rt_components_board_init() so that
     * rt_hw_spi_init() (INIT_BOARD_EXPORT) finds them. */
#ifdef SOC_SERIES_STM32F7
#if defined(BSP_USING_SPI1) && defined(BSP_SPI1_TX_USING_DMA) && defined(BSP_SPI1_RX_USING_DMA)
    _spi_lld_board_init();
#endif
#endif

#ifdef RT_USING_COMPONENTS_INIT
    rt_components_board_init();
#endif
}

#if defined(BSP_USING_SPI) && defined(HAL_RTT_SPI_ATTACH_LIST)
static int _spi_device_board_init(void)
{
    _spi_device_init();
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

#ifdef BSP_USING_SDIO
#include <dfs_fs.h>

#define SD_POWER_PIN    GET_PIN(G, 7)   /* PG7 = VDD_3V3_SD_CARD_EN */

static int sd_card_mount(void)
{
    rt_pin_mode(SD_POWER_PIN, PIN_MODE_OUTPUT);
    rt_pin_write(SD_POWER_PIN, PIN_HIGH);
    rt_thread_mdelay(100);

    rt_device_t sd_dev = RT_NULL;
    for (int retry = 0; retry < 10; retry++) {
        sd_dev = rt_device_find("sd0");
        if (sd_dev != RT_NULL) break;
        rt_thread_mdelay(100);
    }

    if (sd_dev == RT_NULL) {
        rt_kprintf("[sd] sd0 device not found\n");
        return -1;
    }

    if (dfs_mount("sd0", "/sd", "elm", 0, 0) == 0) {
        rt_kprintf("[sd] mounted /sd ok\n");
    } else {
        rt_kprintf("[sd] mount /sd failed\n");
        return -1;
    }

    mkdir("/sd/APM", 0777);
    mkdir("/sd/APM/LOGS", 0777);
    mkdir("/sd/APM/TERRAIN", 0777);
    mkdir("/sd/APM/STORAGE", 0777);

    return 0;
}
INIT_ENV_EXPORT(sd_card_mount);
#endif
