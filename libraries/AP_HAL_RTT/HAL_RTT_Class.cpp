/*
 * ArduPilot + RT-Thread HAL - HAL class and driver instances.
 * run() 顺序：若 RT-Thread 未在 board 启动中初始化则 rt_hw_board_init /
 * rt_system_timer_init / rt_system_scheduler_init；创建主线程（入口 _main_loop_entry：
 * setup() 后 while(1) loop()）；最后 rt_system_scheduler_start()。
 * 需链接 librtthread；依赖符号：rt_hw_board_init, rt_system_timer_init,
 * rt_system_scheduler_init, rt_system_scheduler_start, rt_thread_create 等。
 */

#include <AP_HAL/AP_HAL.h>
#include "HAL_RTT_Class.h"
#include "Scheduler.h"
#include "UARTDriver.h"
/* DEBUG: setup completion stage tracker */
volatile uint32_t rtt_dbg_setup_trace = 0;

#include <stm32f7xx.h>
#include "hal_usb_lld_rtt.h"
#include "rtt_ctl_telemetry.h"
#include "rtt_dbg_bkp.h"
#include <rtthread.h>
#include "RCInput.h"

/* GPIO 寄存器操作所需 */
#include <stm32f7xx.h>
#include "RCOutput.h"
#include "GPIO.h"
#include "Storage.h"
#include "SPIDevice.h"
#include "AnalogIn.h"
#include "Util.h"
#include "I2CDevice.h"

extern uint32_t rtt_boot_rcc_csr;
#if HAL_WITH_IO_MCU
#include <AP_IOMCU/AP_IOMCU.h>
#endif
#include "SPIDeviceManager.h"
#include "shared_dma.h"
#include "I2CDeviceManager.h"
#include "WSPIDevice.h"
#include <AP_HAL/OpticalFlow.h>
#include "Flash.h"
#if HAL_WITH_DSP
#include "DSP.h"
#endif
#include <stm32f7xx.h>
#if defined(RT_USING_FINSH) && defined(MSH_USING_BUILT_IN_COMMANDS)
#include <finsh.h>
#endif

#ifndef DEFAULT_SERIAL0_BAUD
#define SERIAL0_BAUD 115200
#else
#define SERIAL0_BAUD DEFAULT_SERIAL0_BAUD
#endif
#if AP_SIM_ENABLED && CONFIG_HAL_BOARD != HAL_BOARD_SITL
#include <AP_HAL/SIMState.h>
#endif

extern "C" void rt_hw_board_init(void);

static void rtt_gpio_write_pin_value(uint8_t pin, bool high)
{
    const uint8_t port = pin >> 4;
    const uint8_t bit = pin & 0x0F;
    if (port > 8) {
        return;
    }
    GPIO_TypeDef *gpio = (GPIO_TypeDef *)(GPIOA_BASE + ((uint32_t)port << 10U));
    RCC->AHB1ENR |= (1U << port);
    (void)RCC->AHB1ENR;
    gpio->MODER = (gpio->MODER & ~(3UL << (bit * 2))) | (1UL << (bit * 2));
    gpio->OTYPER &= ~(1UL << bit);
    gpio->OSPEEDR = (gpio->OSPEEDR & ~(3UL << (bit * 2))) | (2UL << (bit * 2));
    if (high) {
        gpio->BSRR = 1UL << bit;
    } else {
        gpio->BSRR = 1UL << (bit + 16U);
    }
    __DSB();
}

static void rtt_enable_peripheral_power_rails(const AP_HAL::HAL& hal_ref)
{
    /*
     * [Cybernetics Ch.4] Closed-loop alignment with ChibiOS
     * peripheral_power_enable(): nVDD_* rails are active-low and must be
     * driven LOW after the initial bootloader/radio settle delay.
     */
    hal_ref.scheduler->delay(100);
#ifdef HAL_GPIO_nVDD_5V_PERIPH_EN_VALUE
    rtt_gpio_write_pin_value(HAL_GPIO_nVDD_5V_PERIPH_EN_VALUE, false);
#endif
#ifdef HAL_GPIO_nVDD_5V_HIPOWER_EN_VALUE
    rtt_gpio_write_pin_value(HAL_GPIO_nVDD_5V_HIPOWER_EN_VALUE, false);
#endif
#ifdef HAL_GPIO_VDD_5V_PERIPH_EN_VALUE
    rtt_gpio_write_pin_value(HAL_GPIO_VDD_5V_PERIPH_EN_VALUE, true);
#endif
#ifdef HAL_GPIO_VDD_5V_HIPOWER_EN_VALUE
    rtt_gpio_write_pin_value(HAL_GPIO_VDD_5V_HIPOWER_EN_VALUE, true);
#endif
    hal_ref.scheduler->delay(20);
}

namespace RTT
{
class OpticalFlowStub : public AP_HAL::OpticalFlow
{
public:
    void init() override {}
    bool read(Data_Frame& frame) override { (void)frame; return false; }
    void push_gyro(float gyro_x, float gyro_y, float dt) override { (void)gyro_x; (void)gyro_y; (void)dt; }
    void push_gyro_bias(float gyro_bias_x, float gyro_bias_y) override { (void)gyro_bias_x; (void)gyro_bias_y; }
};
}

static RTT::UARTDriver cons(0);
static RTT::UARTDriver serial1Driver(1);
static RTT::UARTDriver serial2Driver(2);
static RTT::UARTDriver serial3Driver(3);
static RTT::UARTDriver serial4Driver(4);
static RTT::UARTDriver serial5Driver(5);
static RTT::UARTDriver serial6Driver(6);
static RTT::UARTDriver serial7Driver(7);
static RTT::UARTDriver serial8Driver(8);
static RTT::UARTDriver serial9Driver(9);

static RTT::I2CDeviceManager i2cDeviceManager;
static RTT::SPIDeviceManager spiDeviceManager;
static RTT::WSPIDeviceManager wspiDeviceManager;
RTT::WSPIDeviceManager *hal_wspi = &wspiDeviceManager;
static RTT::AnalogIn analogIn;
static RTT::Storage storageDriver;
static RTT::GPIO gpioDriver;
static RTT::RCInput rcinDriver;
static RTT::RCOutput rcoutDriver;
RTT::RCOutput *rtt_rcout_instance = &rcoutDriver;
static RTT::Scheduler schedulerInstance;
static RTT::Util utilInstance;
static RTT::OpticalFlowStub opticalFlowDriver;
static RTT::Flash flashDriver;
#if HAL_WITH_DSP
static RTT::DSP dspDriver;
#endif
#if HAL_WITH_IO_MCU
// IOMCU UART mirrors ChibiOS: uart_io is the generated IOMCU serial slot,
// not a second driver instance with a hard-coded HAL index.
#if HAL_UART_IOMCU_IDX == 7
#define RTT_IOMCU_SERIAL_DRIVER serial7Driver
#elif HAL_UART_IOMCU_IDX == 8
#define RTT_IOMCU_SERIAL_DRIVER serial8Driver
#elif HAL_UART_IOMCU_IDX == 9
#define RTT_IOMCU_SERIAL_DRIVER serial9Driver
#else
#error "Unsupported HAL_UART_IOMCU_IDX for AP_HAL_RTT"
#endif
AP_IOMCU iomcu(RTT_IOMCU_SERIAL_DRIVER);

RTT::UARTDriver *get_rtt_iomcu_uart(void) { return &RTT_IOMCU_SERIAL_DRIVER; }
#endif
#if AP_SIM_ENABLED && CONFIG_HAL_BOARD != HAL_BOARD_SITL
static AP_HAL::SIMState xsimstate;
#endif

extern const AP_HAL::HAL& hal;

namespace RTT { WSPIDeviceManager *hal_wspi = nullptr; }

HAL_RTT::HAL_RTT() :
    AP_HAL::HAL(
        &cons,
        &serial1Driver,
        &serial2Driver,
        &serial3Driver,
        &serial4Driver,
        &serial5Driver,
        &serial6Driver,
        &serial7Driver,
        &serial8Driver,
        &serial9Driver,
        &i2cDeviceManager,
        &spiDeviceManager,
        &wspiDeviceManager,
        &analogIn,
        &storageDriver,
        &cons,
        &gpioDriver,
        &rcinDriver,
        &rcoutDriver,
        &schedulerInstance,
        &utilInstance,
        &opticalFlowDriver,
        &flashDriver,
#if HAL_WITH_DSP
        &dspDriver,
#endif
#if AP_SIM_ENABLED && CONFIG_HAL_BOARD != HAL_BOARD_SITL
        &xsimstate,
#endif
        nullptr
    )
{}

/* Debug flags to track initialization */
volatile uint32_t rtt_dbg_main_thread_entered = 0;
volatile uint32_t rtt_dbg_components_init_done = 0;
volatile uint32_t rtt_dbg_main_called = 0;
volatile uint32_t rtt_dbg_hal_run_called = 0xDEADBEEF;
volatile uint32_t rtt_dbg_main_loop_entry_called = 0xCAFEBABE;
volatile uint32_t rtt_dbg_main_loop_iterations = 0;
volatile uint32_t rtt_dbg_loop_time_us = 0;
volatile uint32_t rtt_dbg_loop_time_max_us = 0;
volatile uint32_t rtt_dbg_loop_time_min_us = 0xFFFFFFFF;
volatile uint32_t rtt_dbg_work_time_us = 0;
volatile uint32_t rtt_dbg_work_time_max_us = 0;
volatile uint32_t rtt_dbg_overrun_count = 0;
volatile uint32_t rtt_dbg_main_loop_service_yield_count = 0;
volatile uint32_t rtt_dbg_fast_loop_count = 0;
volatile uint32_t rtt_dbg_boost_calls_per_loop = 0;
volatile uint32_t rtt_dbg_boost_total_us_per_loop = 0;
volatile uint32_t rtt_dbg_setup_stage = 0;
volatile uint32_t rtt_dbg_spi_ok = 0;
volatile uint32_t rtt_dbg_wait_sample_us = 0;
volatile uint32_t rtt_dbg_run_tasks_us = 0;
volatile uint32_t rtt_dbg_extra_loop = 0;
volatile uint32_t rtt_dbg_ins_stage = 0;
volatile uint32_t rtt_dbg_ins_loop_rate = 0;
volatile uint32_t rtt_dbg_ins_backend_count = 0;
volatile uint32_t rtt_dbg_ins_gyro_count = 0;
volatile uint32_t rtt_dbg_ins_accel_count = 0;
volatile uint32_t rtt_dbg_ins_wait_calls = 0;
volatile uint32_t rtt_dbg_ins_wait_counter = 0;
volatile uint32_t rtt_dbg_ins_wait_limit = 0;
volatile uint32_t rtt_dbg_ins_gyro_avail_mask = 0;
volatile uint32_t rtt_dbg_ins_accel_avail_mask = 0;
volatile uint32_t rtt_dbg_ins_gyro_wait_mask = 0;
volatile uint32_t rtt_dbg_ins_accel_wait_mask = 0;
volatile uint32_t rtt_dbg_ins_new_gyro_mask = 0;
volatile uint32_t rtt_dbg_ins_new_accel_mask = 0;
volatile uint32_t rtt_dbg_ins_cal_j = 0;
volatile uint32_t rtt_dbg_ins_cal_i = 0;
volatile uint32_t rtt_dbg_ins_cal_converged = 0;
extern "C" volatile uint32_t rtt_cpu_idle_pct;

#ifndef HAL_RTT_UART7_PERIODIC_TELEMETRY
#define HAL_RTT_UART7_PERIODIC_TELEMETRY 0
#endif

#if defined(RT_USING_FINSH) && defined(MSH_USING_BUILT_IN_COMMANDS)
static void ap_rate(void)
{
    rt_kprintf("cpu_idle=%lu%% load=%lu%% loop_us=%lu loop_hz=%lu\n",
               (unsigned long)rtt_cpu_idle_pct,
               (unsigned long)(100U - ((rtt_cpu_idle_pct > 100U) ? 100U : rtt_cpu_idle_pct)),
               (unsigned long)rtt_dbg_loop_time_us,
               (unsigned long)(rtt_dbg_loop_time_us > 0 ? (1000000U / rtt_dbg_loop_time_us) : 0));
    rt_kprintf("overrun=%lu iterations=%lu\n",
               (unsigned long)rtt_dbg_overrun_count,
               (unsigned long)rtt_dbg_main_loop_iterations);
}
MSH_CMD_EXPORT(ap_rate, show ArduPilot CPU and loop timing stats);
#endif

struct main_loop_arg {
    RTT::Scheduler* sched;
    AP_HAL::HAL::Callbacks* callbacks;
};

static void _main_loop_entry(void* arg)
{
    rtt_dbg_main_loop_entry_called = 0x12345678;  /* Magic number to verify we're here */
    rtt_ctl_print_snapshot();

    main_loop_arg* a = (main_loop_arg*)arg;
    a->sched->set_main_thread_id(rt_thread_self());

    /*
     * ChibiOS setup priority discipline — mirrors HAL_ChibiOS_Class.cpp:265, 317:
     *
     *   1. Set main priority          → APM_MAIN_PRIORITY (=5)
     *      (ChibiOS: chThdSetPriority(APM_MAIN_PRIORITY), L236)
     *   2. Drop to startup priority   → APM_RTT_STARTUP_PRIORITY (=15)
     *      (ChibiOS: hal_chibios_set_priority(APM_STARTUP_PRIORITY), L265)
     *   3. Signal hal_initialized     → timer/SPI threads start running
     *      (ChibiOS: schedulerInstance.hal_initialized(), L273)
     *   4. Run setup() at low priority  → sensor init loops get CPU time
     *   5. Restore main thread priority → APM_MAIN_PRIORITY (=5)
     *      (ChibiOS: chThdSetPriority(APM_MAIN_PRIORITY), L317)
     *
     * Dropping priority during setup lets timer (4), SPI (4), UART (6)
     * and other service threads preempt the main init, preventing sensor
     * read timeouts and IOMCU upload stalls.
     */
    {
        rt_thread_t self = rt_thread_self();
        rt_uint8_t main_prio = (rt_uint8_t)APM_RTT_MAIN_PRIORITY;
        rt_thread_control(self, RT_THREAD_CTRL_CHANGE_PRIORITY, &main_prio);
    }

    /* Drop to startup priority — below timer/SPI(4), UART(6), above IO(18) */
    {
        rt_thread_t self = rt_thread_self();
        rt_uint8_t startup_prio = (rt_uint8_t)APM_RTT_STARTUP_PRIORITY;
        rt_thread_control(self, RT_THREAD_CTRL_CHANGE_PRIORITY, &startup_prio);
    }

    /* Signal hal_initialized — timer/SPI/UART threads can now run freely */
    a->sched->hal_initialized();

    a->callbacks->setup();
    a->sched->set_system_initialized();

    /* Restore main priority for the main loop */
    {
        rt_thread_t self = rt_thread_self();
        rt_uint8_t main_prio = (rt_uint8_t)APM_RTT_MAIN_PRIORITY;
        rt_thread_control(self, RT_THREAD_CTRL_CHANGE_PRIORITY, &main_prio);
    }

    rtt_dbg_hal_run_called = 0x11111111;  /* Second magic number after setup */

    uint32_t last_loop_us = AP_HAL::micros();
    uint8_t service_yield_counter = 0;
    for (;;) {
        rtt_dbg_boost_calls_per_loop = 0;
        rtt_dbg_boost_total_us_per_loop = 0;
        uint32_t pre_loop_us = AP_HAL::micros();
        /* Poll USB bus events (enumeration, control transfers, CDC data RX) */
        usb_lld_poll_rtt();
        a->callbacks->loop();
        /* GCS comms run via AP_Scheduler (400 Hz update_send/update_receive),
         * same as ChibiOS — do NOT call call_delay_cb() every loop iteration.
         * scheduler_delay_callback remains available through delay() during
         * long init waits (register_delay_callback, min 5 ms). */
        uint32_t post_loop_us = AP_HAL::micros();
        uint32_t work = post_loop_us - pre_loop_us;
        rtt_dbg_work_time_us = work;
        if (work > rtt_dbg_work_time_max_us) rtt_dbg_work_time_max_us = work;
        if (work > 2500) rtt_dbg_overrun_count++;
        if (!schedulerInstance.check_called_boost()) {
            /*
             * [Cybernetics Ch.4] Closed-loop: RTT sub-200us delays are DWT
             * busy-waits, unlike ChibiOS chThdSleep() calls.  Periodically
             * yield a real RT-Thread tick so lower-priority IO/storage/logger
             * threads can update heartbeats without promoting them above the
             * 400Hz main loop budget.
             */
            if (++service_yield_counter >= 32) {
                service_yield_counter = 0;
                rtt_dbg_main_loop_service_yield_count++;
                rt_thread_mdelay(1);
            } else {
                hal.scheduler->delay_microseconds(50);
            }
        }
        schedulerInstance.watchdog_pat();
        uint32_t now_us = AP_HAL::micros();
        uint32_t dt = now_us - last_loop_us;
        last_loop_us = now_us;
        rtt_dbg_loop_time_us = dt;
        if (dt > rtt_dbg_loop_time_max_us) rtt_dbg_loop_time_max_us = dt;
        if (dt < rtt_dbg_loop_time_min_us && dt > 0) rtt_dbg_loop_time_min_us = dt;
        if (dt < 1500) rtt_dbg_fast_loop_count++;
        rtt_dbg_main_loop_iterations++;
#if HAL_RTT_UART7_PERIODIC_TELEMETRY
        rtt_ctl_telemetry_tick(1000);
#endif
    }
}

/* Declared in system.cpp — IWDG init + feed */
extern "C" void ap_rtt_iwdg_init(void);

void HAL_RTT::run(int argc, char * const argv[], Callbacks* callbacks) const
{
    /* Feed IWDG before anything else — hardware IWDG is active from reset with
     * ~512ms timeout (FLASH_OPTCR_IWDG_SW=0 on CUAV V5).  Must run
     * before debug markers or init that can exceed the remaining margin. */
    ap_rtt_iwdg_init();
#if AP_HAL_SHARED_DMA_ENABLED
    RTT::Shared_DMA::init();
#endif
    rtt_dbg_hal_run_called = 0xAAAAAAAA;
    rtt_ctl_print_snapshot();
    rt_kprintf("HAL_RTT::run\n");

    /* Strategic feed. SysTick feeds every 1ms from here on, but
     * ap_rtt_iwdg_init() may have consumed margin in the fixed hardware
     * watchdog window. */
    *(volatile uint32_t *)0x40003000 = 0xAAAA;

    /* Save reset reason before RMVF clear — mirrors ChibiOS board.c
     * stm32_watchdog_save_reason() / stm32_watchdog_clear_reason(). */
    rtt_boot_rcc_csr = RCC->CSR;
    rtt_dbg_bkp_restore_prev_fault();
    rtt_ctl_print_snapshot();
    RCC->CSR |= RCC_CSR_RMVF;

#ifdef HAL_I2C_CLEAR_BUS
    RTT::I2CDevice::clear_all_buses();
#endif

    /* Tell bootloader the app is alive — write RTC_BOOT_FWOK to backup
     * register 0.  Mirrors ChibiOS stm32_util.c set_fast_reboot().
     * PX4 bootloader on CUAV V5 reads BKP0R on startup to detect
     * app-crashed-from-previous-boot; writing FWOK here prevents
     * false-positive erase on subsequent power cycles. */
    {
        rtt_dbg_bkp_enable_domain();
        RTC->BKP0R = 0xb0093a26UL;  /* RTC_BOOT_FWOK */
        __DSB();
    }

    (void)argc;
    (void)argv;

    ((RTT::Scheduler*)scheduler)->set_callbacks(callbacks);

    scheduler->init();

    /*
     * Init GPIO output pins (sensor power rails, etc.) BEFORE any
     * SPI/I2C device communication.  This powers on VDD_3V3_SENSORS
     * and other rails so IMU/Baro/USD card etc. respond on the bus.
     */
    hal.gpio->init();
    rtt_enable_peripheral_power_rails(hal);

    /* SPI1 GPIO MODER — gpio->init() may clobber PG11/PA6/PD7 AF mode.
     * PG11=SCK, PA6=MISO, PD7=MOSI (fmuv5/CUAV V5 reference). */
#ifdef STM32F7
    {
        /* PG11: MODER bit 23:22 = 10 (AF), AFR1 bit 15:12 = 0101 (AF5) */
        GPIOG->MODER = (GPIOG->MODER & ~(3U << 22)) | (2U << 22);
        GPIOG->AFR[1] = (GPIOG->AFR[1] & ~(0xFU << 12)) | (5U << 12);
        /* PA6: MODER bit 13:12 = 10 (AF), AFR0 bit 27:24 = 0101 (AF5) */
        GPIOA->MODER = (GPIOA->MODER & ~(3U << 12)) | (2U << 12);
        GPIOA->AFR[0] = (GPIOA->AFR[0] & ~(0xFU << 24)) | (5U << 24);
        /* PD7: MODER bit 15:14 = 10 (AF), AFR0 bit 31:28 = 0101 (AF5) */
        GPIOD->MODER = (GPIOD->MODER & ~(3U << 14)) | (2U << 14);
        GPIOD->AFR[0] = (GPIOD->AFR[0] & ~(0xFU << 28)) | (5U << 28);
    }
#endif

    /* Initialize DWC2 USB in device mode (self-enumeration, no CherryUSB) */
    rtt_dbg_setup_trace = 30;
    usb_lld_init_rtt();
    rtt_dbg_setup_trace = 31;

    /* Poll immediately — USB bus reset from host happens right after device
     * connects (microseconds), but usb_lld_init_rtt clears GINTSTS. If we
     * don't poll here, the USBRST event is lost forever and USB never
     * enumerates.  Subsequent polls happen in the main loop. */
    usb_lld_poll_rtt();
    rtt_ctl_print_snapshot();

    hal.serial(0)->begin(SERIAL0_BAUD);
    rtt_dbg_setup_trace = 32;
    hal.analogin->init();

    /* Pre-initialize SPI bus 1 DeviceBus — warm up the lazy semaphore init
     * from known-good context (main init, interrupts on, scheduler running).
     * Without this warmup, the first SPIDevice::transfer (during IMU probe
     * in setup) triggers rt_mutex_init which deadlocks with the RT-Thread
     * spinlock when the scheduler is still stabilizing. */
    {
        RTT::DeviceBus *spibus = RTT::DeviceBus::get_bus(1, APM_RTT_SPI_PRIORITY);
        /* Take+give to force lazy mutex init NOW, not during SPIDevice::transfer */
        const bool semaphore_taken = spibus->semaphore.take_nonblocking();
        if (semaphore_taken) {
            spibus->semaphore.give();
        }
    }
    /* SPI2 (FRAM) — CMSIS GPIO/mutex before setup() opens storage */
    RTT::spi_cmsis_prepare_bus(2);

    rtt_dbg_hal_run_called = 0xBBBBBBBB;

    main_loop_arg arg;
    arg.sched = (RTT::Scheduler*)scheduler;
    arg.callbacks = callbacks;
    _main_loop_entry(&arg);
}

void AP_HAL::init()
{
}

static HAL_RTT hal_rtt;

const AP_HAL::HAL& AP_HAL::get_HAL()
{
    return hal_rtt;
}

AP_HAL::HAL& AP_HAL::get_HAL_mutable()
{
    return hal_rtt;
}
