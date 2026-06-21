/*
 * STM32F7 RT-Thread clock_time backend for CUAV V5.
 *
 * DWT CYCCNT is used as the high-resolution source clock, while TIM5 is a
 * one-shot 1 MHz event timer. This gives rt_clock_hrtimer_udelay() the same
 * important scheduling property as ChibiOS' microsecond sleep: the caller is
 * blocked instead of busy-spinning, so lower-priority SPI/IMU producer threads
 * can run while the main loop waits for samples.
 */

#include <rtthread.h>
#include <rthw.h>
#include <rtdevice.h>
#include <drivers/clock_time.h>
#include <stm32f7xx.h>

#if defined(SOC_SERIES_STM32F7)

extern uint32_t SystemCoreClock;

volatile uint32_t rtt_dbg_clock_time_init_count = 0;
volatile uint32_t rtt_dbg_clock_time_last_delta_us = 0;
volatile uint32_t rtt_dbg_clock_time_irq_count = 0;
volatile uint32_t rtt_dbg_clock_time_timeout_count = 0;

static struct rt_clock_time_device dwt_source_dev;
static struct rt_clock_time_device tim5_event_dev;
static rt_bool_t clock_time_ready;

static rt_uint64_t dwt_get_freq(struct rt_clock_time_device *dev)
{
    RT_UNUSED(dev);
    return SystemCoreClock;
}

static rt_uint64_t dwt_get_counter(struct rt_clock_time_device *dev)
{
    RT_UNUSED(dev);
    return DWT->CYCCNT;
}

static rt_err_t dwt_set_timeout(struct rt_clock_time_device *dev, rt_uint64_t delta)
{
    RT_UNUSED(dev);
    RT_UNUSED(delta);
    return -RT_ENOSYS;
}

static const struct rt_clock_time_ops dwt_source_ops =
{
    dwt_get_freq,
    dwt_get_counter,
    dwt_set_timeout,
};

static rt_uint32_t apb1_timer_clock_hz(void)
{
    const rt_uint32_t ppre1 = RCC->CFGR & RCC_CFGR_PPRE1;
    rt_uint32_t divisor = 1;

    switch (ppre1)
    {
    case RCC_CFGR_PPRE1_DIV2:
        divisor = 2;
        break;
    case RCC_CFGR_PPRE1_DIV4:
        divisor = 4;
        break;
    case RCC_CFGR_PPRE1_DIV8:
        divisor = 8;
        break;
    case RCC_CFGR_PPRE1_DIV16:
        divisor = 16;
        break;
    default:
        divisor = 1;
        break;
    }

    const rt_uint32_t pclk1 = SystemCoreClock / divisor;
    return divisor == 1 ? pclk1 : pclk1 * 2U;
}

static rt_uint64_t tim5_get_freq(struct rt_clock_time_device *dev)
{
    RT_UNUSED(dev);
    return 1000000ULL;
}

static rt_uint64_t tim5_get_counter(struct rt_clock_time_device *dev)
{
    RT_UNUSED(dev);
    return TIM5->CNT;
}

static rt_err_t tim5_set_timeout(struct rt_clock_time_device *dev, rt_uint64_t delta)
{
    RT_UNUSED(dev);

    if (!clock_time_ready)
    {
        return -RT_ERROR;
    }

    if (delta == 0)
    {
        delta = 1;
    }
    if (delta > 0xFFFFFFFFULL)
    {
        delta = 0xFFFFFFFFULL;
    }

    TIM5->CR1 &= ~TIM_CR1_CEN;
    TIM5->DIER = 0;
    TIM5->SR = 0;
    TIM5->CNT = 0;
    TIM5->ARR = (rt_uint32_t)delta;
    TIM5->EGR = TIM_EGR_UG;
    TIM5->SR = 0;
    TIM5->DIER = TIM_DIER_UIE;
    TIM5->CR1 = TIM_CR1_OPM | TIM_CR1_CEN;

    rtt_dbg_clock_time_last_delta_us = (rt_uint32_t)delta;
    rtt_dbg_clock_time_timeout_count++;
    return RT_EOK;
}

static const struct rt_clock_time_ops tim5_event_ops =
{
    tim5_get_freq,
    tim5_get_counter,
    tim5_set_timeout,
};

void rt_clock_time_source_init(void)
{
    static rt_bool_t initialized = RT_FALSE;
    if (initialized)
    {
        return;
    }
    initialized = RT_TRUE;

    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

    RCC->APB1ENR |= RCC_APB1ENR_TIM5EN;
    (void)RCC->APB1ENR;
    RCC->APB1RSTR |= RCC_APB1RSTR_TIM5RST;
    RCC->APB1RSTR &= ~RCC_APB1RSTR_TIM5RST;

    TIM5->CR1 = 0;
    TIM5->CR2 = 0;
    TIM5->SMCR = 0;
    TIM5->DIER = 0;
    TIM5->SR = 0;
    TIM5->CNT = 0;
    TIM5->PSC = (apb1_timer_clock_hz() / 1000000U) - 1U;
    TIM5->ARR = 1;
    TIM5->EGR = TIM_EGR_UG;
    TIM5->SR = 0;

    dwt_source_dev.ops = &dwt_source_ops;
    dwt_source_dev.res_scale = RT_CLOCK_TIME_RESMUL;
    tim5_event_dev.ops = &tim5_event_ops;
    tim5_event_dev.res_scale = RT_CLOCK_TIME_RESMUL;

    rt_clock_time_device_register(&dwt_source_dev, "dwtclk", RT_CLOCK_TIME_CAP_SOURCE);
    rt_clock_time_set_default_source(&dwt_source_dev);
    rt_clock_time_device_register(&tim5_event_dev, "tim5evt", RT_CLOCK_TIME_CAP_EVENT);
    rt_clock_time_set_default_event(&tim5_event_dev);

    NVIC_SetPriority(TIM5_IRQn, 5);
    NVIC_ClearPendingIRQ(TIM5_IRQn);
    NVIC_EnableIRQ(TIM5_IRQn);

    clock_time_ready = RT_TRUE;
    rtt_dbg_clock_time_init_count++;
}

static int rtt_clock_time_board_init(void)
{
    rt_clock_time_source_init();
    return 0;
}
INIT_BOARD_EXPORT(rtt_clock_time_board_init);

void TIM5_IRQHandler(void)
{
    if ((TIM5->SR & TIM_SR_UIF) == 0)
    {
        return;
    }

    TIM5->SR = (rt_uint16_t)~TIM_SR_UIF;
    TIM5->DIER = 0;
    TIM5->CR1 &= ~TIM_CR1_CEN;

    rt_interrupt_enter();
    rtt_dbg_clock_time_irq_count++;
    rt_clock_time_event_isr();
    rt_interrupt_leave();
}

#endif /* SOC_SERIES_STM32F7 */
