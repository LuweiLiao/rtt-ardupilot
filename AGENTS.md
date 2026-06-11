---
description: RTT (RT-Thread) ArduPilot 移植项目 — 稳定性攻坚规则、编译烧录调试指南、诊断修复验证工作流
alwaysApply: true
---

# RTT ArduPilot 移植 — 攻坚规则与操作指南

## 1. RTT 稳定性攻坚驱动规则

### 核心原则
1. **不达目标不停歇** — 一旦进入调试/修复，必须持续工作直到目标达成，每完成一步自动规划下一步
2. **禁止中途弹窗问方案** — AI 自主选最优方案，只对物理操作（接线、按按钮、卸螺旋桨）询问用户
3. **双重验证标准** — 必须通过 CDC MAVLink（pymavlink 收到心跳且状态进入 STANDBY）+ OpenOCD GDB（halt/step/resume 无 HardFault），两条缺一不可
4. **每次修复必须可检验** — 修改→编译→烧录→双重验证，修复失败回退换方案，不堆积无效代码

### 调试目标等级
| 等级 | 要求 |
|------|------|
| **L0** | 飞控启动保持运行：OpenOCD 可连、Bootloader 跳转、无 HardFault、USB CDC 枚举、MAVLink 心跳持续、状态从 BOOT 进入 STANDBY |
| **L1** | 传感器与数据流：IMU 数据(RAW_IMU)、姿态(ATTITUDE)、电压/电流(SYS_STATUS) |
| **L2** | 功能完整：RC 输入、GPS 数据、参数读写、日志记录 |

### 禁止事项
- 无 OpenOCD 验证 → 不声称"修复完成"
- 无 CDC MAVLink 验证 → 不声称"运行正常"
- 禁止一次性修改多个模块后编译
- 禁止未确认根因就切换模块
- 禁止堆积未经验证的代码到 `staging/pogo-rtt`

### USB 崩溃因果关系
- **飞控 USB CDC 消失 = 固件崩溃/卡死**，不是 USB 总线问题
- 不要修复主机 USB 侧（hub 复位、重新枚举是徒劳的）
- 正确做法：修复固件代码 → 重新编译烧录 → 固件运行后 USB 自动恢复

### 命令自动执行
- 编译、烧录、调试命令自动执行，无需确认
- 使用 `-j$(nproc)` 全速并行编译
- USB 断开时禁止要求用户拔插，优先使用 `usb unbind/bind` 恢复

---

## 2. 编译与烧录

### 编译固件
```bash
# ArduCopter
scons --v=ArduCopter --target=cuav_v5 -j$(nproc)
scons --v=ArduCopter --target=pixhawk6c-mini -j$(nproc)
# 其他机型
scons --v=ArduPlane --target=cuav_v5 -j$(nproc)
scons --v=Rover --target=cuav_v5 -j$(nproc)

# ❌ 禁止 waf
```

### 产物路径
| 产物 | 路径 |
|------|------|
| bin 固件 | `build/rtt_cuav_v5/rtthread.bin` |
| ELF (含符号) | `build/rtt_deploy/cuav_v5/rt-thread.elf` |
| APJ 烧录包 | `build/rtt_deploy/cuav_v5/arducopter.apj` |

### 烧录方式
```bash
# 一键编译+烧录（PX4 bootloader 协议）
scons --v=ArduCopter --target=cuav_v5 --upload --port=/dev/ttyACM0

# OpenOCD 直接烧录（调试用）
openocd -f interface/stlink.cfg -f target/stm32f7x.cfg \
  -c "program build/rtt_cuav_v5/rtthread.bin 0x08008000" -c "reset run"

# Pixhawk6C Mini → 0x08020000
openocd -f interface/stlink.cfg -f target/stm32h7x_dual_bank.cfg \
  -c "program build/rtt_pixhawk6c_mini/rtthread.bin 0x08020000" -c "reset run"
```

### Bootloader / IO firmware
- Bootloader 使用 ArduPilot 原生固件（CUAV V5: `Tools/bootloaders/cuav-v5-bl.bin` → `0x08000000`）
- IO firmware 由 RTT 固件启动时自动上传到 IOMCU
- **禁止修改 Bootloader 和 IO firmware**

### 板级信息
| 板级 | target 别名 | MCU | 应用入口 | Bootloader 预留 |
|------|------------|-----|---------|----------------|
| CUAV V5 | `cuav_v5` / `cuav-v5` | STM32F767 | `0x08008000` | 前 32KB |
| Pixhawk6C Mini | `pixhawk6c_mini` / `pixhawk6c-mini` | STM32H743 | `0x08020000` | 前 128KB |

---

## 3. 诊断-修复-验证工作流

### 阶段 A：诊断
```
循环直到找到根因：
  1. OpenOCD halt → 查 PC 位置 → 判断当前执行到哪里
  2. PC 在 0x08000000 范围 → bootloader 阶段
  3. PC 在 0x08008000 范围 → 应用固件阶段
  4. 查 HardFault 状态 → 记录 ESR
  5. 查 RT-Thread 线程状态 → info threads + rt_thread_current
  6. CDC 收心跳 → 看 STATUS_TEXT 输出到哪一步
  7. 对比上次运行记录，判断进展还是退步
```

### 阶段 B：修复
```
每次只改一个模块，改完立即编译+烧录+验证：
  1. HardFault → 分析 ESR → 定位代码 → 修复
  2. 初始化卡住 → 查 init_ardupilot 中哪一步超时/死锁
  3. USB CDC 不工作 → 查 CherryUSB 配置/中断处理
  4. Scheduler 不跑 → 查 RT-Thread 线程创建/调度
  5. 外设不工作 → 查 SPI/I2C/UART 驱动配置
```

### 阶段 C：验证（双重验证，缺一不可）
```
OpenOCD:
  1. OpenOCD 连接 → 检测到 STM32F7/H7
  2. 复位后 halt → PC 在预期位置
  3. resume → 等待 10 秒

CDC MAVLink:
  4. pymavlink wait_heartbeat timeout=15 → 收到 HEARTBEAT
  5. status=STANDBY 或 ACTIVE
  6. 收到 STATUS_TEXT → 显示传感器初始化完成
  7. 运行 30 秒 → 无 HardFault → 消息流稳定
```

### 每次操作后记录
```json
{"时间": "", "操作": "诊断/修复/验证", "修改文件": [], "OpenOCD结果": "", "CDC结果": "", "下一步": ""}
```

---

## 4. GDB 调试速查

### HardFault 分析
```gdb
(gdb) b HardFault_Handler
(gdb) continue
# 触发后
(gdb) monitor reg esr    # 查看错误类型
(gdb) bt                  # 调用栈
(gdb) x/8w $sp            # 异常帧: R0,R1,R2,R3,R12,LR,PC,xPSR
(gdb) set $sp = $psp      # 若在线程中
(gdb) bt
```

### 常用 ESR 类型
| 值 | 含义 |
|----|------|
| `0x01` | INVSTATE — 指令访问越界 |
| `0x08` | PRECISERR — 精确总线错误（访问外设无效地址） |
| `0x09` | IMPRECISERR — 不精确总线错误（常见：DMA 写入非法地址） |
| `0x0C/0x0E` | 压栈失败（栈溢出） |
| `0x18` | DIVBYZERO — 除零错误 |

### RT-Thread 线程调试
```gdb
(gdb) info threads
(gdb) thread N
(gdb) bt
(gdb) p (char*)((struct rt_thread*)rt_thread_current)->name
```

---

## 5. 串口映射 (CUAV V5)
| 端口 | UART | 用途 |
|------|------|------|
| SERIAL0 | USB CDC | GCS/MAVROS |
| SERIAL1 | USART2 | TELEM1 |
| SERIAL2 | USART3 | TELEM2 |
| SERIAL3 | USART1 | GPS1 |
| SERIAL4 | UART4 | GPS2 |
| SERIAL5 | USART6 | TELEM3 |
| SERIAL6 | UART7 | Debug |
