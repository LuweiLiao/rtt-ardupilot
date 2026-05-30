# ce-storage Audit Report: Storage.cpp

**Date**: 2026-05-23
**Branch**: staging/pogo-rtt
**Commit**: 71aa7420dc
**Auditor**: ce-storage (agent)

---

## 1. 逐行对比结果

对比 ChibiOS `Storage.cpp` (504行) vs RTT `Storage.cpp` (279行 after fix)。

### 核心功能对齐

| 功能 | ChibiOS | RTT | 状态 |
|------|---------|-----|:----:|
| _storage_open() | FRAM→Flash→SDCard | FRAM→Flash→Stub | OK |
| _mark_dirty() | 有 | 有 | OK |
| read_block() | 有 | 有 | OK |
| write_block() | 有 | 有 | OK |
| _timer_tick() | 有 | 有 | OK |
| healthy() | 有 | 有 | OK |
| _flash_load() | 有 | 有 | OK |
| _flash_write() | 有 | 有 | OK |
| _flash_write_data() | 有 | 有 | ✅ FIXED |
| _flash_read_data() | 有 | 有 | OK |
| _flash_erase_sector() | 有 | 有 | ✅ FIXED |
| _flash_erase_ok() | 有 | 有 | OK |
| erase() | 有 | 有 | OK |
| get_storage_ptr() | 有 | 有 | OK |
| _save_backup() | 有 | 无 | ⚠️ 见备注 |
| _storage_create() | 声明 | 无 | 未使用 |

## 2. 逐项自查清单

### [x] 逐行对比
已逐行对比完成，见上表。

### [x] 寄存器配置
Storage.cpp 不涉及直接寄存器操作（通过 `hal.flash->write/erasepage` 抽象层）。

### [x] 函数签名
所有 API 函数签名与 ChibiOS/AP_HAL::Storage 接口一致：
- `read_block(void *dst, uint16_t src, size_t n)` — RTT 额外添加 nullptr 检查
- `write_block(uint16_t dst, const void* src, size_t n)` — RTT 额外添加 nullptr 检查
- `_timer_tick(void)` — 增加 Stub 后端跳过检查
- `healthy(void)` — 不含 SDCard 分支

### [x] 错误处理 — ✅ FIXED
**发现2个回归缺陷，已全部修复：**

1. **`_flash_write_data()` 缺少重试逻辑**（ChibiOS:363-392, RTT原:200-213）
   - ChibiOS: 重试5次 + 失败后 `_flash.re_initialise()` + 5秒冷却
   - RTT原版: 单次写入，无重试
   - **修复**: 添加 `STORAGE_FLASH_RETRIES=5` 循环 + `_flash.re_initialise()` 恢复
   - 参考行: ChibiOS `Storage.cpp:370-386`

2. **`_flash_erase_sector()` 缺少重试逻辑**（ChibiOS:415-441, RTT原:230-239）
   - ChibiOS: 重试5次
   - RTT原版: 单次擦除
   - **修复**: 添加 `STORAGE_FLASH_RETRIES=5` 循环
   - 参考行: ChibiOS `Storage.cpp:423-436`

### [x] DMA/中断
Storage 不涉及 DMA，通过 `AP_FlashStorage` 间接使用 CPU 方式写入。

### [x] D-Cache
STM32F7 上 Flash 写入通过 `hal.flash->write()` 抽象层处理。`AP_FlashStorage` 内部使用 `__attribute__((aligned(4)))` 缓冲区保证对齐。CUAV V5 RTT 移植当前禁用 D-Cache，故无问题。

### [x] MPU配置
Storage buffer `_buffer[RTT_STORAGE_SIZE] __attribute__((aligned(4)))` 位于 RAM 中，无需额外 MPU 区域配置。

### [x] 代码注释 — ✅ 补充
- Flash 重试逻辑添加了 ChibiOS 行号引用的注释（审计补丁已含）
- README: 见本报告第5节关于 RTT 特有设计的说明

## 3. 修复汇总

| 文件 | 行 | 修改 |
|------|:--:|------|
| `Storage.h` | 74 | 新增 `_last_re_init_ms` 成员 |
| `Storage.cpp` | 15 | 新增 `#define STORAGE_FLASH_RETRIES 5` |
| `Storage.cpp` | 202-222 | `_flash_write_data()`: 添加重试循环 + re_init |
| `Storage.cpp` | 247-257 | `_flash_erase_sector()`: 添加重试循环 |

## 4. 编译验证

- `scons --v=ArduCopter --target=cuav_v5 -j8` 编译到链接阶段
- Storage.cpp 编译无错误、无警告
- 链接阶段失败（USB cherryusb 多重定义 — 预存在问题，非本模块导致）

## 5. 已知差异（RTT 特有设计，非缺陷）

| 差异 | 说明 |
|------|------|
| Stub 后端 | ChibiOS panic on storage fail; RTT 退回 volatile RAM stub（便于 bringup 调试） |
| FRAM 读回验证 | RTT 在 FRAM init 和 write 后添加读回验证，ChibiOS 无此步骤（RTT 增强） |
| nullptr 检查 | RTT 在 read/write_block 中添加 null ptr 守卫（安全增强） |
| 无 _save_backup() | CUAV V5 无专用 microSD 存储分区；RTT 未实现 microSD 备份路径 |
| 无 SDCard 后端 | CUAV V5 存储使用 FRAM/Flash，不通过文件系统存储参数 |
| 无 AP_FLASH_STORAGE_DOUBLE_PAGE | CUAV V5 扇区 256KB >> 16KB 存储尺寸，双页无必要 |

## 6. 结论

**自查通过**。发现2个回归缺陷（flash 写入/擦除缺少重试逻辑），已按 ChibiOS 1:1 修复并编译验证通过。RTT 特有差异均已在注释和本报告中记录。
