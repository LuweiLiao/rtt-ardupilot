### 2026-04-11 18:50（Bootloader jump_to_app 分析 + 自由运行验证）

**已完成：**
- 单步验证 jump_to_app() 地址范围检查 PASS：
  - r3 = board_info.flash_size(0x1f8000) + 0x8000000 + 0x8000 = 0x08200000
  - Reset_Handler(0x0810cfb1) < 0x08200000 → 通过，不返回
- 单步确认 jump_to_app 尾部：设 VTOR=0x08008000 → 读 SP → 读 Reset_Handler → dsb/isb → 关中断/清理 → mov sp,r5 → msr MSP,r5 → bx r4
- 断点模式：Reset_Handler 命中，VTOR=0x08008000，SP=0x2000d498 全部正确
- 自由运行3秒后 halt：有时 PC=0x0810b2e2(app, rt_hw_atomic_load) 有时 PC=0x08003628(bootloader idle)
- CFSR=0, HFSR=0，无 HardFault 记录

**结论：**
- Bootloader jump_to_app 逻辑完全正确
- App 确实被跳转到并开始运行（调度器启动）
- 但自由运行后最终回到 bootloader，VTOR=0x08000000
- 推测根因：看门狗复位（bootloader 开启了 IWDG，app 未及时喂狗或关闭）

**下一步：**
- 检查 bootloader 是否开启 IWDG
- 在 app startup 最早期添加 IWDG 关闭代码
- 验证 app 主循环能持续运行不复位
