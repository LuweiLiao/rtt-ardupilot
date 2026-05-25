/*
 generated hardware definitions from hwdef.dat - DO NOT EDIT
*/

#pragma once

#ifndef TRUE
#define TRUE 1
#endif

#ifndef FALSE
#define FALSE 0
#endif

/* RT-Thread + AP_HAL_RTT board configuration — auto-generated */
/* Do not edit — generated from hwdef.dat by rtt_hwdef.py */

#ifndef SEEK_SET
#define SEEK_SET 0
#endif
#ifndef SEEK_CUR
#define SEEK_CUR 1
#endif
#ifndef SEEK_END
#define SEEK_END 2
#endif

/* GNU libc extensions for RT-Thread/newlib */
#ifdef __cplusplus
#include <cstddef>
extern "C" int ffs(int);
extern "C" int asprintf(char **, const char *, ...);
extern "C" void *memmem(const void *, std::size_t, const void *, std::size_t);
extern "C" std::size_t strnlen(const char *s, std::size_t maxlen);
extern "C" char *strdup(const char *s);
#endif

/* Sensor probe macros */
#define PROBE_IMU_SPI(driver, devname, args ...) ADD_BACKEND(AP_InertialSensor_ ## driver::probe(*this,hal.spi->get_device(devname),##args))
#define PROBE_IMU_SPI2(driver, devname1, devname2, args ...) ADD_BACKEND(AP_InertialSensor_ ## driver::probe(*this,hal.spi->get_device(devname1),hal.spi->get_device(devname2),##args))
#define PROBE_IMU_I2C(driver, bus, addr, args ...) ADD_BACKEND(AP_InertialSensor_ ## driver::probe(*this,GET_I2C_DEVICE(bus,addr),##args))
#define PROBE_BARO_SPI(driver, devname, args ...) ADD_BACKEND(AP_Baro_ ## driver::probe(*this,hal.spi->get_device(devname),##args))
#define PROBE_BARO_I2C(driver, bus, addr, args ...) ADD_BACKEND(AP_Baro_ ## driver::probe(*this,std::move(GET_I2C_DEVICE(bus,addr)),##args))
#define PROBE_MAG_SPI(driver, devname, args ...) ADD_BACKEND(DRIVER_ ## driver, AP_Compass_ ## driver::probe(hal.spi->get_device(devname),##args))
#define PROBE_MAG_I2C(driver, bus, addr, args ...) ADD_BACKEND(DRIVER_ ## driver, AP_Compass_ ## driver::probe(GET_I2C_DEVICE(bus,addr),##args))

#define BOARD_NAME "cuav_v5"

#define HAL_MCU_STM32F7XX 1
#define HAL_MCU_STM32F767XX 1

#define FLASH_SIZE_KB 2048
#define FLASH_RESERVE_START_KB 32
#define FLASH_ORIGIN 0x08008000
#define FLASH_LENGTH_KB 1504

#define OSCILLATOR_HZ 16000000

#define SERIAL_PORT_COUNT 8
#define SERIAL_PORT_0_NAME OTG1
#define SERIAL_PORT_1_NAME USART2
#define SERIAL_PORT_2_NAME USART3
#define SERIAL_PORT_3_NAME USART1
#define SERIAL_PORT_4_NAME UART4
#define SERIAL_PORT_5_NAME USART6
#define SERIAL_PORT_6_NAME UART7
#define SERIAL_PORT_7_NAME OTG2
#define HAL_RTT_SERIAL0_OTG 1
#define HAL_OTG1_CONFIG 1
#define HAL_RTT_UART_DEVICE_LIST "usb-acm0", "uart2", "uart3", "uart1", "uart4", "uart6", "uart7", "usb-acm0"
#define HAL_WITH_IO_MCU 0

#define TARGET_HW_PX4_FMU_V5 50
#define APJ_BOARD_ID TARGET_HW_PX4_FMU_V5

/* SPI device table — generated from SPIDEV lines */
#define HAL_SPI_DEVICE0 {"icm20689", "spi11", 1, 1, 3, 2000000U, 8000000U}
#define HAL_SPI_DEVICE1 {"icm20602", "spi12", 1, 2, 3, 2000000U, 8000000U}
#define HAL_SPI_DEVICE2 {"bmi055_g", "spi13", 1, 3, 3, 10000000U, 10000000U}
#define HAL_SPI_DEVICE3 {"bmi055_a", "spi14", 1, 4, 3, 10000000U, 10000000U}
#define HAL_SPI_DEVICE4 {"ramtron", "spi21", 2, 1, 3, 8000000U, 8000000U}
#define HAL_SPI_DEVICE5 {"ms5611", "spi41", 4, 1, 3, 20000000U, 20000000U}
#define HAL_SPI_DEVICE_LIST HAL_SPI_DEVICE0, HAL_SPI_DEVICE1, HAL_SPI_DEVICE2, HAL_SPI_DEVICE3, HAL_SPI_DEVICE4, HAL_SPI_DEVICE5
#define HAL_SPI_DEVICE_COUNT 6

/* SPI device attach table */
#define HAL_RTT_SPI_ATTACH_LIST \
    {"spi1", "spi11", GET_PIN(F, 2)}, \
    {"spi1", "spi12", GET_PIN(F, 3)}, \
    {"spi1", "spi13", GET_PIN(F, 4)}, \
    {"spi1", "spi14", GET_PIN(G, 10)}, \
    {"spi2", "spi21", GET_PIN(F, 5)}, \
    {"spi4", "spi41", GET_PIN(F, 10)}

/* SPI device attach table — numeric pin values */
#define HAL_RTT_SPI_ATTACH_VALUES \
    {"spi1", "spi11", 82}, \
    {"spi1", "spi12", 83}, \
    {"spi1", "spi13", 84}, \
    {"spi1", "spi14", 106}, \
    {"spi2", "spi21", 85}, \
    {"spi4", "spi41", 90}

/* PWM channel map — generated from PWM pin definitions */
/* Each entry: RTT PWM device name, timer channel (1-based) */
#define HAL_RTT_PWM_MAP_COUNT 8
#define HAL_RTT_PWM_MAP { \
    {"pwm1", 4}, \
    {"pwm1", 3}, \
    {"pwm1", 2}, \
    {"pwm1", 1}, \
    {"pwm4", 2}, \
    {"pwm4", 3}, \
    {"pwm12", 1}, \
    {"pwm12", 2} }

/* GPIO output pins — generated from OUTPUT definitions */
#define HAL_GPIO_VDD_3V3_SENSORS_EN_PIN   GET_PIN(E, 3)
#define HAL_GPIO_VDD_3V3_SENSORS_EN_VALUE 67
#define HAL_GPIO_VDD_3V3_SENSORS_EN_INIT  1
#define HAL_GPIO_VDD_5V_RC_EN_PIN   GET_PIN(G, 5)
#define HAL_GPIO_VDD_5V_RC_EN_VALUE 101
#define HAL_GPIO_VDD_5V_RC_EN_INIT  1
#define HAL_GPIO_VDD_3V3_SD_CARD_EN_PIN   GET_PIN(G, 7)
#define HAL_GPIO_VDD_3V3_SD_CARD_EN_VALUE 103
#define HAL_GPIO_VDD_3V3_SD_CARD_EN_INIT  1
#define HAL_GPIO_HEATER_EN_PIN   GET_PIN(A, 7)
#define HAL_GPIO_HEATER_EN_VALUE 7
#define HAL_GPIO_HEATER_EN_INIT  0
#define HAL_GPIO_VDD_5V_WIFI_EN_PIN   GET_PIN(G, 6)
#define HAL_GPIO_VDD_5V_WIFI_EN_VALUE 102
#define HAL_GPIO_VDD_5V_WIFI_EN_INIT  1
#define HAL_GPIO_nSPI5_RESET_EXTERNAL1_PIN   GET_PIN(B, 10)
#define HAL_GPIO_nSPI5_RESET_EXTERNAL1_VALUE 26
#define HAL_GPIO_nSPI5_RESET_EXTERNAL1_INIT  1
#define HAL_GPIO_LED_RED_PIN   GET_PIN(B, 1)
#define HAL_GPIO_LED_RED_VALUE 17
#define HAL_GPIO_LED_RED_INIT  0
#define HAL_GPIO_LED_GREEN_PIN   GET_PIN(C, 6)
#define HAL_GPIO_LED_GREEN_VALUE 38
#define HAL_GPIO_LED_GREEN_INIT  0
#define HAL_GPIO_LED_BLUE_PIN   GET_PIN(C, 7)
#define HAL_GPIO_LED_BLUE_VALUE 39
#define HAL_GPIO_LED_BLUE_INIT  0
#define HAL_GPIO_SPEKTRUM_PWR_PIN   GET_PIN(E, 4)
#define HAL_GPIO_SPEKTRUM_PWR_VALUE 68
#define HAL_GPIO_SPEKTRUM_PWR_INIT  1
#define HAL_GPIO_nVDD_5V_HIPOWER_EN_PIN   GET_PIN(F, 12)
#define HAL_GPIO_nVDD_5V_HIPOWER_EN_VALUE 92
#define HAL_GPIO_nVDD_5V_HIPOWER_EN_INIT  1
#define HAL_GPIO_nVDD_5V_PERIPH_EN_PIN   GET_PIN(G, 4)
#define HAL_GPIO_nVDD_5V_PERIPH_EN_VALUE 100
#define HAL_GPIO_nVDD_5V_PERIPH_EN_INIT  1
#define HAL_GPIO_GPIO_CAN2_SILENT_PIN   GET_PIN(I, 8)
#define HAL_GPIO_GPIO_CAN2_SILENT_VALUE 136
#define HAL_GPIO_GPIO_CAN2_SILENT_INIT  0
#define HAL_GPIO_GPIO_CAN1_SILENT_PIN   GET_PIN(H, 2)
#define HAL_GPIO_GPIO_CAN1_SILENT_VALUE 114
#define HAL_GPIO_GPIO_CAN1_SILENT_INIT  0
#define HAL_GPIO_GPIO_CAN3_SILENT_PIN   GET_PIN(H, 4)
#define HAL_GPIO_GPIO_CAN3_SILENT_VALUE 116
#define HAL_GPIO_GPIO_CAN3_SILENT_INIT  0

/* ADC input pins — generated from ADC definitions */
#define HAL_ADC_BATT_VOLTAGE_SENS_PIN   GET_PIN(A, 0)
#define HAL_ADC_BATT_VOLTAGE_SENS_SCALE 1
#define HAL_ADC_BATT_CURRENT_SENS_PIN   GET_PIN(A, 1)
#define HAL_ADC_BATT_CURRENT_SENS_SCALE 1
#define HAL_ADC_BATT2_VOLTAGE_SENS_PIN   GET_PIN(A, 2)
#define HAL_ADC_BATT2_VOLTAGE_SENS_SCALE 1
#define HAL_ADC_BATT2_CURRENT_SENS_PIN   GET_PIN(A, 3)
#define HAL_ADC_BATT2_CURRENT_SENS_SCALE 1
#define HAL_ADC_RSSI_IN_PIN   GET_PIN(B, 0)
#define HAL_ADC_RSSI_IN_SCALE 1
#define HAL_ADC_VDD_5V_SENS_PIN   GET_PIN(C, 0)
#define HAL_ADC_VDD_5V_SENS_SCALE 2
#define HAL_ADC_SCALED_V3V3_PIN   GET_PIN(C, 1)
#define HAL_ADC_SCALED_V3V3_SCALE 2
#define HAL_ADC_SPARE1_ADC1_PIN   GET_PIN(C, 4)
#define HAL_ADC_SPARE1_ADC1_SCALE 1
#define HAL_ADC_SPARE2_ADC1_PIN   GET_PIN(A, 4)
#define HAL_ADC_SPARE2_ADC1_SCALE 1

#define HAL_INS_PROBE1  ADD_BACKEND(AP_InertialSensor_Invensense::probe(*this,hal.spi->get_device("icm20689"),ROTATION_NONE))
#define HAL_INS_PROBE2  ADD_BACKEND(AP_InertialSensor_Invensense::probe(*this,hal.spi->get_device("icm20602"),ROTATION_NONE))
#ifndef INS_MAX_INSTANCES
#define INS_MAX_INSTANCES 2
#endif
#define HAL_INS_PROBE_LIST HAL_INS_PROBE1;HAL_INS_PROBE2

#define HAL_BARO_PROBE1  ADD_BACKEND(AP_Baro_MS5611::probe(*this,hal.spi->get_device("ms5611")))
#undef AP_BARO_MS5611_ENABLED
#define AP_BARO_MS5611_ENABLED 1
#define HAL_BARO_PROBE_LIST HAL_BARO_PROBE1

#define DEFAULT_SERIAL0_BAUD 921600
#define AP_CHECK_FIRMWARE_ENABLED 1
#define HAL_BATT_VOLT_PIN 0
#define HAL_BATT_CURR_PIN 1
#define HAL_BATT2_VOLT_PIN 2
#define HAL_BATT2_CURR_PIN 3
#define HAL_BATT_VOLT_SCALE 18.0
#define HAL_BATT_CURR_SCALE 24.0
#define HAL_RTT_I2C_BUS_NAMES "i2c3"
#define HAL_MAG_PROBE_LIST PROBE_MAG_I2C(IST8310, 0, 0x0E, false, ROTATION_ROLL_180_YAW_90)
#define HAL_COMPASS_ALLOW_INIT_NO_MAG 1
#define HAL_COMPASS_AUTO_ROT_DEFAULT 2
#define STM32F767xx 1
#define USE_HAL_DRIVER 1
#define HAL_WITH_RAMTRON 1
#define STORAGE_FLASH_PAGE 10
#define HAL_WITH_EKF_DOUBLE 0
#define HAL_LOGGING_FILESYSTEM_ENABLED 1
#define HAL_LOGGING_MAVLINK_ENABLED 1
#define AP_SCRIPTING_ENABLED 0
#define SCRIPTING_DIRECTORY "/APM/scripts_rtt"
#define AP_FILESYSTEM_POSIX_ENABLED 1
#define AP_NOTIFY_GPIO_LED_RGB_RED_PIN 122
#define AP_NOTIFY_GPIO_LED_RGB_GREEN_PIN 123
#define AP_NOTIFY_GPIO_LED_RGB_BLUE_PIN 124
#define AP_NOTIFY_GPIO_LED_RGB_ENABLED 1
#define HAL_GPIO_A_LED_PIN 90
#define HAL_GPIO_B_LED_PIN 92
#define HAL_HAVE_SAFETY_SWITCH 1
#define HAL_HEATER_GPIO_PIN 80
#define HAL_OS_FATFS_IO 0
#define HAL_SPEKTRUM_PWR_ENABLED 1
#define HAL_GPIO_SPEKTRUM_PWR 73
#define AP_NOTIFY_GPIO_LED_2_ENABLED 1
#define AP_FILESYSTEM_POSIX_HAVE_UTIME 0
#define AP_FILESYSTEM_POSIX_HAVE_STATFS 1
#define HAL_BOARD_LOG_DIRECTORY "/logs"
#define HAL_BOARD_TERRAIN_DIRECTORY "/APM/TERRAIN"
#define HAL_BOARD_STORAGE_DIRECTORY "/APM/STORAGE"
#define AP_FEATURE_BOARD_DETECT 1
#define BOARD_TYPE_DEFAULT 24
#define HAL_BARO_ALLOW_INIT_NO_BARO 1
#define HAL_DEFAULT_INS_FAST_SAMPLE 1
#define HAL_STORAGE_SIZE 32768
#define HAL_WITH_IO_MCU_DSHOT 1
#define HAL_NUM_CAN_IFACES 2
#define HAL_CAN_IFACE1_ENABLE 1
#define HAL_CAN_IFACE2_ENABLE 1
#define HAL_CANFD_SUPPORTED 0


// ROMFS embedded header available
#define HAL_HAVE_AP_ROMFS_EMBEDDED_H 1
