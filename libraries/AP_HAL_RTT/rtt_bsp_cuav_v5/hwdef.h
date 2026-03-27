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

/* RT-Thread + AP_HAL_RTT board configuration */

#ifndef SEEK_SET
#define SEEK_SET 0
#endif
#ifndef SEEK_CUR
#define SEEK_CUR 1
#endif
#ifndef SEEK_END
#define SEEK_END 2
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
#define HAL_MCU_STM32F765XX 1

#define FLASH_SIZE_KB 2048
#define FLASH_RESERVE_START_KB 32
#define FLASH_ORIGIN 0x08008000
#define FLASH_LENGTH_KB 2016

#define OSCILLATOR_HZ 16000000

#define SERIAL_PORT_COUNT 2
#define SERIAL_PORT_0_NAME OTG1
#define SERIAL_PORT_1_NAME USART3
#define HAL_RTT_UART_DEVICE_LIST "usb-acm0", "uart3"
#define HAL_RTT_SERIAL0_OTG 1
#define HAL_OTG1_CONFIG 1

#define TARGET_HW_PX4_FMU_V5 50
#define APJ_BOARD_ID TARGET_HW_PX4_FMU_V5

/* SPI device table — generated from SPIDEV lines in hwdef.dat */
/* RTT_SPIDesc: name, rtt_devname, bus, devid, mode, lowspeed, highspeed */
#define HAL_SPI_DEVICE0 {"icm20689", "spi11", 1, 1, 3, 2000000U, 8000000U}
#define HAL_SPI_DEVICE1 {"icm20602", "spi12", 1, 2, 3, 2000000U, 8000000U}
#define HAL_SPI_DEVICE2 {"icm42688", "spi13", 1, 3, 3, 2000000U, 8000000U}
#define HAL_SPI_DEVICE3 {"bmi055_g", "spi14", 1, 4, 3, 10000000U, 10000000U}
#define HAL_SPI_DEVICE4 {"bmi055_a", "spi15", 1, 5, 3, 10000000U, 10000000U}
#define HAL_SPI_DEVICE5 {"ramtron", "spi21", 2, 1, 3, 8000000U, 8000000U}
#define HAL_SPI_DEVICE6 {"ms5611", "spi41", 4, 1, 3, 20000000U, 20000000U}
#define HAL_SPI_DEVICE_LIST HAL_SPI_DEVICE0, HAL_SPI_DEVICE1, HAL_SPI_DEVICE2, HAL_SPI_DEVICE3, HAL_SPI_DEVICE4, HAL_SPI_DEVICE5, HAL_SPI_DEVICE6
#define HAL_SPI_DEVICE_COUNT 7

/* SPI device attach table for rt_hw_spi_device_attach() */
#define HAL_RTT_SPI_ATTACH_LIST \
    {"spi1", "spi11", GET_PIN(F, 2)}, \
    {"spi1", "spi12", GET_PIN(F, 3)}, \
    {"spi1", "spi13", GET_PIN(F, 11)}, \
    {"spi1", "spi14", GET_PIN(F, 4)}, \
    {"spi1", "spi15", GET_PIN(G, 10)}, \
    {"spi2", "spi21", GET_PIN(F, 5)}, \
    {"spi4", "spi41", GET_PIN(F, 10)}

#define HAL_INS_PROBE1  ADD_BACKEND(AP_InertialSensor_Invensense::probe(*this,hal.spi->get_device("icm20689"),ROTATION_NONE))
#define HAL_INS_PROBE2  ADD_BACKEND(AP_InertialSensor_Invensense::probe(*this,hal.spi->get_device("icm20602"),ROTATION_NONE))
#define HAL_INS_PROBE3  ADD_BACKEND(AP_InertialSensor_Invensensev3::probe(*this,hal.spi->get_device("icm42688"),ROTATION_PITCH_180_YAW_270))
#define HAL_INS_PROBE4  ADD_BACKEND(AP_InertialSensor_BMI055::probe(*this,hal.spi->get_device("bmi055_a"),hal.spi->get_device("bmi055_g"),ROTATION_ROLL_180_YAW_90))
#define HAL_INS_PROBE5  ADD_BACKEND(AP_InertialSensor_BMI088::probe(*this,hal.spi->get_device("bmi055_a"),hal.spi->get_device("bmi055_g"),ROTATION_ROLL_180_YAW_90))
#define HAL_INS_PROBE_LIST HAL_INS_PROBE1;HAL_INS_PROBE2;HAL_INS_PROBE3;HAL_INS_PROBE4;HAL_INS_PROBE5

#define HAL_BARO_PROBE1  ADD_BACKEND(AP_Baro_MS5611::probe(*this,hal.spi->get_device("ms5611")))
#undef AP_BARO_MS5611_ENABLED
#define AP_BARO_MS5611_ENABLED 1
#define HAL_BARO_PROBE_LIST HAL_BARO_PROBE1

#define HAL_PROBE_EXTERNAL_I2C_COMPASSES
#define HAL_RTT_I2C_BUS_NAMES "i2c3" , "i2c1" , "i2c2" , "i2c4"
#define HAL_MAG_PROBE_LIST PROBE_MAG_I2C(IST8310, 0, 0x0E, false, ROTATION_ROLL_180_YAW_90)
#define HAL_COMPASS_AUTO_ROT_DEFAULT 2
#define STM32F767xx 1
#define USE_HAL_DRIVER 1
#define HAL_STORAGE_SIZE 16384
#define HAL_WITH_RAMTRON 1
#define STORAGE_FLASH_PAGE 10
#define HAL_WITH_EKF_DOUBLE 0
#define HAL_LOGGING_FILESYSTEM_ENABLED 1
#define HAL_LOGGING_MAVLINK_ENABLED 0
#define AP_FILESYSTEM_POSIX_ENABLED 1
#define AP_FILESYSTEM_POSIX_HAVE_UTIME 0
#define AP_FILESYSTEM_POSIX_HAVE_STATFS 0
#define HAL_BOARD_LOG_DIRECTORY "/sd/APM/LOGS"
#define HAL_BOARD_TERRAIN_DIRECTORY "/sd/APM/TERRAIN"
#define HAL_BOARD_STORAGE_DIRECTORY "/sd/APM/STORAGE"

