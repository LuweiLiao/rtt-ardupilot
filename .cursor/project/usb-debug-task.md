# USB CDC 热复位自主调试任务 — 2026-04-17 夜间

## 目标
解决 CUAV v5 (STM32F7) USB CDC 热复位后无法枚举的问题。

## 当前状态
- 冷启动 ✅ (237 msgs, 16 types)
- 热复位 ❌ (ttyACM1 消失, AHB2ENR=0)

## 根因分析
热复位后 DWC2 进入 CSRST=1+AHBIDL=0 死锁状态 (GRSTCTL=0x40001847)。
通过 monitor mww 验证：写 GRSTCTL=0 可以清除死锁。
但当前代码实现（dwc2_reset 中的 GRSTCTL=0 写入）效果不足。

## 已验证的事实
1. RCC AHB2 寄存器地址正确（AHB2ENR=0x40023820 bit7, AHB2RSTR=0x40023830 bit7）
2. monitor mww 写 GRSTCTL=0 → GRSTCTL 变为 0x00000040（死锁清除）
3. RCC AHB2 reset 无法清除 CSRST 死锁
4. 热复位后进入 cherryusb_cdc_init 时：GRSTCTL=0x40001447, GSNPSID=0x0200d1e8, AHB2ENR=0
5. 冷启动正常说明 init 序列本身没问题

## 已修改的文件
- cherryusb.c: NVIC disable + cdc_acm_rcc_reset() + 诊断 rt_kprintf
- usb_dc_dwc2.c: dwc2_reset GRSTCTL=0 清除 + SDIS 50ms delay + 超时保护

## 关键文件路径
- cherryusb.c: /home/llw/firmare/pogo-apm/modules/rt-thread/bsp/stm32/stm32f765-cuav-v5/board/ports/cherryusb/cherryusb.c
- usb_dc_dwc2.c: /home/llw/firmare/pogo-apm/modules/rt-thread/components/drivers/usb/cherryusb/port/dwc2/usb_dc_dwc2.c
- usb_irq.c: /home/llw/firmare/pogo-apm/modules/rt-thread/bsp/stm32/stm32f765-cuav-v5/board/ports/cherryusb/usb_irq.c
- HAL MSP: /home/llw/firmare/pogo-apm/modules/rt-thread/bsp/stm32/stm32f765-cuav-v5/board/CubeMX_Config/Src/stm32f7xx_hal_msp.c

## 构建和烧录命令
- 构建: cd /home/llw/firmare/pogo-apm && python3 -m SCons --target=cuav-v5 -j16
- 烧录: arm-none-eabi-gdb -batch -ex "target remote :3333" -ex "mon reset halt" -ex "monitor flash write_image erase build/rtt_deploy/cuav_v5/rtthread.bin 0x08008000 bin" -ex "mon reset run"
- OpenOCD 已运行 (pid 通过 pgrep openocd 检查)
- GDB 非侵入检查: arm-none-eabi-gdb -batch -ex "file build/rtt_deploy/cuav_v5/rt-thread.elf" -ex "target remote :3333" -ex "mon halt" -ex "set \$b=0x50000000" -ex "x/1xw \$b+0x0C" -ex "x/1xw 0x40023820" -ex "mon resume"
- 热复位: arm-none-eabi-gdb -batch -ex "target remote :3333" -ex "mon reset init"

## 测试流程
- 每次修改后：构建 → 烧录 → 等 30s → 冷启动测试(heartbeat+msgs) → 热复位 → 等 30s → 热复位测试
- pymavlink 测试脚本见下方
- 冷启动 OK 后至少做 3 次热复位，全部通过才算成功

## MAVLink 测试 (python3)
```python
import pymavlink.mavutil as mu, time
for dev in ['/dev/ttyACM1','/dev/ttyACM0']:
    try:
        m = mu.mavlink_connection(dev, baud=115200)
        hb = m.wait_heartbeat(timeout=8)
        if hb:
            cnt=0; types=set(); t0=time.time()
            while time.time()-t0<5:
                msg=m.recv_match(timeout=0.5)
                if msg and msg.get_type()!='BAD_DATA': cnt+=1; types.add(msg.get_type())
            print(f'OK:{dev}:{cnt}msgs:{len(types)}types')
            break
    except Exception as e: pass
```

## DWC2 寄存器地址
- USB_OTG_FS base: 0x50000000
- GRSTCTL: +0x0C, GSNPSID: +0x4C, GINTSTS: +0x10, GINTMSK: +0x14
- GAHBCFG: +0x04, GUSBCFG: +0x08, DCTL: +0x800, PCGCCTL: +0xE00
- RCC_AHB2ENR: 0x40023820 (bit7=OTGFSEN)
- RCC_AHB2RSTR: 0x40023830 (bit7=OTGFSRST)

## 逐步尝试方案（按优先级排序）

### 方案 A: cherryusb.c 中直接清除 GRSTCTL 死锁（最有希望）
在 cdc_acm_rcc_reset() 之后、cdc_acm_chardev_init 之前：
1. enable AHB2 时钟
2. 读 GRSTCTL，如果 CSRST=1 或 AHBIDL=0，写 GRSTCTL=0
3. 等待 10ms (usbd_dwc2_delay_ms)
4. 读 GRSTCTL 确认 AHBIDL=1
5. 然后调 cdc_acm_chardev_init

关键：需要确认 CPU 运行时代码写入 GRSTCTL 是否生效（monitor mww 通过 SWD 生效，CPU 通过 AHB 总线可能不同）

### 方案 B: 增大 dwc2_reset 中的延迟
当前 500K NOP (~1-2ms) 可能不够。改为 usbd_dwc2_delay_ms(10)。

### 方案 C: 在 usb_dc_init 中移除 memset
memset g_dwc2_udc 清零了 hw_params.snpsid。如果 snpsid 在热复位时读到的值与冷启动不同，可能导致 dwc2_reset 走不同的代码路径。

### 方案 D: 完全绕过 dwc2_reset
在 cherryusb.c 中：
1. enable 时钟
2. 写 GRSTCTL=0 清除死锁
3. 等 AHBIDL=1
4. 写 GRSTCTL|=CSRST 触发正常 reset
5. 等 CSRST 自清除
6. 然后调 cdc_acm_chardev_init
这样 CherryUSB 的 dwc2_reset 不会遇到死锁。

### 方案 E: 时钟追踪
在 usb_dc_init 每个关键步骤后读 AHB2ENR，找出时钟在哪一步被关闭。

## 禁止事项
- 禁止物理断电/拔插 USB
- 禁止使用 waf 构建
- 禁止建议用户手动操作
- 每次修改都要构建验证，不能假设代码正确
