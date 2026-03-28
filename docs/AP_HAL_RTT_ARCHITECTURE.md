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

## SPI DMA 架构

STM32F7 SPI1 使用自研 Low-Level DMA (LLD) 驱动，替代 HAL 库的 DMA 传输路径：

```
应用层 (ArduPilot SPIDevice)
    │
    ▼
drv_spi.c :: spixfer()
    ├── lld 指针存在？ ──▶ spi_lld_xfer()  [LLD 路径]
    │                        ├── 直接操作 DMA 寄存器
    │                        ├── RX ISR: 清标志 + rt_completion_done
    │                        └── 线程侧 poll BSY 后返回
    └── 否 ──▶ HAL_SPI_TransmitReceive_DMA()  [HAL 路径]
                 └── HAL ISR busy-wait (保留给低频总线)
```

关键设计：

- LLD 上下文 (`spi_lld_bus_t`) 在 `rt_board_init.c` 中静态分配并注册
- DMA 寄存器地址和 ISR 标志掩码在初始化时预计算，ISR 零开销
- `rt_completion` 用于线程同步，消除 ISR 内阻塞
- NVIC 由 HAL `stm32_spi_init()` 统一管理，LLD 不单独操作 NVIC

## 目录结构

```
libraries/AP_HAL_RTT/
├── hwdef/
│   ├── <board>/hwdef.dat         # 每板一个
│   └── scripts/rtt_hwdef.py      # 生成脚本
├── rtt_bsp_<board>/              # RT-Thread BSP 包
│   ├── SConstruct / SConscript
│   ├── board/ (rt_board_init.c, link.lds, CubeMX, ports/, drv_spi_lld.c/h, rtt_libc_compat.c)
│   ├── packages/ (CMSIS, HAL driver)
│   ├── pkgs_update_manual.sh
│   ├── dirent.h
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

## STM32F767 内存布局

```
0x08000000 ┌─────────────────────┐
           │  Bootloader (32KB)  │
0x08008000 ├─────────────────────┤
           │  Application        │
           │  (~1.2MB / 2MB)     │
0x08200000 └─────────────────────┘

0x20000000 ┌─────────────────────┐
           │  DTCM (128KB)       │  ← CPU only, DMA 不可访问
           │  .data / .bss       │
0x20020000 ├─────────────────────┤
           │  SRAM1 (384KB)      │  ← DMA 可访问
           │  RT-Thread Heap     │  ← HEAP_BEGIN = 0x20020000
           │  (线程栈、DMA buf)  │
0x20080000 └─────────────────────┘
```

关键约束：DTCM 仅 CPU 可访问，DMA 控制器无法读写。所有需要 DMA 访问的内存（SPI buffer、线程栈等）必须分配在 SRAM1 中。

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
