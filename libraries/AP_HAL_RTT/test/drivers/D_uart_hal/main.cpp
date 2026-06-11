/**
 * test_D_uart_hal — AP_HAL UARTDriver HAL smoke (Batch A1)
 *
 * Exercises real AP_HAL::UARTDriver via hal.serial() — begin / printf / write.
 * Does not claim RX or loopback; does not start full hal.run() / scheduler threads.
 *
 * CUAV V5 SERIAL_ORDER: OTG1(0) USART2(1) ... USART6(5) OTG2(6).
 * Module tests use USB backend NONE — serial(0) may be USB CDC without native USB;
 * primary hardware smoke target is serial(5) = USART6. UART7 is reserved for
 * the RT-Thread console/shell debug port.
 *
 * Build: scons --target=cuav_v5 --test=D_uart_hal -j$(nproc)
 */

#include <AP_HAL/AP_HAL.h>
#include <stdio.h>
#include <string.h>

extern "C" {
#include "test_runner.h"
}

static const AP_HAL::HAL &hal = AP_HAL::get_HAL();

static void setup_uart(AP_HAL::UARTDriver *uart, const char *name)
{
    if (uart == nullptr) {
        test_printf("    %s: (null driver — skip)\r\n", name);
        return;
    }
    uart->begin(57600);
    test_printf("    %s: begin(57600) ok\r\n", name);
}

static void exercise_uart(AP_HAL::UARTDriver *uart, const char *name)
{
    if (uart == nullptr) {
        return;
    }

    uart->printf("D_uart_hal HAL smoke on %s\r\n", name);

    static const char wr[] = "D_uart_hal write probe\r\n";
    const uint32_t n = uart->write((const uint8_t *)wr, (uint32_t)strlen(wr));
    test_printf("    %s: write returned %lu bytes\r\n", name, (unsigned long)n);
    TEST_ASSERT(n == strlen(wr), "write byte count");
}

static void step_uart_hal_smoke(void)
{
    TEST_STEP("UARTDriver HAL smoke (AP_HAL API)");

    test_printf("    hal.scheduler->delay(200) before UART begin\r\n");
    hal.scheduler->delay(200);

    AP_HAL::UARTDriver *const s5 = hal.serial(5);
    AP_HAL::UARTDriver *const s0 = hal.serial(0);

    TEST_ASSERT(s5 != nullptr, "serial(5) USART6 driver");
    test_printf("    serial(5)=USART6 hardware path (primary)\r\n");

    setup_uart(s5, "SERIAL5/USART6");
    exercise_uart(s5, "SERIAL5/USART6");

    test_printf("    serial(0)=OTG1/USB (optional; USB backend may be NONE)\r\n");
    setup_uart(s0, "SERIAL0/OTG1");
    if (s0 != nullptr) {
        s0->printf("D_uart_hal SERIAL0 optional line\r\n");
    }

    test_printf("    note: RX / loopback not tested in this image\r\n");
    test_printf("    note: scheduler->init() not called (isolated smoke)\r\n");

    TEST_PASS();
}

extern "C" int main(void)
{
    TEST_INIT("D_UART_HAL");

    step_uart_hal_smoke();

    TEST_DONE();
    return 0;
}
