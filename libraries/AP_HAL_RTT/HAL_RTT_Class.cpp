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
#include "hal_usb_lld_rtt.h"
#include <rtthread.h>
#include "RCInput.h"

/* GPIO 寄存器操作所需 */
#include <stm32f7xx.h>
#include "RCOutput.h"
#include "GPIO.h"
#include "Storage.h"
#include "AnalogIn.h"
#include "Util.h"
#if HAL_WITH_IO_MCU
#include <AP_IOMCU/AP_IOMCU.h>
#endif
#include "SPIDeviceManager.h"
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
// IOMCU UART — maps to HAL_UART_IOMCU_IDX=7 (UART8)
// This driver is NOT in the HAL serial array, so Scheduler must tick it
// separately via get_rtt_iomcu_uart().
static RTT::UARTDriver ioUartDriver(HAL_UART_IOMCU_IDX);
AP_IOMCU iomcu(ioUartDriver);

RTT::UARTDriver *get_rtt_iomcu_uart(void) { return &ioUartDriver; }
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
volatile uint32_t rtt_dbg_hal_run_called = 0xDEADBEEF;
volatile uint32_t rtt_dbg_main_loop_entry_called = 0xCAFEBABE;
volatile uint32_t rtt_dbg_main_loop_iterations = 0;
volatile uint32_t rtt_dbg_loop_time_us = 0;
volatile uint32_t rtt_dbg_loop_time_max_us = 0;
volatile uint32_t rtt_dbg_loop_time_min_us = 0xFFFFFFFF;
volatile uint32_t rtt_dbg_work_time_us = 0;
volatile uint32_t rtt_dbg_work_time_max_us = 0;
volatile uint32_t rtt_dbg_overrun_count = 0;
volatile uint32_t rtt_dbg_fast_loop_count = 0;
volatile uint32_t rtt_dbg_boost_calls_per_loop = 0;
volatile uint32_t rtt_dbg_boost_total_us_per_loop = 0;
volatile uint32_t rtt_dbg_wait_sample_us = 0;
volatile uint32_t rtt_dbg_run_tasks_us = 0;
volatile uint32_t rtt_dbg_extra_loop = 0;
extern "C" volatile uint32_t rtt_cpu_idle_pct;

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
    for (;;) {
        rtt_dbg_boost_calls_per_loop = 0;
        rtt_dbg_boost_total_us_per_loop = 0;
        uint32_t pre_loop_us = AP_HAL::micros();
        /* Poll USB bus events (enumeration, control transfers, CDC data RX) */
        usb_lld_poll_rtt();
        a->callbacks->loop();
        /* Call delay callbacks after loop() completes.
         * On ChibiOS, call_delay_cb() is called inside wait_for_sample() → delay(),
         * but RTT's delay_microseconds_boost() bypasses delay(), so we must
         * call it explicitly. Placing it after loop() ensures scheduler tasks
         * have already run and time budgets are reset. */
        a->sched->call_delay_cb();
        uint32_t post_loop_us = AP_HAL::micros();
        uint32_t work = post_loop_us - pre_loop_us;
        rtt_dbg_work_time_us = work;
        if (work > rtt_dbg_work_time_max_us) rtt_dbg_work_time_max_us = work;
        if (work > 2500) rtt_dbg_overrun_count++;
        if (!schedulerInstance.check_called_boost()) {
            hal.scheduler->delay_microseconds(50);
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
    }
}

/* Declared in system.cpp — IWDG init + feed */
extern "C" void ap_rtt_iwdg_init(void);

void HAL_RTT::run(int argc, char * const argv[], Callbacks* callbacks) const
{
    rtt_dbg_hal_run_called = 0xAAAAAAAA;

    /* Feed + reconfigure IWDG IMMEDIATELY — hardware IWDG is active from reset
     * with ~512ms timeout because FLASH_OPTCR_IWDG_SW=0 on CUAV V5.
     * This must happen before any long-running init, timer, or USB enumeration.
     * ap_rtt_iwdg_init() reconfigures timeout to ~10s and feeds the counter. */
    ap_rtt_iwdg_init();

    /* Clear sticky reset flags (RCC_CSR RMVF) — mirrors ChibiOS __late_init()
     * stm32_watchdog_clear_reason(). Prevents was_watchdog_reset() from
     * falsely returning true from a previous boot's RCC_CSR residue. */
    RCC->CSR |= RCC_CSR_RMVF;

    /* Tell bootloader the app is alive — write RTC_BOOT_FWOK to backup
     * register 0.  Mirrors ChibiOS stm32_util.c set_fast_reboot().
     * PX4 bootloader on CUAV V5 reads BKP0R on startup to detect
     * app-crashed-from-previous-boot; writing FWOK here prevents
     * false-positive erase on subsequent power cycles. */
    {
        /* Enable PWR + backup domain write access */
        RCC->APB1ENR |= RCC_APB1ENR_PWREN;
        (void)RCC->APB1ENR;
        if ((RCC->BDCR & RCC_BDCR_RTCEN) == 0) {
            RCC->BDCR |= RCC_BDCR_RTCSEL_0;  /* LSE default */
            RCC->BDCR |= RCC_BDCR_RTCEN;
        }
        PWR->CR1 |= PWR_CR1_DBP;          /* F7: CR1 not CR, PWR_CR1_DBP not PWR_CR_DBP */
        __DSB();
        *(volatile uint32_t *)0x40002850UL = 0xb0093a26UL;  /* RTC_BOOT_FWOK */
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
    usb_lld_init_rtt();

    hal.serial(0)->begin(SERIAL0_BAUD);
    hal.analogin->init();

    /* Pre-initialize SPI bus 1 DeviceBus — warm up the lazy semaphore init
     * from known-good context (main init, interrupts on, scheduler running).
     * Without this warmup, the first SPIDevice::transfer (during IMU probe
     * in setup) triggers rt_mutex_init which deadlocks with the RT-Thread
     * spinlock when the scheduler is still stabilizing. */
    {
        RTT::DeviceBus *spibus = RTT::DeviceBus::get_bus(1, APM_RTT_SPI_PRIORITY);
        /* Take+give to force lazy mutex init NOW, not during SPIDevice::transfer */
        spibus->semaphore.take_nonblocking();
        spibus->semaphore.give();
    }

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
