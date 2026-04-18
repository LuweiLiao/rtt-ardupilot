# SDIO SD Card Feasibility Report — RTT CUAV V5

**Date:** 2026-04-19  
**Board:** CUAV V5 (STM32F767)

## 结论：完全可行，几乎所有基础设施已就绪

## 1. RT-Thread STM32F7 SDIO 驱动

✅ **已有完整驱动：**
- HAL 驱动：`modules/rt-thread/bsp/stm32/libraries/HAL_Drivers/drivers/drv_sdio.c` + `drv_sdmmc.c`
- F7 专用配置：`drivers/config/f7/sdio_config.h` — 已配置 SDMMC1 + DMA2_Stream3/6 Channel4
- 板级移植代码：`bsp/stm32/stm32f765-cuav-v5/board/ports/sdcard_port.c` — 完整的 SD 卡挂载逻辑，包括：
  - PG7 SD 卡电源使能
  - `rt_hw_sdio_init()` 初始化
  - `dfs_mount("sd0", "/sdcard", "elm", 0, 0)` 挂载 elmfat
  - `/sdcard/APM` 目录创建（ArduPilot 文件系统）

## 2. .config 状态

DFS（文件系统框架）已启用：
- `CONFIG_RT_USING_DFS=y`
- `CONFIG_RT_USING_DFS_ELMFAT=y`
- `CONFIG_DFS_FD_MAX=16`

**需要启用的选项（当前禁用）：**
```
CONFIG_RT_USING_SDIO=y          # line 291
CONFIG_BSP_USING_SDIO=y         # line 1476
CONFIG_PKG_USING_STM32_SDIO=y   # line 825 (可能不需要，看HAL驱动是否直接编译)
```

## 3. hwdef.py SDMMC 支持

✅ **已完整支持：**
- `rtt_hwdef.py` line 101: `SDMMC1/SDMMC2` 在 `DMA_IRQ_PERIPH_MAP` 中定义
- Line 191: 信号解析识别 `SDMMC\d+` 前缀
- Line 250: SDMMC pin 收集到 `self.sd_pins`
- Line 745: `generate_sdmmc_msp_init()` 生成 `HAL_SD_MspInit()` 代码

## 4. hwdef.dat Pin 定义

✅ **已正确定义，无冲突：**
```
PC8  SDMMC_D0   SDMMC1 AF12
PC9  SDMMC_D1   SDMMC1 AF12
PC10 SDMMC_D2   SDMMC1 AF12
PC11 SDMMC_D3   SDMMC1 AF12
PC12 SDMMC_CLK  SDMMC1 AF12
PD2  SDMMC_CMD  SDMMC1
```

**Pin 冲突检查：** PD0/PD1 用于 UART4 (GPS2)，PD2 用于 SDMMC1_CMD — 无冲突。

## 5. 启用步骤

1. **修改 .config** — 启用 `CONFIG_RT_USING_SDIO=y` 和 `CONFIG_BSP_USING_SDIO=y`
2. **确认 `sdcard_port.c` 被编译** — 检查 SConstruct 或 board/SConscript 是否包含此文件
3. **DMA 冲突检查** — sdio_config.h 使用 DMA2_Stream3/6 Channel4，需确认与 SPI4/SPI1 不冲突（config 注释已提及）
4. **重新编译** — `scons` 即可

## 6. 风险点

| 项目 | 风险 | 说明 |
|------|------|------|
| DMA 冲突 | 低 | 需验证 DMA2_Stream3/6 未被其他外设占用 |
| SDIO 中断优先级 | 低 | NVIC 配置需正确，避免与实时中断冲突 |
| 电源时序 | 低 | sdcard_port.c 已处理 PG7 电源 + 500ms 等待 |
| `--gc-sections` | 低 | sdcard_port.c 通过显式引用确保 drv_sdio.o 不被裁剪 |

## 7. 总结

这是一个**几乎零额外工作量**的功能 — 驱动、配置、板级代码、hwdef 解析全部就绪。只需在 .config 中启用 SDIO，确认编译链路，即可使用 `/sdcard` 挂载点。
