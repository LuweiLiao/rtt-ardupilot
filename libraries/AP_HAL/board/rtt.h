#pragma once

/**
 * RT-Thread (RTT) board HAL header.
 * Selected when CONFIG_HAL_BOARD == HAL_BOARD_RTT.
 * HAL implementation is provided by AP_HAL_RTT (libraries/AP_HAL_RTT).
 */

#include <hwdef.h>

/* RGB LED (PixRacer scheme): PH10/PH11/PH12 via RT-Thread GET_PIN(port,num) */
#ifndef AP_NOTIFY_GPIO_LED_RGB_ENABLED
#define AP_NOTIFY_GPIO_LED_RGB_ENABLED 1
#endif
#ifndef AP_NOTIFY_GPIO_LED_RGB_RED_PIN
#define AP_NOTIFY_GPIO_LED_RGB_RED_PIN 122   /* GET_PIN(H,10) = 7*16+10 */
#endif
#ifndef AP_NOTIFY_GPIO_LED_RGB_GREEN_PIN
#define AP_NOTIFY_GPIO_LED_RGB_GREEN_PIN 123  /* GET_PIN(H,11) = 7*16+11 */
#endif
#ifndef AP_NOTIFY_GPIO_LED_RGB_BLUE_PIN
#define AP_NOTIFY_GPIO_LED_RGB_BLUE_PIN 124   /* GET_PIN(H,12) = 7*16+12 */
#endif
/* Active-low LEDs: HAL_GPIO_LED_ON defaults to 0 (write LOW to turn on).
 * Do NOT define it — AP_HAL_Boards.h default is 0 and errors if explicitly
 * set to 0. GPIO pin init is in rt_board_init.c instead of PixRacerLED::init()
 * since PixRacerLED skips pinMode() when LED_ON==0 (ChibiOS uses hwdef pin parser). */

/*
 * GPS/WGS84 等需要 double 与 libm（RAD_TO_DEG_DOUBLE、sqrt 等），与 EKF 是否双精度无关。
 * AP_HAL_Macros.h 仅在 SITL/Linux/HAL_WITH_EKF_DOUBLE/AP_SIM 时开启 ALLOW_DOUBLE_MATH_FUNCTIONS；
 * 若 hwdef 设 HAL_WITH_EKF_DOUBLE=0 而不定义此项，整机会无法编译。
 */
#ifndef ALLOW_DOUBLE_MATH_FUNCTIONS
#define ALLOW_DOUBLE_MATH_FUNCTIONS
#endif

#define HAL_BOARD_NAME "RTT"

#define HAL_BOARD_SUBTYPE_RTT_GENERIC 7000
#ifndef CONFIG_HAL_BOARD_SUBTYPE
#define CONFIG_HAL_BOARD_SUBTYPE HAL_BOARD_SUBTYPE_RTT_GENERIC
#endif

#define HAL_INS_DEFAULT HAL_INS_NONE

/* Barometer SPI device names — must match SPIDEV names in hwdef.dat */
#ifndef HAL_BARO_MS5611_NAME
#define HAL_BARO_MS5611_NAME "ms5611"
#endif
#ifndef HAL_BARO_MS5611_SPI_EXT_NAME
#define HAL_BARO_MS5611_SPI_EXT_NAME "ms5611_ext"
#endif
/* Allow baro-less boot (bringup phase) */
#ifndef HAL_BARO_ALLOW_INIT_NO_BARO
#define HAL_BARO_ALLOW_INIT_NO_BARO
#endif

#define HAL_CPU_CLASS HAL_CPU_CLASS_150
#define HAL_MEM_CLASS HAL_MEM_CLASS_192

#ifndef HAL_STORAGE_SIZE
#define HAL_STORAGE_SIZE 16384
#endif
#define HAL_STORAGE_SIZE_AVAILABLE HAL_STORAGE_SIZE

#ifndef HAL_PROGRAM_SIZE_LIMIT_KB
#define HAL_PROGRAM_SIZE_LIMIT_KB 2048
#endif
/* No QuadSPI on generic RTT; disable scripting for first port (no filesystem yet) */
#ifndef HAL_USE_QUADSPI
#define HAL_USE_QUADSPI 0
#endif
#ifndef AP_SCRIPTING_ENABLED
#define AP_SCRIPTING_ENABLED 0
#endif
/* Define early so any config that uses them sees them (include order) */
#ifndef HAL_OS_FATFS_IO
#define HAL_OS_FATFS_IO 0
#endif

/*
 * RTT provides POSIX-compatible file operations through its DFS VFS.
 * Enable POSIX filesystem backend so AP_Filesystem can access the SD card
 * which is mounted at "/" by BSP's sd_card_mount_sync().
 */
#ifndef AP_FILESYSTEM_POSIX_ENABLED
#define AP_FILESYSTEM_POSIX_ENABLED 1
#endif
#ifndef AP_FILESYSTEM_POSIX_HAVE_FSYNC
#define AP_FILESYSTEM_POSIX_HAVE_FSYNC 1
#endif
#ifndef AP_FILESYSTEM_POSIX_HAVE_STATFS
#define AP_FILESYSTEM_POSIX_HAVE_STATFS 1
#endif
#ifndef AP_FILESYSTEM_POSIX_HAVE_UTIME
#define AP_FILESYSTEM_POSIX_HAVE_UTIME 0
#endif

#ifndef AP_SIM_ENABLED
#define AP_SIM_ENABLED 0
#endif
#ifndef AP_HAL_UARTDRIVER_ENABLED
#define AP_HAL_UARTDRIVER_ENABLED 1
#endif
#ifndef HAL_OS_LITTLEFS_IO
#define HAL_OS_LITTLEFS_IO 0
#endif
#ifndef HAL_LOGGING_ENABLED
#define HAL_LOGGING_ENABLED 1
#endif
#ifndef HAL_WITH_DSP
#define HAL_WITH_DSP 0
#endif
/* GyroFFT requires DSP; explicitly disable when HAL_WITH_DSP is 0,
 * otherwise AP_HAL_Boards.h default (HAL_PROGRAM_SIZE_LIMIT_KB > 1024)
 * re-enables it on large-flash boards like CUAV V5 (2MB) */
#if !HAL_WITH_DSP
#ifndef HAL_GYROFFT_ENABLED
#define HAL_GYROFFT_ENABLED 0
#endif
#endif
#ifndef HAL_CANFD_SUPPORTED
#define HAL_CANFD_SUPPORTED 0
#endif

#ifndef HAL_HAVE_BOARD_VOLTAGE
#define HAL_HAVE_BOARD_VOLTAGE 0
#endif
#ifndef HAL_HAVE_SERVO_VOLTAGE
#define HAL_HAVE_SERVO_VOLTAGE 0
#endif
#ifndef HAL_HAVE_SAFETY_SWITCH
#define HAL_HAVE_SAFETY_SWITCH 0
#endif

/* Disable CAN until RTT CAN interface (CanIface.cpp) is properly implemented.
 * hwdef.dat may set HAL_NUM_CAN_IFACES 2 but the RTT CAN driver is not ready yet. */
#undef HAL_NUM_CAN_IFACES
#define HAL_NUM_CAN_IFACES 0

#undef HAL_MAX_CAN_PROTOCOL_DRIVERS
#define HAL_MAX_CAN_PROTOCOL_DRIVERS 0

#ifndef HAL_PICCOLO_CAN_ENABLE
#define HAL_PICCOLO_CAN_ENABLE 0
#endif

#ifndef HAL_BOARD_STORAGE_DIRECTORY
#define HAL_BOARD_STORAGE_DIRECTORY "/APM"
#endif
#ifndef HAL_BOARD_TERRAIN_DIRECTORY
#define HAL_BOARD_TERRAIN_DIRECTORY "/APM/TERRAIN"
#endif

#ifndef HAL_HAVE_HARDWARE_DOUBLE
#define HAL_HAVE_HARDWARE_DOUBLE 1
#endif
#ifndef HAL_WITH_EKF_DOUBLE
#define HAL_WITH_EKF_DOUBLE HAL_HAVE_HARDWARE_DOUBLE
#endif

#ifndef HAL_WITH_MCU_MONITORING
#define HAL_WITH_MCU_MONITORING 0
#endif

#ifndef HAL_WITH_IO_MCU
#define HAL_WITH_IO_MCU 0
#endif

/* 与 ChibiOS 一致：默认关闭 RAMTRON；具体板子在 hwdef.dat 中 define HAL_WITH_RAMTRON 1 */
#ifndef HAL_WITH_RAMTRON
#define HAL_WITH_RAMTRON 0
#endif

/* Macros used in #if (not #ifdef) in HAL headers; must be defined before
 * rtt.h pulls in AP_HAL.h via Semaphores.h, since AP_HAL_Boards.h is
 * not yet fully processed at that point (-Werror=undef). */
#ifndef AP_CRASHDUMP_ENABLED
#define AP_CRASHDUMP_ENABLED 0
#endif
#ifndef HAL_ENABLE_DFU_BOOT
#define HAL_ENABLE_DFU_BOOT 0
#endif
#ifndef HAL_USE_WSPI_DEFAULT_CFG
#define HAL_USE_WSPI_DEFAULT_CFG 0
#endif

#ifdef __cplusplus
/* AP_Common.h defines WARN_IF_UNUSED etc.; include before HAL so HAL headers see it */
#include <AP_Common/AP_Common.h>
/* Requires AP_HAL_RTT (libraries/AP_HAL_RTT) to be present */
#include <AP_HAL_RTT/Semaphores.h>
#define HAL_Semaphore RTT::Semaphore
#define HAL_BinarySemaphore RTT::BinarySemaphore
#endif
