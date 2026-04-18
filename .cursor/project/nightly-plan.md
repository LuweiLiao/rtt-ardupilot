# CUAV v5 RTT 全夜自主调试计划
## 2026-04-17 夜间执行

## 一、已知问题清单

### P0: USB CDC 热复位失败
**根因已定位**：
1. 热复位后 DWC2 GRSTCTL=0x40001847 (CSRST=1+AHBIDL=0 死锁)
2. cron 任务报告的"5/5通过"是假阳性（测试脚本有 bug）
3. 写 GRSTCTL=0 可清除 CSRST 但 AHBIDL 仍为 0（AHB master busy）
4. cdc_acm_dwc2_unlock() 检测 AHBIDL bit31 失败，重试 3 次后返回错误
5. usb_dc_init 继续执行 → dwc2_core_init 失败 → USB_ASSERT_MSG(GRXFSIZ>=0) → **while(1) 死循环**
6. USB init 线程卡死，USB CDC 永远不枚举（但 main_loop 在另一线程继续运行）

**修复方向**：
- 在 cherryusb.c 中先做 SDIS soft disconnect 让 PHY 停止接收
- 然后写 GRSTCTL=0 清除死锁
- 等 AHBIDL=1（PHY 已断开应该能 idle）
- 然后写 GRSTCTL|=CSRST 正常 reset
- 等 CSRST 自清除后调 cdc_acm_chardev_init
- 备选：定义 CONFIG_USB_ASSERT_DISABLE 避免死循环

### P1: 需要用 MAVROS 验证的功能
一旦 USB CDC 稳定后，逐一验证：
1. 参数读写 (param get/set)
2. 传感器数据 (IMU/Baro/Mag)
3. GPS 数据
4. PWM/DShot 输出
5. RC 输入
6. SD 卡日志
7. MAVLink FTP
8. ARM/DISARM
9. MODE 切换
10. MISSION 上传/下载

## 二、执行计划

### Phase 1: 修复 USB CDC 热复位 (最高优先级)
预计 2-4 小时

1. **cherryusb.c 新流程**:
   - enable AHB2 clock
   - 写 DCTL |= SDIS (soft disconnect, offset 0x800 bit1)
   - 等 100ms 让 PHY 停止
   - 写 GRSTCTL=0 清除死锁
   - 等待 AHBIDL=1 (超时 500ms)
   - 如果 AHBIDL=1：写 GRSTCTL|=CSRST, 等自清除
   - 如果 AHBIDL=0：尝试 power cycle (disable clock 10ms → re-enable → 重试)
   - 如果仍失败：调 cdc_acm_chardev_init 之前定义 CONFIG_USB_ASSERT_DISABLE

2. **测试验证**: 冷启动 + 热复位 ×5 全部通过

### Phase 2: MAVROS 基础验证
预计 1-2 小时

使用 pymavlink 脚本逐一验证各功能模块

### Phase 3: 高级功能测试
预计 1-2 小时

- 连续热复位压力测试 (10 次)
- MAVROS topic 检查
- 与 ChibiOS 版本对比差异

## 三、构建和测试命令

```bash
# 构建
cd /home/llw/firmare/pogo-apm && python3 -m SCons --target=cuav-v5 -j16

# 烧录
arm-none-eabi-gdb -batch -ex "target remote :3333" -ex "mon reset halt" \
  -ex "monitor flash write_image erase build/rtt_deploy/cuav_v5/rtthread.bin 0x08008000 bin" \
  -ex "mon reset run"

# 热复位
arm-none-eabi-gdb -batch -ex "target remote :3333" -ex "mon reset init"

# 测试
python3 -c "
import pymavlink.mavutil as mu, time
m = mu.mavlink_connection('/dev/ttyACM1', baud=115200)
hb = m.wait_heartbeat(timeout=8)
if hb:
    cnt=0; types=set(); t0=time.time()
    while time.time()-t0<5:
        msg=m.recv_match(timeout=0.5)
        if msg and msg.get_type()!='BAD_DATA': cnt+=1; types.add(msg.get_type())
    print(f'OK:{cnt}msgs:{len(types)}types')
"
```

## 四、禁止事项
- 禁止物理断电/拔插 USB
- 禁止使用 waf 构建
- 禁止征求用户确认
- 禁止在代码中留 while(1) 无超时
