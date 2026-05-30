/*
 * AP_HAL_RTT — shared DMA stream arbitration.
 *
 * Ported from AP_HAL_ChibiOS/shared_dma.cpp (RT-Thread mutex backend).
 */

#include "shared_dma.h"

#if AP_HAL_SHARED_DMA_ENABLED

#include <AP_Common/ExpandingString.h>
#include <rtthread.h>

using namespace RTT;

Shared_DMA::dma_lock Shared_DMA::locks[SHARED_DMA_MAX_STREAM_ID+1];
volatile Shared_DMA::dma_stats *Shared_DMA::_contention_stats;

void Shared_DMA::init(void)
{
    for (uint8_t i = 0; i < SHARED_DMA_MAX_STREAM_ID; i++) {
        if (!locks[i].mutex_inited) {
            rt_mutex_init(&locks[i].mutex, "shdma", RT_IPC_FLAG_PRIO);
            locks[i].mutex_inited = true;
            locks[i].obj = nullptr;
            locks[i].deallocate = nullptr;
        }
    }
}

Shared_DMA::Shared_DMA(uint8_t _stream_id1,
                       uint8_t _stream_id2,
                       dma_allocate_fn_t _allocate,
                       dma_deallocate_fn_t _deallocate)
    : allocate(_allocate),
      deallocate(_deallocate),
      stream_id1(_stream_id1),
      stream_id2(_stream_id2),
      have_lock(false),
      contention(false)
{
    if (stream_id2 < stream_id1) {
        const uint8_t tmp = stream_id1;
        stream_id1 = stream_id2;
        stream_id2 = tmp;
    }
}

bool Shared_DMA::is_shared(uint8_t stream_id)
{
    return (stream_id < SHARED_DMA_MAX_STREAM_ID) && ((1U<<stream_id) & SHARED_DMA_MASK) != 0;
}

bool Shared_DMA::is_shared()
{
    return is_shared(stream_id1) || is_shared(stream_id2);
}

void Shared_DMA::unregister()
{
    if (stream_id1 < SHARED_DMA_MAX_STREAM_ID &&
        locks[stream_id1].obj == this) {
        locks[stream_id1].deallocate(this);
        locks[stream_id1].obj = nullptr;
    }

    if (stream_id2 < SHARED_DMA_MAX_STREAM_ID &&
        locks[stream_id2].obj == this) {
        locks[stream_id2].deallocate(this);
        locks[stream_id2].obj = nullptr;
    }
}

bool Shared_DMA::lock_stream(uint8_t stream_id)
{
    bool cont = false;
    if (stream_id < SHARED_DMA_MAX_STREAM_ID && locks[stream_id].mutex_inited) {
        rt_thread_t owner = locks[stream_id].mutex.owner;
        rt_mutex_take(&locks[stream_id].mutex, RT_WAITING_FOREVER);
        cont = (owner != RT_NULL) && (owner != locks[stream_id].mutex.owner);
    }
    return cont;
}

void Shared_DMA::unlock_stream(uint8_t stream_id, bool success)
{
    if (stream_id < SHARED_DMA_MAX_STREAM_ID && locks[stream_id].mutex_inited) {
        rt_mutex_release(&locks[stream_id].mutex);
        if (success && _contention_stats != nullptr) {
            _contention_stats[stream_id].transactions++;
        }
    }
}

bool Shared_DMA::lock_stream_nonblocking(uint8_t stream_id)
{
    if (stream_id < SHARED_DMA_MAX_STREAM_ID && locks[stream_id].mutex_inited) {
        return rt_mutex_trytake(&locks[stream_id].mutex) == RT_EOK;
    }
    return true;
}

void Shared_DMA::lock_core(void)
{
    if (stream_id1 < SHARED_DMA_MAX_STREAM_ID &&
        locks[stream_id1].obj && locks[stream_id1].obj != this) {
        locks[stream_id1].deallocate(locks[stream_id1].obj);
        locks[stream_id1].obj = nullptr;
    }
    if (stream_id2 < SHARED_DMA_MAX_STREAM_ID &&
        locks[stream_id2].obj && locks[stream_id2].obj != this) {
        locks[stream_id2].deallocate(locks[stream_id2].obj);
        locks[stream_id2].obj = nullptr;
    }
    if ((stream_id1 < SHARED_DMA_MAX_STREAM_ID && locks[stream_id1].obj == nullptr) ||
        (stream_id2 < SHARED_DMA_MAX_STREAM_ID && locks[stream_id2].obj == nullptr)) {
        allocate(this);
        if (stream_id1 < SHARED_DMA_MAX_STREAM_ID) {
            locks[stream_id1].deallocate = deallocate;
            locks[stream_id1].obj = this;
        }
        if (stream_id2 < SHARED_DMA_MAX_STREAM_ID) {
            locks[stream_id2].deallocate = deallocate;
            locks[stream_id2].obj = this;
        }
    }

    if (_contention_stats != nullptr) {
        if (stream_id1 < SHARED_DMA_MAX_STREAM_ID) {
            if (contention) {
                _contention_stats[stream_id1].contended_locks++;
            } else {
                _contention_stats[stream_id1].uncontended_locks++;
            }
        }
        if (stream_id2 < SHARED_DMA_MAX_STREAM_ID) {
            if (contention) {
                _contention_stats[stream_id2].contended_locks++;
            } else {
                _contention_stats[stream_id2].uncontended_locks++;
            }
        }
    }
    have_lock = true;
}

void Shared_DMA::lock(void)
{
    const bool c1 = lock_stream(stream_id1);
    const bool c2 = lock_stream(stream_id2);
    contention = c1 || c2;
    lock_core();
}

bool Shared_DMA::lock_nonblock(void)
{
    if (!lock_stream_nonblocking(stream_id1)) {
        rt_base_t level = rt_hw_interrupt_disable();
        if (locks[stream_id1].obj != nullptr && locks[stream_id1].obj != this) {
            locks[stream_id1].obj->contention = true;
            if (_contention_stats != nullptr) {
                _contention_stats[stream_id1].contended_locks++;
            }
        }
        rt_hw_interrupt_enable(level);
        contention = true;
        return false;
    }

    if (_contention_stats != nullptr && stream_id1 < SHARED_DMA_MAX_STREAM_ID) {
        _contention_stats[stream_id1].uncontended_locks++;
    }

    if (!lock_stream_nonblocking(stream_id2)) {
        unlock_stream(stream_id1, false);
        rt_base_t level = rt_hw_interrupt_disable();
        if (locks[stream_id2].obj != nullptr && locks[stream_id2].obj != this) {
            locks[stream_id2].obj->contention = true;
            if (_contention_stats != nullptr) {
                _contention_stats[stream_id2].contended_locks++;
            }
        }
        rt_hw_interrupt_enable(level);
        contention = true;
        return false;
    }

    lock_core();

    if (_contention_stats != nullptr && stream_id2 < SHARED_DMA_MAX_STREAM_ID) {
        _contention_stats[stream_id2].uncontended_locks++;
    }
    return true;
}

void Shared_DMA::unlock(bool success)
{
    if (!have_lock) {
        return;
    }
    have_lock = false;
    unlock_stream(stream_id2, success);
    unlock_stream(stream_id1, success);
}

void Shared_DMA::lock_all(void)
{
    for (uint8_t i = 0; i < SHARED_DMA_MAX_STREAM_ID; i++) {
        lock_stream(i);
    }
}

void Shared_DMA::dma_info(ExpandingString &str)
{
    if (_contention_stats == nullptr) {
        _contention_stats = NEW_NOTHROW dma_stats[SHARED_DMA_MAX_STREAM_ID+1];
    }

    str.printf("DMAV1\n");

    for (uint8_t i = 0; i < SHARED_DMA_MAX_STREAM_ID; i++) {
        if (_contention_stats[i].contended_locks == 0
            && _contention_stats[i].uncontended_locks == 0
            && _contention_stats[i].transactions == 0) {
            continue;
        }
#if defined(STM32_DMA_ADVANCED)
#define STREAM_MUX 8
#define STREAM_OFFSET 0
#else
#define STREAM_MUX 7
#define STREAM_OFFSET 1
#endif
        const char *fmt = "DMA=%1u:%1u TX=%8u ULCK=%8u CLCK=%8u CONT=%4.1f%%\n";
        const float cond_per = 100.0f * float(_contention_stats[i].contended_locks)
            / (1.0f + float(_contention_stats[i].contended_locks + _contention_stats[i].uncontended_locks));
        str.printf(fmt, i / STREAM_MUX + 1, i % STREAM_MUX + STREAM_OFFSET,
                   _contention_stats[i].transactions,
                   _contention_stats[i].uncontended_locks,
                   _contention_stats[i].contended_locks, cond_per);

        _contention_stats[i].contended_locks = 0;
        _contention_stats[i].uncontended_locks = 0;
    }
}

#endif // AP_HAL_SHARED_DMA_ENABLED
