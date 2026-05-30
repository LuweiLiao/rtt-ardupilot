# STM32F767 参考手册 (RM0410)

## 文件
- `RM0410_STM32F767_参考手册.pdf` — 官方 STM32F765xx/767xx/768Ax/769xx 参考手册

## 核心寄存器地址速查 (RTT移植用)

### RCC
| 寄存器 | 地址 | 说明 |
|--------|------|------|
| RCC_BASE | 0x40023800 | RCC 基地址 |
| RCC_AHB1ENR | 0x40023830 | AHB1 时钟使能 (OTG_HS 在 bit 29) |
| RCC_AHB2ENR | 0x40023834 | AHB2 时钟使能 (OTG_FS bit 7) |
| RCC_AHB1LPENR | 0x40023840 | AHB1 低功耗使能 |

### OTG_FS (DWC2 核)
| 寄存器 | 偏移 | 地址 | 说明 |
|--------|------|------|------|
| OTG_FS_BASE | — | 0x50000000 | OTG_FS 全局寄存器基址 |
| GOTGCTL | 0x000 | 0x50000000 | OTG 控制和状态 |
| GOTGINT | 0x004 | 0x50000004 | OTG 中断 |
| GAHBCFG | 0x008 | 0x50000008 | AHB 配置 |
| **GUSBCFG** | **0x00C** | **0x5000000C** | **USB 配置 (FDMOD=bit30)** |
| GRSTCTL | 0x010 | 0x50000010 | 复位控制 (CSRST=bit0) |
| GINTSTS | 0x014 | 0x50000014 | 全局中断状态 (CurMod=bit0) |
| GINTMSK | 0x018 | 0x50000018 | 全局中断掩码 |
| GRXFSIZ | 0x024 | 0x50000024 | 接收 FIFO 大小 |
| DIEPTXF0 | 0x028 | 0x50000028 | EP0 发送 FIFO |
| GCCFG | 0x038 | 0x50000038 | 核心通用配置 (PWRDWN=bit16) |
| GHWCFG2 | 0x048 | 0x50000048 | 硬件配置2 (架构类型 bits 4-5) |

### OTG_FS Device 寄存器
| 寄存器 | 偏移 | 地址 | 说明 |
|--------|------|------|------|
| DCFG | 0x800 | 0x50000800 | 设备配置 |
| DCTL | 0x804 | 0x50000804 | 设备控制 (SDIS=bit1) |
| DSTS | 0x808 | 0x50000808 | 设备状态 (ENUMSPD=bits 0-1) |
| DIEPMSK | 0x810 | 0x50000810 | IN EP 中断掩码 |
| DOEPMSK | 0x814 | 0x50000814 | OUT EP 中断掩码 |
| DAINT | 0x818 | 0x50000818 | 所有 EP 中断 |
| DAINTMSK | 0x81C | 0x5000081C | 所有 EP 中断掩码 |
| DIEPCTL0 | 0x900 | 0x50000900 | IN EP0 控制 |
| DOEPCTL0 | 0xB00 | 0x50000B00 | OUT EP0 控制 |

### GUSBCFG 关键位
- **bit 30 (FDMOD)** — Force Device Mode (写1进设备模式)
- bit 29 (FHMOD) — Force Host Mode (写1进主机模式)
- bits 10-15 (TRDT) —  turnaround time (FS: 5, HS: 9)
- bit 6 (PHYSEL) — 内部 FS PHY 选择

### 关键知识点（调试教训）
1. GUSBCFG 在 CSRST 后保持原值 — 不会复位
2. FDMOD 在 bit 30（不是 bit 29！ChibiOS 定义正确）
3. RCC_AHB2ENR.OTGFSEN=bit7 — 必须使能才能写 OTG_FS 寄存器
4. ENUMDONE=GINTSTS.bit5 — 主机枚举完成后置位
5. DSTS.ENUMSPD=2 表示 FS(48MHz) 模式
6. 核心复位流程: RCC AHB2RST → RCC时钟→ GUSBCFG → CSRST → FIFO/EP0 → SDIS=0
