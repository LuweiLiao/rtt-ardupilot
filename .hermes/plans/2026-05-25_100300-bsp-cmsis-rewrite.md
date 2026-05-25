# Plan: BSP CMSIS 重写 + Libraries 还原

## Goal

将 ArduPilot RTT 移植从"修补模式"切换到"移植模式"：
1. 所有 BSP 层驱动换成 CMSIS 寄存器直写，消除 STM32 HAL 依赖
2. CherryUSB CDC ACM 替换为 ChibiOS USB LLD
3. Libraries 修改全部还原为 ArduPilot master
4. 修改止步于 `AP_HAL_RTT` 层

## Current State

### 已污染文件（需 revert）

| 文件 | 已改内容 |
|------|----------|
| `ArduCopter/esc_calibration.cpp` | 非 AP_HAL_RTT，**必须还原** |
| `ArduCopter/system.cpp` | 非 AP_HAL_RTT，**必须还原** |
| `libraries/AP_InertialSensor/AP_InertialSensor.cpp` | 添加了空探针宏，**必须还原** |
| `libraries/AP_Vehicle/AP_Vehicle.cpp` | 非 AP_HAL_RTT，**必须还原** |
| `Tools/scripts/scons_ardupilot_sources.py` | 非 AP_HAL_RTT，**必须还原** |

### BSP 层文件清单（modules/rt-thread/bsp/stm32/stm32f765-cuav-v5/board/）

| 文件 | 当前依赖 | 目标 |
|------|----------|------|
| `board.c` | CubeMX HAL | CMSIS 寄存器 |
| `drv_spi_lld.c` | HAL + RT-Thread 框架 | CMSIS 寄存器 + RTT 设备接口 |
| `drv_usart_ll.c` | HAL UART | CMSIS USART 寄存器 |
| `drv_flash_ll.c` | HAL Flash | CMSIS Flash 寄存器 |
| `drv_gpio_ll.c` | HAL GPIO | CMSIS GPIO 寄存器 |
| `drv_common_ll.c` | HAL 各种 | CMSIS 直写 |
| `stm32f7xx_hal_msp.c` | **纯 HAL** | **删掉**，GPIO 配到 drv_gpio_ll |
| `stm32f7xx_it.c` | HAL ISR | CMSIS ISR |
| `rt_board_init.c` | RT-Thread 板级 | **精简** |
| `stm32f7_clock_ll.c` | HAL RCC | CMSIS RCC 寄存器 |
| `ports/cherryusb/usb_irq.c` | CherryUSB | **替换为 ChibiOS USB LLD** |
| `ports/cherryusb/cherryusb.c` | CherryUSB | **替换** |

### CherryUSB 文件
- `modules/rt-thread/components/drivers/usb/cherryusb/port/dwc2/usb_dc_dwc2.c` — **整个替换为 ChibiOS USB LLD DWC2 驱动**

### AP_HAL_RTT 层（正确的修改边界）

| 文件 | 状态 |
|------|------|
| `SPIDevice.cpp` | ✅ 已 CMSIS 直写，保留 |
| `AnalogIn.cpp` | ✅ 已 CMSIS 直写，保留 |
| `HAL_RTT_Class.cpp` | ✅ 已 CMSIS，保留（删除 DeviceBus 热身） |
| `I2CDevice.cpp` | ❌ 走 rt_i2c_transfer()，需改为 CMSIS I2C 直写 |
| `UARTDriver.cpp` | ❌ 走 RT-Thread 串口框架，需改为 CMSIS USART 直写 |
| 其他 .cpp | ⚪ 待审计 |

### USB CDC 状态
- `drivers/hal_usb_lld_rtt.c` — **已存在**（724 行 CMSIS DWC2 轮询模式）
- `drivers/hal_usb_lld_rtt.h` — **已存在**
- CherryUSB `usb_dc_dwc2.c` — 当前编译入口，**替换为 hal_usb_lld_rtt**

## Step-by-Step Plan

### Phase 0: 还原 Libraries（5 步）

1. **还原 5 个污染文件**：
   ```bash
   cd /data/firmare/pogo-apm
   git checkout HEAD -- \
     ArduCopter/esc_calibration.cpp \
     ArduCopter/system.cpp \
     libraries/AP_InertialSensor/AP_InertialSensor.cpp \
     libraries/AP_Vehicle/AP_Vehicle.cpp \
     Tools/scripts/scons_ardupilot_sources.py
   ```

2. **确认 scons 编译正常**（先不动 BSP）

### Phase 1: BSP GPIO + Clock CMSIS 重写（5 步）

3. **`drv_gpio_ll.c`** — CMSIS 寄存器直写，参考 ChibiOS `GPIO.cpp` LLD 风格
   - 替换所有 `HAL_GPIO_*` 调用为 `GPIOx->MODER/AFR/BSRR`
   - 实现：`rtt_gpio_set_mode()`, `rtt_gpio_write()`, `rtt_gpio_read()`
   
4. **`stm32f7_clock_ll.c`** — CMSIS RCC 寄存器
   - PLL = 216MHz (HSE 8MHz → x27 = 216MHz → /2 PLLP = 108MHz HCLK 实际配置)
   - RCC->CR/PLLCFGR/CFGR 寄存器直写
   - AHB1/APB1/APB2 各外设时钟使能

5. **`board.c`** — 删除所有 `HAL_Init()`、`HAL_RCC_*` 调用
   - 替换为 `stm32f7_clock_ll.c` 的 clock_init()
   - 保留 RT-Thread 必须的 `rt_hw_board_init()` 调用

6. **`stm32f7xx_hal_msp.c`** — **整个文件删除**，外设 GPIO 配置移到各自 LLD 文件中

### Phase 2: BSP 外设 CMSIS 重写（6 步）

7. **`drv_spi_lld.c`** — CMSIS SPI 寄存器直写
   - SPI1/SPI4 的 CR1/CR2/DR/SR 寄存器操作
   - 轮询模式（与 SPIDevice.cpp 现有 CMSIS 路径对齐）

8. **`drv_usart_ll.c`** — CMSIS USART 寄存器直写
   - USART1-6 的 CR1/CR2/CR3/DR/SR 寄存器
   - 轮询 TX/RX + RXNE 中断接收

9. **`drv_flash_ll.c`** — CMSIS Flash 寄存器
   - FLASH->ACR（等待周期）
   - 锁、擦除、编程

10. **`drv_common_ll.c`** — 系统级 CMSIS
    - SCB->VTOR（向量表重定向）
    - SysTick 配置

11. **`stm32f7xx_it.c`** — CMSIS NVIC 直写
    - ISR 函数保留，替换 HAL 调用为 CMSIS

12. **`rt_board_init.c`** — 精简板级初始化
    - 删除 HAL 调用
    - 只保留 RT-Thread 必要的初始化和设备注册

### Phase 3: USB LLD 替换 CherryUSB（3 步）

13. **将 CherryUSB 从编译移除**：
    - 修改 Kconfig/SConscript 跳过 `cherryusb/cdc_acm.c` 等

14. **激活 `drivers/hal_usb_lld_rtt.c`**（724 行，已存在）
    - 将其注册为 RT-Thread USB 设备或直接由 HAL_RTT_Class 调用
    - 参考 ChibiOS `hal_usb_lld.c` 的数据流：`_write()` → `_start_xmit()` → DWC2 寄存器

15. **删除 CherryUSB 端口文件**：
    - `board/ports/cherryusb/usb_irq.c`
    - `board/ports/cherryusb/cherryusb.c`

### Phase 4: AP_HAL_RTT 层对齐（3 步）

16. **`I2CDevice.cpp`** — 删掉 `rt_i2c_transfer()` 调用
    - 改为 CMSIS I2C 寄存器直写（I2C1-3 的 CR1/CR2/DR/SR1/SR2）
    - 参考 ChibiOS `i2c_lld.c`

17. **`UARTDriver.cpp`** — 删掉 `rt_device_read/write` 调用
    - 改为 CMSIS USART 寄存器直写

18. **清理 `HAL_RTT_Class.cpp`**：
    - 删除 DeviceBus 热身代码（已不需要）
    - 验证没有 HAL 依赖

### Phase 5: 编译 + 验证（3 步）

19. **编译**：`scons --v=ArduCopter --target=cuav_v5 -j$(nproc)`
20. **烧录**：OpenOCD telnet verify
21. **验证**：
    - hal_run=0x11111111
    - setup_stage>0x230
    - ADC 计数 > 0
    - main_loop_iterations > 0
    - USB CDC /dev/ttyACM1 枚举
    - MAVLink 心跳

## 风险与权衡

1. **Drv_spi 替换风险**：RT-Thread 设备框架依赖 drv_spi 的 rt_spi_device_attach 注册。如果 drv_spi 被删，需确保 SPIDevice.cpp 的 CMSIS 路径（_dev==nullptr）独立工作。

2. **USB 中断冲突**：CherryUSB 用 USBD_OTG_IRQHandler，ChibiOS USB LLD 也用同一 IRQ。DWC2 寄存器状态在切换时需干净初始化。

3. **I2C 时序**：CMSIS I2C 直写需精确遵循 STM32F767 I2C 时序要求（SCL 频率、上升时间），参考 ChibiOS i2c_lld.c 中的忙等待逻辑。

4. **回归风险**：任何一步编译失败时，`git checkout` 回退还原的文件后 AP_HAL_RTT 仍保留 CMSIS 代码，模块化隔离干净。

## 文件变动清单

### 删除
- `modules/rt-thread/bsp/stm32/stm32f765-cuav-v5/board/CubeMX_Config/Src/stm32f7xx_hal_msp.c`
- `modules/rt-thread/bsp/stm32/stm32f765-cuav-v5/board/ports/cherryusb/usb_irq.c`
- `modules/rt-thread/bsp/stm32/stm32f765-cuav-v5/board/ports/cherryusb/cherryusb.c`
- `modules/rt-thread/components/drivers/usb/cherryusb/port/dwc2/usb_dc_dwc2.c`

### 重写
- `board/drivers_ll/drv_gpio_ll.c` — CMSIS
- `board/drivers_ll/drv_spi_ll.c` — CMSIS  
- `board/drivers_ll/drv_usart_ll.c` — CMSIS
- `board/drivers_ll/drv_flash_ll.c` — CMSIS
- `board/drivers_ll/drv_common_ll.c` — CMSIS
- `board/drivers_ll/stm32f7_clock_ll.c` — CMSIS
- `board/board.c` — 去 HAL
- `board/rt_board_init.c` — 精简
- `board/CubeMX_Config/Src/stm32f7xx_it.c` — CMSIS

### 还原（git checkout HEAD --）
- `ArduCopter/esc_calibration.cpp`
- `ArduCopter/system.cpp`
- `libraries/AP_InertialSensor/AP_InertialSensor.cpp`
- `libraries/AP_Vehicle/AP_Vehicle.cpp`
- `Tools/scripts/scons_ardupilot_sources.py`

### AP_HAL_RTT 修改
- `I2CDevice.cpp` — CMSIS 直写
- `UARTDriver.cpp` — CMSIS 直写
- `HAL_RTT_Class.cpp` — 清理 DeviceBus 热身
- `UARTDriver.h` — 可能需改接口

## 验证方法

每 Phase 完成后编译 + 烧录 + OpenOCD 探针验证。

成功标准：
```
hal_run           = 11111111  (setup 完成)
setup_stage       = 00000236+ (setup 阶段数)
main_loop         = N (>0)    (有主循环滴答)
fast_loop         = N (>0)    (快速循环运行)
adc_conv_count    = N (>0)    (ADC DMA 采样)
/dev/ttyACM1      = 枚举       (USB CDC)
MAVLink heartbeat = 存在       (mavproxy 可连)
```
