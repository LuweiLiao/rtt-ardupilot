/*
 * ChibiOS compatibility shim for AP_IOMCU on RT-Thread.
 *
 * AP_IOMCU.cpp includes <ch.h> and uses ChibiOS threading/event primitives.
 * This header maps those to RT-Thread equivalents so the upstream AP_IOMCU
 * library compiles without modification.
 *
 * Key mappings:
 *   chEvtSignal()          -> rt_event_send() on per-thread rt_event object
 *   chThdGetSelfX()        -> rt_thread_self() (wrapped in ch_thread)
 *   chEvtWaitAnyTimeout()  -> rt_event_recv() with RT_EVENT_FLAG_OR
 *   chThdCreateStatic()    -> rt_thread_create() + rt_thread_startup()
 *   EVENT_MASK(n)          -> (1UL << (n))
 */

#ifndef CH_H_
#define CH_H_

#include <rtthread.h>
#include <stdlib.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ----------------------------------------------------------------
 *  Type definitions
 * ---------------------------------------------------------------- */

/* ChibiOS systime_t = system ticks */
typedef rt_tick_t systime_t;

/* msg_t for chThdExit return */
typedef int32_t msg_t;

/* ChibiOS thread_t wrapper — carries the RT-Thread thread handle
 * plus an embedded rt_event for the ChibiOS event signaling model. */
typedef struct ch_thread {
    rt_thread_t      rtt_thread;
    struct rt_event  event;
} thread_t;

/* ----------------------------------------------------------------
 *  Event mask macro
 * ---------------------------------------------------------------- */
typedef uint32_t eventmask_t;
#define EVENT_MASK(n)   (1UL << (n))
#define ALL_EVENTS      (~0UL)

/* ----------------------------------------------------------------
 *  Time conversion
 * ---------------------------------------------------------------- */

/* Convert milliseconds to system ticks */
static inline systime_t chTimeMS2I(uint32_t ms)
{
    return rt_tick_from_millisecond(ms);
}

/* ----------------------------------------------------------------
 *  Thread functions
 * ---------------------------------------------------------------- */

/* Get current thread as thread_t*.
 * Relies on the thread_t* being stored in rt_thread->user_data
 * at creation time (see chThdCreateStatic). */
static inline thread_t *chThdGetSelfX(void)
{
    rt_thread_t self = rt_thread_self();
    if (self) {
        return (thread_t *)self->user_data;
    }
    return nullptr;
}

/* Signal an event to a thread */
static inline void chEvtSignal(thread_t *tp, eventmask_t events)
{
    if (tp) {
        rt_event_send(&tp->event, events);
    }
}

/* Wait for any event with timeout. Returns received event mask, or 0 on timeout. */
static inline eventmask_t chEvtWaitAnyTimeout(eventmask_t mask, systime_t timeout_ticks)
{
    rt_uint32_t recved = 0;
    rt_err_t ret = rt_event_recv(&chThdGetSelfX()->event,
                                  mask,
                                  RT_EVENT_FLAG_OR | RT_EVENT_FLAG_CLEAR,
                                  timeout_ticks,
                                  &recved);
    if (ret == RT_EOK) {
        return recved;
    }
    return 0;
}

/* Create a thread. ChibiOS uses static allocation; RT-Thread uses dynamic.
 * Parameters:
 *   wa          — working area pointer (unused, RT-Thread allocates internally)
 *   stack_size  — stack size in bytes
 *   prio        — RT-Thread priority (lower = higher priority)
 *   func        — thread entry function: void (*)(void *)
 *   arg         — argument to thread function */
static inline thread_t *chThdCreateStatic(void *wa, size_t stack_size,
                                           rt_uint8_t prio,
                                           void (*func)(void *),
                                           void *arg)
{
    (void)wa;  /* RT-Thread allocates stack internally */

    thread_t *tp = (thread_t *)calloc(1, sizeof(thread_t));
    if (!tp) {
        return nullptr;
    }

    /* Initialize the embedded event object */
    static int evt_seq = 0;
    char evt_name[RT_NAME_MAX];
    rt_snprintf(evt_name, sizeof(evt_name), "ioe%d", evt_seq++);
    rt_event_init(&tp->event, evt_name, RT_IPC_FLAG_PRIO);

    /* Create RT-Thread thread */
    tp->rtt_thread = rt_thread_create("iomcu",
                                       func,
                                       arg,
                                       stack_size,
                                       prio,
                                       20);  /* timeslice in ticks */
    if (!tp->rtt_thread) {
        free(tp);
        return nullptr;
    }

    /* Store thread_t* in user_data so chThdGetSelfX() can retrieve it */
    tp->rtt_thread->user_data = (uintptr_t)tp;

    rt_thread_startup(tp->rtt_thread);
    return tp;
}

/* Exit current thread. In RT-Thread, a thread function simply returns. */
static inline void chThdExit(msg_t msg)
{
    (void)msg;
    /* nothing — the thread function will return naturally */
}

#ifdef __cplusplus
}
#endif

#endif /* CH_H_ */
