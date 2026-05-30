/*
 * AP_HAL_RTT — shared DMA stream arbitration (ChibiOS API parity).
 *
 * Semantic port of libraries/AP_HAL_ChibiOS/shared_dma.h using RT-Thread
 * mutexes instead of ChibiOS chMtx. No ChibiOS kernel types.
 *
 * Call Shared_DMA::init() once from HAL_RTT::run() before drivers that may
 * share DMA streams (SPI, UART, future bdshot/AnalogIn DMA).
 */

#pragma once

#include "HAL_RTT_Namespace.h"
#include <AP_HAL/AP_HAL.h>

#define SHARED_DMA_MAX_STREAM_ID (8*2)

/* DMA stream ID placeholder when only one stream is used */
#define SHARED_DMA_NONE 255

#ifndef SHARED_DMA_MASK
#define SHARED_DMA_MASK 0
#endif

#if AP_HAL_SHARED_DMA_ENABLED

namespace RTT
{

class Shared_DMA
{
public:
    FUNCTOR_TYPEDEF(dma_allocate_fn_t, void, Shared_DMA *);
    FUNCTOR_TYPEDEF(dma_deallocate_fn_t, void, Shared_DMA *);

    Shared_DMA(uint8_t stream_id1, uint8_t stream_id2,
               dma_allocate_fn_t allocate,
               dma_deallocate_fn_t deallocate);

    static void init(void);

    void lock(void);
    bool lock_nonblock(void);
    void unlock(bool success = true);
    void unregister(void);

    bool has_contention(void) const { return contention; }
    bool is_locked(void) const { return have_lock; }

    static void lock_all(void);
    static void dma_info(ExpandingString &str);

    static bool is_shared(uint8_t stream_id);
    bool is_shared();

private:
    dma_allocate_fn_t allocate;
    dma_deallocate_fn_t deallocate;
    uint8_t stream_id1;
    uint8_t stream_id2;
    bool have_lock;
    bool contention;

    void lock_core(void);

    static bool lock_stream(uint8_t stream_id);
    void unlock_stream(uint8_t stream_id, bool success);
    bool lock_stream_nonblocking(uint8_t stream_id);

    struct dma_lock {
        struct rt_mutex mutex;
        dma_deallocate_fn_t deallocate;
        Shared_DMA *obj;
        bool mutex_inited;
    };

    static dma_lock locks[SHARED_DMA_MAX_STREAM_ID+1];

    static volatile struct dma_stats {
        uint32_t contended_locks;
        uint32_t uncontended_locks;
        uint32_t transactions;
    } *_contention_stats;
};

} // namespace RTT

#endif // AP_HAL_SHARED_DMA_ENABLED
