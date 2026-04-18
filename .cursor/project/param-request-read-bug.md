# PARAM_REQUEST_READ Bug 分析

## 现象
- `PARAM_REQUEST_LIST` 正常工作（返回 177 个参数）
- `PARAM_REQUEST_READ`（按名字或按索引）永远无响应
- GDB 确认 `handle_param_request_read` handler 被调用
- `mavlink_msg_param_value_send` 未被调用（`check_payload_size` 返回 false）

## 根因链
1. `handle_param_request_read` → `HAVE_PAYLOAD_SPACE(chan, PARAM_VALUE)` → `txspace()` → `_writebuf.space()`
2. `txspace()` 返回 0（writebuf 满）
3. writebuf 的 drain 在 `_timer_tick` 中进行，但 `update_receive` 阶段 drain 还没发生
4. 即使在 `txspace()` 中手动调用 `_drain_writebuf_to_dev()`，`rt_device_write` 对 USB CDC 可能返回 0（USB endpoint buffer 也满了）

## 深层问题
RT-Thread USB CDC driver 的 `rt_device_write` 返回 0 表示 device buffer full。与 ChibiOS 不同，ChibiOS 的 serial driver 直接操作 DMA，不需要中间 buffer。

## 待修复
- 需要分析 RT-Thread USB CDC driver 的 buffer 机制
- 可能需要在 UARTDriver 层实现 "blocking write until space available" 选项
- 或者增大 USB CDC 的 TX buffer size
- 或者在 deferred message 发送阶段（而非接收阶段）处理 PARAM_REQUEST_READ

## 临时方案
PARAM_REQUEST_LIST 可以替代 PARAM_REQUEST_READ 获取所有参数。MAVROS 使用 PARAM_REQUEST_LIST 获取参数列表，然后 PARAM_REQUEST_READ 只用于个别参数查询。
