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
extern void rt_hw_systick_init(void);

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
    __DMB();
    MPU->CTRL = 0;

    /*
     * Region 0: All SRAM (DTCM + SRAM1 + SRAM2), base 0x20000000, 1MB
     * Normal, Write-Through No Write-Allocate, NON-Shareable, Full Access.
     * S=0 forces local exclusive monitor for ldrex/strex, avoiding
     * STM32F7 AXI bus global-monitor PRECISERR on SRAM exclusive access.
     */
    MPU->RNR  = 0;
    MPU->RBAR = 0x20000000U;
    MPU->RASR = (0U  << 28) |  /* XN=0 */
                (3U  << 24) |  /* AP=011 full access */
                (0U  << 19) |  /* TEX=000 */
                (0U  << 18) |  /* S=0 non-shareable */
                (1U  << 17) |  /* C=1 cacheable */
                (0U  << 16) |  /* B=0 not bufferable */
                (0U  <<  8) |  /* SRD=0 */
                (19U <<  1) |  /* SIZE=19 → 1MB */
                (1U  <<  0);   /* ENABLE */

    /*
     * Region 1: Peripheral space (0x40000000, 512MB)
     * Device, non-cacheable, Shareable, Full Access, XN
     */
    MPU->RNR  = 1;
    MPU->RBAR = 0x40000000U;
    MPU->RASR = (1U  << 28) |  /* XN=1 no exec */
                (3U  << 24) |  /* AP=011 full access */
                (0U  << 19) |  /* TEX=000 */
                (1U  << 18) |  /* S=1 shareable */
                (0U  << 17) |  /* C=0 not cacheable */
                (1U  << 16) |  /* B=1 bufferable */
                (0U  <<  8) |  /* SRD=0 */
                (28U <<  1) |  /* SIZE=28 → 512MB */
                (1U  <<  0);   /* ENABLE */

    /*
     * Region 2: SDIO DMA buffer (cache_buf in .sram1_bss) — non-cacheable.
     * 16KB at 0x20020000, but only sub-regions 3-5 enabled (0x20021800–0x20022FFF)
     * via SRD mask. Higher region number overrides Region 0 for this range.
     */
    MPU->RNR  = 2;
    MPU->RBAR = 0x20020000U;
    MPU->RASR = (0U  << 28) |  /* XN=0 */
                (3U  << 24) |  /* AP=011 full access */
                (1U  << 19) |  /* TEX=001 normal non-cacheable */
                (0U  << 18) |  /* S=0 */
                (0U  << 17) |  /* C=0 */
                (0U  << 16) |  /* B=0 */
                (0xC7U << 8) | /* SRD=11000111: disable sub 0,1,2,6,7; enable 3,4,5 */
                (13U <<  1) |  /* SIZE=13 → 16KB */
                (1U  <<  0);   /* ENABLE */

    MPU->CTRL = MPU_CTRL_PRIVDEFENA_Msk | MPU_CTRL_ENABLE_Msk;
    __DSB();
    __ISB();
}

static void _fpu_context_init(void)
{
#if defined (__VFP_FP__) && !defined(__SOFTFP__)
    /*
     * Lua scripting mixes hard-float code with frequent RT-Thread context
     * switches. Disable lazy stacking so exception entry always materializes
     * the low FPU frame on the owning thread stack instead of deferring via
     * FPCAR/LSPACT, which has been triggering INVSTATE during mixed FPU/non-FPU
     * thread switches on CUAV V5 bring-up.
     */
    FPU->FPCCR |= FPU_FPCCR_ASPEN_Msk;
    FPU->FPCCR &= ~FPU_FPCCR_LSPEN_Msk;
    __DSB();
    __ISB();
#endif
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
    _fpu_context_init();
    SCB_EnableICache();
    SCB_EnableDCache();

    /* Minimal HAL_Init() equivalent — direct register operations */
    FLASH->ACR |= FLASH_ACR_ARTEN | FLASH_ACR_PRFTEN;
    NVIC_SetPriorityGrouping(3U);  /* PRIGROUP=3 → 4-bit preemption (same as HAL NVIC_PRIORITYGROUP_4) */

    SystemClock_Config();
    rt_hw_systick_init();
    rt_hw_pin_init();
    rt_hw_usart_init();

    /* VDD_3V3_SENSORS_EN = PE3, drive HIGH to power sensors */
    rt_pin_mode(GET_PIN(E, 3), PIN_MODE_OUTPUT);
    rt_pin_write(GET_PIN(E, 3), PIN_HIGH);

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

volatile int rtt_sd_mount_stage = 0;
volatile int rtt_sd_mount_result = -99;

static int sd_card_mount(void)
{
    rtt_sd_mount_stage = 1;
    rt_pin_mode(SD_POWER_PIN, PIN_MODE_OUTPUT);
    rt_pin_write(SD_POWER_PIN, PIN_HIGH);
    rt_thread_mdelay(200);

    rtt_sd_mount_stage = 2;
    rt_device_t sd_dev = RT_NULL;
    for (int retry = 0; retry < 30; retry++) {
        sd_dev = rt_device_find("sd0");
        if (sd_dev != RT_NULL) break;
        rt_thread_mdelay(200);
    }

    if (sd_dev == RT_NULL) {
        rtt_sd_mount_stage = -1;
        rt_kprintf("[sd] sd0 device not found after 6s\n");
        rtt_sd_mount_result = -1;
        return -1;
    }

    rtt_sd_mount_stage = 3;
    rt_thread_mdelay(500);

    rtt_sd_mount_stage = 4;
    int ret = dfs_mount("sd0", "/", "elm", 0, 0);
    if (ret == 0) {
        rtt_sd_mount_stage = 5;
        rt_kprintf("[sd] mounted / ok\n");
    } else {
        rtt_sd_mount_stage = -4;
        rt_kprintf("[sd] mount / failed (ret=%d errno=%d)\n", ret, rt_get_errno());
        rtt_sd_mount_result = -4;
        return -1;
    }

    mkdir("/APM", 0777);
    mkdir("/APM/LOGS", 0777);
    mkdir("/APM/TERRAIN", 0777);
    mkdir("/APM/STORAGE", 0777);

    rtt_sd_mount_stage = 10;
    rtt_sd_mount_result = 0;
    rt_kprintf("[sd] APM dirs created\n");
    return 0;
}
INIT_ENV_EXPORT(sd_card_mount);
#endif

/* ----------------------------------------------------------------
 *  True CPU idle measurement via DWT cycle counter + idle hook.
 *  rtt_cpu_idle_pct is updated every second; read via GDB or MAVLink.
 * ---------------------------------------------------------------- */
volatile uint32_t rtt_cpu_idle_pct = 0;
volatile uint32_t rtt_cpu_idle_cycles = 0;

static volatile uint32_t _idle_cycles_acc = 0;
static volatile uint32_t _idle_last_cyc = 0;
static volatile uint32_t _measure_start_cyc = 0;

static void _idle_hook(void)
{
    uint32_t now = DWT->CYCCNT;
    if (_idle_last_cyc != 0) {
        _idle_cycles_acc += (now - _idle_last_cyc);
    }
    _idle_last_cyc = now;
}

static void _cpu_measure_thread(void *arg)
{
    (void)arg;
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

    while (1) {
        _idle_cycles_acc = 0;
        _idle_last_cyc = 0;
        _measure_start_cyc = DWT->CYCCNT;
        rt_thread_mdelay(1000);
        uint32_t total = DWT->CYCCNT - _measure_start_cyc;
        uint32_t idle = _idle_cycles_acc;
        _idle_last_cyc = 0;
        rtt_cpu_idle_cycles = idle;
        if (total > 0) {
            rtt_cpu_idle_pct = (uint32_t)((uint64_t)idle * 100 / total);
        }
    }
}

static int _cpu_idle_monitor_init(void)
{
    rt_thread_idle_sethook(_idle_hook);
    rt_thread_t th = rt_thread_create("cpumon", _cpu_measure_thread,
                                      RT_NULL, 1024,
                                      RT_THREAD_PRIORITY_MAX - 1, 20);
    if (th) rt_thread_startup(th);
    return 0;
}
INIT_APP_EXPORT(_cpu_idle_monitor_init);

