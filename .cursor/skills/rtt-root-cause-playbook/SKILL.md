---
name: rtt-root-cause-playbook
description: 用症状到根因的判别树排查 AP_HAL_RTT 常见问题，适用于用户提到 Error_Handler、HardFault、枚举成功但没参数、SPI 传输异常、主循环频率异常、WSL2 USB 问题或“别死抠细节”时。
---

# RTT Root Cause Playbook

## 先归层

先判断问题属于哪一层：
- 启动链
- HAL 抽象
- BSP / 驱动
- 构建链
- 主机环境

## 常见症状

### 进 `_Error_Handler` 或很早 HardFault
- 先查时钟、bootloader 跳转、`VTOR/MSP/PSP`
- 再查首次线程切换与栈/异常返回路径

### USB 枚举成功但无参数
- 先确认 `setup` 是否走完
- 先确认 CPU 是否还在运行，而不是停在 GDB halt
- 再区分固件问题还是 WSL2 环境问题

### SPI 传输有动作但 probe 失败
- 先查设备表、CS、同步语义、bus lock
- 不要第一反应就怀疑硬件坏

### 主循环频率明显偏低
- 先查调度架构、等待精度、周期线程模型
- 再查单次传输耗时与线程优先级

### 运行约 30 秒后 HardFault (BFSR.STKERR / Bus Error)
- **首先检查**：DMA buffer 是否分配在 DTCM (0x20000000-0x2001FFFF)
- STM32F767 的 DTCM 仅 CPU 可访问，DMA 控制器无法读写
- 若 `HEAP_BEGIN` 在 DTCM 内（如 `&__bss_end`），`rt_malloc` 分配的 DMA buffer 会静默失败
- **验证**：GDB 中检查崩溃线程的栈指针地址，若在 0x2000xxxx 范围则是 DTCM 问题
- **修复**：确保 `HEAP_BEGIN = 0x20020000`（SRAM1 起始）
- 若非 DTCM 问题，再查线程栈溢出（`list thread` 看栈使用率）

### CPU 持续爆红 / SPI 传输 CPU 开销过高
- 检查 SPI DMA ISR 是否有 busy-wait（`while (FTLVL)` / `while (BSY)`）
- STM32 HAL 的 `HAL_SPI_IRQHandler` 在 ISR 内等待 FIFO 和 BSY 清零，高频传输时占满 CPU
- **解决方案**：启用 SPI LLD 驱动 (`drv_spi_lld`)，将 BSY 等待移到线程上下文
- 验证：`rtt_dbg_main_loop_iterations` 每秒增量应 ~400

## 收敛规则

- 每轮只保留一个主假设
- 连续三轮没有新增证据，就换验证方法或升维到上一层
- 若现象更像环境问题，要明确隔离边界，不继续深抠固件

## 与逐驱动验证的关系

- 如果某问题已经有对应 driver-validation example 或 smoke，先回到那一层验证
- driver-validation 负责回答“这一层怎么证明通过”，本 playbook 负责回答“失败后往哪一层回退”
- 若整机现象与驱动级结果矛盾，优先检查层级边界是否被混淆
