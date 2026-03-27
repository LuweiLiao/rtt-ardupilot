/*
 * AP_HAL_RTT — STM32 Flash driver
 * Wraps STM32 HAL Flash API for parameter storage.
 */

#pragma once

#include <AP_HAL/AP_HAL.h>
#include "HAL_RTT_Namespace.h"
#include "Semaphores.h"

class RTT::Flash : public AP_HAL::Flash
{
public:
    uint32_t getpageaddr(uint32_t page) override;
    uint32_t getpagesize(uint32_t page) override;
    uint32_t getnumpages(void) override;
    bool erasepage(uint32_t page) override;
    bool write(uint32_t addr, const void *buf, uint32_t count) override;
    void keep_unlocked(bool set) override;
    bool ispageerased(uint32_t page) override;

private:
    Semaphore _sem;
    bool _keep_unlocked = false;
};
