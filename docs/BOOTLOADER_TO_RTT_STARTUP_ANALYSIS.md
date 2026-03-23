# Bootloader → RTT 启动链深度分析（五人小组讨论）

本文从五个视角对「烧录后仍不工作 / USB 串口不出现」做系统梳理，涵盖 ChibiOS bootloader、中断与向量表、跳转与启动代码、RTT 调度、以及外设/USB 初始化顺序，并给出结论与建议。

---

## 1. ChibiOS Bootloader 启动与跳转逻辑

**负责视角**：Bootloader 行为与跳转约定。

### 1.1 流程摘要

- Bootloader 位于 `0x08000000`，占用 **128KB**（`FLASH_BOOTLOADER_LOAD_KB 128`，见 `libraries/AP_HAL_RTT/hwdef/pixhawk6c_mini/hwdef-bl.dat`）。
- 应用区起始：`APP_START_ADDRESS = 0x08000000 + 128*1024 = 0x08020000`（与 `bl_protocol.cpp` 中 `APP_START_ADDRESS` 一致）。
- 跳转前 bootloader 会：
  1. 校验应用区前若干字非 0xFFFFFFFF，且 `app_base[1]`（Reset_Handler）在合法 Flash 范围内；
  2. （STM32H7）关闭 D-Cache、I-Cache（`SCB_DisableDCache/ICache`）；
  3. 关闭外设时钟（如 `rccDisableAPB1L/APB1H/APB2`）、复位 OTG_FS（`rccResetOTG_FS()`）；
  4. `port_disable()` 关闭所有中断；
  5. **写 VTOR**：`*(volatile uint32_t *)SCB_VTOR = APP_START_ADDRESS`（0x08020000）；
  6. `do_jump(app_base[0], app_base[1])`：SP = app_base[0]，PC = app_base[1]（即应用向量表栈顶 + Reset_Handler）。

### 1.2 与应用的约定

- 应用必须在 **0x08020000** 处放置**向量表**（首字 = 初始 SP，次字 = Reset_Handler）。
- 应用链接脚本 ROM 必须 `ORIGIN = 0x08020000`（当前 `link.lds` 已满足）。
- 跳转后 VTOR 已指向 0x08020000，应用若再次设置 VTOR 为同一值，是幂等的。

### 1.3 风险点

- 若烧录的是错误固件或未按 0x08020000 布局，bootloader 可能不跳转或跳转到错误地址。
- H7 上 bootloader 关闭了 Cache，应用若依赖 Cache 且未在合适时机重新使能，可能带来性能或一致性问题的次要影响（一般不会直接导致「完全不工作」）。

---

## 2. 中断与向量表（VTOR / Reset_Handler）

**负责视角**：向量表位置与第一次取指/中断。

### 2.1 当前设计

- **Bootloader 跳转时**已把 VTOR 设为 `0x08020000`，因此 CPU 第一次取指和后续所有异常/中断都会走应用在 0x08020000 处的向量表。
- **应用侧**：
  - `stm32h7xx_hal_conf.h`（BSP）中定义 `USER_VECT_TAB_ADDRESS`、`VECT_TAB_BASE_ADDRESS 0x08020000UL`、`VECT_TAB_OFFSET 0`。
  - `system_stm32h7xx.c`（packages）在 `SystemInit()` 里 `#if defined(USER_VECT_TAB_ADDRESS)` 时执行 `SCB->VTOR = VECT_TAB_BASE_ADDRESS | VECT_TAB_OFFSET`。
  - `rt_board_init.c` 在 `rt_hw_board_init()` 开头再次设置 `SCB->VTOR = APP_FLASH_BASE`，保证即使某处改过 VTOR 也能恢复。

### 2.2 向量表内容来源

- `.isr_vector` 由 `startup_stm32h743xx.s`（packages 里 GCC 版）提供，`KEEP(*(.isr_vector))` 保证其位于 0x08020000。
- 第二项为 `Reset_Handler`，与 bootloader 的 `do_jump(..., app_base[1])` 一致。

### 2.3 风险点

- 若编译 `system_stm32h7xx.c` 时**未**定义 `USER_VECT_TAB_ADDRESS`（例如未包含 BSP 的 `stm32h7xx_hal_conf.h` 或未通过全局宏传入），则 SystemInit() 不会写 VTOR，此时仅依赖 bootloader 已写的一次；若某段代码在启动早期改过 VTOR，可能出错。建议确认 BSP 的编译选项/包含路径使 `USER_VECT_TAB_ADDRESS` 对 `system_stm32h7xx.c` 可见。
- 若 link.lds 的 ROM 起始不是 0x08020000，则向量表不在约定位置，bootloader 跳转后行为未定义。

---

## 3. 跳转代码与系统启动链（Reset_Handler → entry → rtthread_startup）

**负责视角**：从 Reset 到 RTT 内核运行的完整 C 执行路径。

### 3.1 实际执行顺序

1. **Reset_Handler**（`startup_stm32h743xx.s`）：设 SP、`ExitRun0Mode`、`SystemInit`、复制 .data、清零 .bss、`__libc_init_array`、**`bl entry`**（不调 `main`）。
2. **entry()**（`components.c`，GCC + `RT_USING_USER_MAIN`）：仅调用 `rtthread_startup()`。
3. **rtthread_startup()**：关中断 → **rt_hw_board_init()** → rt_show_version → rt_system_timer_init → rt_system_scheduler_init → rt_application_init（创建 main 线程，入口为 main_thread_entry）→ rt_system_timer_thread_init → rt_thread_idle_init → rt_thread_defunct_init → **rt_system_scheduler_start()**（不再返回）。
4. 调度器运行后，**main 线程**执行 `main_thread_entry` → `rt_components_init()` → **main()**（ArduPilot 的 main 或 BSP 的 applications/main.c）。

### 3.2 关键点

- 当前 BSP 使用的 `startup_stm32h743xx.s`（GCC）末尾是 `bl entry`，与 RT-Thread 的 `RT_USING_USER_MAIN` + GCC 的 `entry()` 设计一致，无需再改 startup。
- `rt_hw_board_init()` 是 RTT 里**最早**的板级 C 入口，其中第一件事就是 `SCB->VTOR = APP_FLASH_BASE`，再 `HAL_Init()`、`SystemClock_Config()`、pin/usart 等，顺序合理。

### 3.3 风险点

- 若某处链接了错误的 startup（例如调 `main` 而不是 `entry`），则不会进入 `rtthread_startup()`，RTT 和 ArduPilot 都不会跑。
- 若 `rt_application_init()` 未执行或 main 线程未创建就调用了 `rt_system_scheduler_start()`，会表现为系统挂起或只跑 idle。

---

## 4. RTT 调度与 entry 启动

**负责视角**：调度器何时启动、main 线程何时运行。

### 4.1 顺序

- 在 `rtthread_startup()` 中，**先** `rt_application_init()`（创建并 startup main 线程），**再** `rt_system_scheduler_start()`。
- 因此调度器启动时，main 线程已在就绪队列，会按优先级被调度；其入口 `main_thread_entry` 会做 `rt_components_init()` 再调 `main()`。

### 4.2 与 USB/Shell 的关系

- USB 设备（CDC）和 Finsh 通常在 `rt_components_init()` 或后续 `INIT_*_EXPORT` 阶段初始化。
- 若在 `rt_hw_board_init()` 里就创建了 LED 闪烁等线程，它们会与 main 线程一起参与调度；只要不阻塞或占用关键资源，不会阻止 main 线程运行和 USB 初始化。

### 4.3 风险点

- 若堆未正确初始化（例如 `rt_system_heap_init` 未在 `rt_hw_board_init` 中调用或范围错误），`rt_thread_create("main", ...)` 可能失败，导致没有 main 线程，表现类似「启动后无任何输出、USB 不枚举」。
- 当前 BSP 的 `rt_hw_board_init` 未显式调 `rt_system_heap_init`；若 RTT 通过其他方式（如链接脚本 + 弱符号）提供堆，需确认其与 link.lds 的 RAM 布局一致。

---

## 5. 外设/时钟/USB 初始化顺序

**负责视角**：时钟、HAL、USB、中断的先后顺序与依赖。

### 5.1 当前顺序（rt_board_init.c）

1. VTOR 重定向  
2. `HAL_Init()`  
3. `SystemClock_Config()`（含 HSE/PLL、HSI48 等，USB 依赖此时钟）  
4. `rt_hw_pin_init()`  
5. `rt_hw_usart_init()`  
6. LED 与 LED 线程  

### 5.2 USB（CherryUSB DWC2）

- USB 设备初始化一般在 components 或设备驱动层（如 `cdc_acm_chardev_init`），在 `rt_hw_board_init()` **之后**。
- BSP 已提供**强符号** `OTG_FS_IRQHandler`（`board/ports/cherryusb/usb_irq.c`），直接调 `USBD_IRQHandler(0)`，避免 upstream `usb_glue_st.c` 中在 `g_usb_dwc2_irq[0]` 尚未注册时被调用导致 HardFault。
- 若 USB 枚举仍失败，可排查：HSI48/USB 时钟是否在 `SystemClock_Config` 中正确使能、VBUS/DP/DM 引脚、以及 OTG_FS 中断是否在 NVIC 使能（通常由 CherryUSB 初始化时使能）。

### 5.3 风险点

- **H7 Cache**：Bootloader 关闭了 D-Cache/I-Cache；若应用或 HAL 在未正确 invalidate/clean 的情况下使用 Cache，可能出现数据不一致。可在确认 VTOR/向量表无问题后，在 `SystemInit` 或 `rt_hw_board_init` 之后按需重新使能 Cache 并做必要维护。
- **link.lds**：当前脚本注释为「STM32F4xx」，但 ORIGIN/LENGTH 与 H743 的 0x08020000/1920K、RAM 0x24000000/512K 一致。建议确认 H743 的 DTCM/AXI SRAM 等是否需单独 region，以及 `_estack` 与各段是否与芯片手册一致，避免栈或堆越界导致不可预测行为。

---

## 综合结论与建议（小组一致意见）

1. **Bootloader 与 VTOR**  
   - Bootloader 已按 0x08020000 写 VTOR 并跳转到应用向量表；应用侧也在 `SystemInit` 与 `rt_hw_board_init` 中再次设置 VTOR，逻辑一致。  
   - 建议：确认编译 `system_stm32h7xx.c` 时能拿到 `USER_VECT_TAB_ADDRESS` 和 `VECT_TAB_BASE_ADDRESS`（通过 BSP 的 hal_conf 或全局 -D）。

2. **启动链**  
   - Reset_Handler → SystemInit → .data/.bss → entry → rtthread_startup → rt_hw_board_init → … → rt_system_scheduler_start → main 线程 → main()，链条完整且与 RT_USING_USER_MAIN 匹配。  
   - 建议：若仍怀疑「未进应用」，可在 `entry()`、`rt_hw_board_init()` 首行、`main_thread_entry` 等处加简单 LED 或 GPIO 翻转，用示波器/逻辑分析仪确认执行到哪一步。

3. **USB 不出现**  
   - 已通过 BSP 内强符号 `OTG_FS_IRQHandler` 避免早期中断调用空指针。  
   - 建议：继续确认 USB 时钟（HSI48/OTG 时钟）、NVIC 中 OTG_FS 中断使能、以及 Finsh/控制台是否绑定到 USB CDC 设备；必要时在 USB 初始化路径加日志或 LED 指示。

4. **堆与 link.lds**  
   - 建议：检查 RTT 堆的起始/结束是否与 link.lds 中 RAM 使用一致；核对 H743 的 RAM 区域划分（含 DTCM、AXI SRAM 等），避免栈或堆侵入非法区域。

5. **调试手段**  
   - 若可连接调试器：在 Reset_Handler、entry、rt_hw_board_init、main_thread_entry、main 处设断点，确认 PC 是否按预期推进；查看 SCB->VTOR、SP、以及 OTG_FS 相关寄存器。  
   - 若仅有 LED：用不同闪烁模式表示「进 Reset_Handler」「进 rt_hw_board_init」「进 main」「进 USB 初始化」等阶段，缩小问题段。

按上述五点逐项核对与加观测点，应能定位「烧录后仍不行」是停在 bootloader、向量表、RTT 启动、调度，还是外设/USB 初始化阶段，并做针对性修改。

---

## 使用 OpenOCD + ST-Link 调试（由 Agent 协助定位）

仓库中已加入脚本，便于用 OpenOCD + GDB 在关键断点停住并查看 PC、VTOR、backtrace，从而判断卡在哪一阶段。

### 前提

- 本机已安装 **OpenOCD**（例如 `apt install openocd` 或从 openocd.org 安装）。
- **ST-Link** 已连接板子与电脑，板子供电。
- 已成功编译一次 RTT 固件，存在 `build/rtt_deploy/pixhawk6c_mini/rtthread.elf`（若未编译，先执行 `scons --v=ArduCopter --target=pixhawk6c-mini`）。

### 步骤（两终端）

**终端 1：启动 OpenOCD**

```bash
cd /path/to/pogo-apm
openocd -f scripts/openocd_stlink_h7.cfg
```

看到 `stm32h7x.cfg` 与 `target halted` 等信息即表示连接成功，保持此终端不关。

**终端 2：启动 GDB 并连上 OpenOCD**

```bash
cd /path/to/pogo-apm
arm-none-eabi-gdb build/rtt_deploy/pixhawk6c_mini/rtthread.elf -x scripts/rtt_debug.gdb
```

脚本会：连到 `:3333`、在 Reset_Handler / entry / rt_hw_board_init / rtthread_startup / main_thread_entry / main / OTG_FS_IRQHandler / HardFault_Handler 等处下断点，然后执行 `continue`。程序会在**第一个命中的断点**停下，并打印 `info reg` 与 `backtrace`。

### 如何解读

- 若停在 **Reset_Handler**：说明 bootloader 跳转和取指正常，可 `continue` 看下一次停在哪。
- 若停在 **entry** 或 **rt_hw_board_init**：说明 C 启动链和板级初始化已执行到 RTT。
- 若停在 **main_thread_entry** 或 **main**：说明调度器已启动、main 线程已跑。
- 若停在 **HardFault_Handler** / **UsageFault_Handler**：说明发生异常，需看 `backtrace` 和 `info reg`（尤其 LR、PC）判断是哪条指令/函数导致。
- 若从未停在任何断点且程序像“跑飞”：可能是 VTOR/向量表或链接地址与运行地址不一致，可在 GDB 中执行 `x/1x 0xE000ED08` 查看 VTOR 当前值（应为 0x08020000）。

把**第一次停住的断点名称**和 GDB 输出的 `backtrace`、`info reg` 贴给 Agent，即可进一步精确定位问题。
