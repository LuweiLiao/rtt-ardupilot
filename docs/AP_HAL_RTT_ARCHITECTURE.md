# AP_HAL_RTT 架构设计

## 核心原则

**hwdef.dat 是唯一的板级真相源。** HAL 层代码 100% 板级无关。

## 架构概览

```
hwdef/<board>/hwdef.dat          ─── 板级硬件描述（引脚、SPI 设备、传感器）
       │
       ▼
rtt_hwdef.py                     ─── 解析 hwdef.dat，生成 hwdef.h
       │
       ▼
build/<board>/hwdef.h            ─── 编译时宏（SPI 设备表、probe 列表等）
       │
       ├──▶ SPIDeviceManager.cpp ─── HAL_SPI_DEVICE_LIST 宏驱动设备查找
       ├──▶ rt_board_init.c      ─── HAL_RTT_SPI_ATTACH_LIST 宏驱动 SPI 挂载
       └──▶ AP_InertialSensor    ─── HAL_INS_PROBE_LIST 宏驱动传感器探测
```

## 与 ChibiOS 的对照

| 方面 | ChibiOS | AP_HAL_RTT |
|------|---------|------------|
| hwdef 脚本 | `chibios_hwdef.py` 继承 `HWDef` | `rtt_hwdef.py` 继承 `HWDef` |
| SPI 设备表 | `SPIDesc` + `HAL_SPI_DEVICE_LIST` | `RTT_SPIDesc` + `HAL_SPI_DEVICE_LIST` |
| SPI CS 控制 | `PAL_LINE(GPIOx, pin)` | `GET_PIN(port, pin)` |
| SPI 设备注册 | ChibiOS 内核自动 | `rt_hw_spi_device_attach()` 显式调用 |
| SPI 设备名 | 内部 bus+devid | RT-Thread 字符串名 (如 "spi13") |
| RTOS 线程 | ChibiOS threads | RT-Thread threads |
| 互斥锁 | ChibiOS mutex | `rt_mutex_t` |
| UART | ChibiOS serial driver | `rt_device` (UART + USB CDC) |

## 目录结构

```
libraries/AP_HAL_RTT/
├── hwdef/
│   ├── <board>/hwdef.dat         # 每板一个
│   └── scripts/rtt_hwdef.py      # 生成脚本
├── rtt_bsp_<board>/              # RT-Thread BSP 包
│   ├── SConstruct / SConscript
│   ├── board/ (rt_board_init.c, link.lds, CubeMX, ports/)
│   ├── packages/ (CMSIS, HAL driver)
│   └── rtconfig.h / .config
├── SPIDevice.cpp/h               # 板级无关 SPI 驱动
├── SPIDeviceManager.cpp/h        # 使用 HAL_SPI_DEVICE_LIST
├── UARTDriver.cpp/h              # 使用 HAL_RTT_UART_DEVICE_LIST
├── Scheduler.cpp/h               # 板级无关
├── Semaphores.cpp/h              # rt_mutex 封装
├── DeviceBus.cpp/h               # 周期回调
├── system.cpp                    # panic/millis/micros
├── Util.cpp/h                    # 时间/内存工具
└── HAL_RTT_Class.cpp/h           # HAL 主类
```

## 逐驱动验证层

除运行时目录外，`AP_HAL_RTT` 还需要一层与治理系统配套的“逐驱动验证层”，用于把问题拆到驱动、总线、子系统级别，而不是每次都依赖整机 `Copter` bring-up。

推荐分层：

```text
libraries/AP_HAL/examples/        # 通用 HAL 语义验证
libraries/AP_HAL_RTT/examples/    # RTT 专属、依赖 BSP/hwdef 的验证
libraries/AP_HAL/tests/           # 主机侧纯逻辑测试
libraries/AP_HAL_RTT/tests/       # 仅在 RTT 专属对象可脱离硬件时引入
```

说明：

- `AP_HAL/examples/` 适合 `UART`、`Storage`、`BinarySem`、`RCOutput` 这类通用抽象能力
- `AP_HAL_RTT/examples/` 适合 `USB CDC`、`SPI attach`、`RTT 设备名`、`DeviceBus` 等 RTT 专属实现
- 完整的验证方法与门禁见 `docs/AP_HAL_RTT_DRIVER_VALIDATION.md`

## 新板适配流程

1. 创建 `hwdef/<boardname>/hwdef.dat`，定义 MCU、SPI 引脚、SPIDEV、IMU/BARO
2. 创建 `rtt_bsp_<boardname>/`，包含 RT-Thread BSP（board.c、link.lds、HAL driver 包）
3. 运行 `./waf configure --board rtt_<boardname> && ./waf copter`

无需修改任何 HAL 层 C++ 代码。

## RTT_SPIDesc 结构

```c
struct RTT_SPIDesc {
    const char *name;         // ArduPilot 设备名 (如 "icm20689")
    const char *rtt_devname;  // RT-Thread 设备名 (如 "spi13")
    uint8_t bus;              // SPI 总线号 (1,2,4...)
    uint8_t devid;            // 总线上的设备 ID
    uint8_t mode;             // SPI 模式 (0-3)
    uint32_t lowspeed;        // 低速频率 Hz
    uint32_t highspeed;       // 高速频率 Hz
};
```

## 非 STM32 扩展

- `rtt_hwdef.py` 的引脚解析抽象化，不假定 STM32 `Pxy` 格式
- BSP 包独立于 HAL 层，通过 `hwdef.dat` 中的 `MCU` 行区分芯片系列
- `GET_PIN()` 宏由 RT-Thread BSP 提供，不同芯片有不同实现
- SPI/I2C/UART 接口通过 RT-Thread 设备框架统一
