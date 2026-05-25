# Phase 1: 时钟树 CMSIS 寄存器化

## 目标

将 `stm32f7_clock_ll.c` 从 HAL RCC 调用改为纯 CMSIS 寄存器直写。
参考 ChibiOS `stm32_clock_ll.c` 风格，参考 CubeMX 生成的配置值。

## CUAV V5 时钟配置（STM32F767）

| 时钟 | 频率 | 来源 |
|------|------|------|
| HSE | 8 MHz | 外部晶振 |
| PLL | 216 MHz | HSE → /M × N / P |
| HCLK (AHB) | 216 MHz | PLLP / 1 |
| APB1 | 54 MHz | HCLK / 4 |
| APB2 | 108 MHz | HCLK / 2 |
| Flash wait states | 5 WS (for 216MHz @ 3.3V) | |

### PLL 参数
```
PLLM = 8   (HSE/8 = 1MHz VCO input)
PLLN = 432 (1MHz × 432 = 432MHz VCO)
PLLP = 2   (432/2 = 216MHz PLLP)
PLLQ = 9   (432/9 = 48MHz for USB OTG)
```

## 涉及文件

### 目标文件（CMSIS 重写）
- `modules/rt-thread/bsp/stm32/stm32f765-cuav-v5/board/drivers_ll/stm32f7_clock_ll.c`

### 被影响文件（调用 clock init 的）
- `board/board.c` — 调用 HAL_Init()、HAL_RCC_* → 改为调用我们的 clock_init()
- `board/CubeMX_Config/Src/stm32f7xx_hal_msp.c` — 后续可删除

## 实现方案

### 核心函数

```c
// 新函数，替换 HAL_RCC_OscConfig + HAL_RCC_ClockConfig
void rtt_clock_init(void)
{
    // 1. 等待 HSE 就绪
    RCC->CR |= RCC_CR_HSEON;
    while (!(RCC->CR & RCC_CR_HSERDY));

    // 2. 配置 PLL (HSE, M=8, N=432, P=2, Q=9)
    RCC->PLLCFGR = (8 << 0)       // PLLM
                 | (432 << 6)     // PLLN
                 | (0 << 16)      // PLLP = 2 (00)
                 | (9 << 24)      // PLLQ
                 | RCC_PLLCFGR_PLLSRC_HSE;  // HSE source
    
    // 3. 使能 PLL 并等待
    RCC->CR |= RCC_CR_PLLON;
    while (!(RCC->CR & RCC_CR_PLLRDY));

    // 4. 配置 AHB/APB 分频 + 时钟源切换
    RCC->CFGR = RCC_CFGR_HPRE_DIV1   // AHB = SYSCLK/1
              | RCC_CFGR_PPRE1_DIV4  // APB1 = HCLK/4
              | RCC_CFGR_PPRE2_DIV2  // APB2 = HCLK/2
              | RCC_CFGR_SW_PLL;     // SYSCLK = PLLP
    
    while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_PLL);

    // 5. Flash 等待周期
    FLASH->ACR = FLASH_ACR_LATENCY_5WS
               | FLASH_ACR_PRFTEN     // 预取使能
               | FLASH_ACR_ARTEN;     // ART 加速器
}
```

### 外围时钟使能（AHB1/APB1/APB2 总线）

```c
void rtt_enable_peripheral_clocks(void)
{
    // AHB1: GPIOA-F, CRC
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN | RCC_AHB1ENR_GPIOBEN
                  | RCC_AHB1ENR_GPIOCEN | RCC_AHB1ENR_GPIODEN
                  | RCC_AHB1ENR_GPIOEEN | RCC_AHB1ENR_GPIOFEN
                  | RCC_AHB1ENR_GPIOGEN;
    (void)RCC->AHB1ENR;  // 保证写入完成
    
    // APB1: I2C1/2/3, SPI2, USART2/3/4/5
    RCC->APB1ENR |= RCC_APB1ENR_I2C1EN | RCC_APB1ENR_I2C2EN | RCC_APB1ENR_I2C3EN;
    (void)RCC->APB1ENR;
    
    // APB2: SPI1/4, USART1/6, ADC1
    RCC->APB2ENR |= RCC_APB2ENR_SPI1EN | RCC_APB2ENR_SPI4EN
                  | RCC_APB2ENR_USART1EN | RCC_APB2ENR_USART6EN
                  | RCC_APB2ENR_ADC1EN;
    (void)RCC->APB2ENR;
}
```

### board.c 的修改

替换：
```c
HAL_Init();                              // → 删掉
SystemClock_Config();                    // → rtt_clock_init()
MX_GPIO_Init();                          // → 删掉（之后在 drv_gpio_ll 实现）
HAL_SYSTICK_Config(SystemCoreClock/1000); // → 保留（SysTick 配置）
HAL_SYSTICK_AutoReloadSet();            // → SysTick->LOAD 直写
```

## 步骤

### Step 1: 读取 ChibiOS 参考
- 读取 `libraries/AP_HAL_ChibiOS/hwdef/CUAVv5/hwdef.dat` → 获取时钟参数
- 读取 ChibiOS `stm32_clock_ll.c` → 参考寄存器写法

### Step 2: 实现 `rtt_clock_init()`
- 写 `stm32f7_clock_ll.c` 替换内容
- 纯 CMSIS 寄存器操作，不用任何 `HAL_RCC_*`

### Step 3: 修改 `board.c`
- 删掉 `HAL_Init()` 调用
- 删掉 `HAL_RCC_*` 调用
- 调用 `rtt_clock_init()`

### Step 4: 编译验证
```bash
cd /data/firmare/pogo-apm && scons --v=ArduCopter --target=cuav_v5 -j$(nproc)
```

### Step 5: OpenOCD 探针验证
- 读取 RCC->CR → 确认 PLL=ON, HSERDY
- 读取 RCC->CFGR → 确认 SWS=PLL
- 读取 RCC->PLLCFGR → 确认 M=8, N=432, P=0
- 读取 FLASH->ACR → 确认 LATENCY=5WS
- 读取 SysTick->LOAD → 确认 216000

## 风险与注意

1. **D-Cache 风险**：STM32F76xxx errata 2.2.1 — D-Cache 在某些条件下会导致数据异常。建议先仅开 I-Cache，D-Cache 后续验证。
2. **SysTick 中断优先级**：必须设为 RT-Thread 要求的 `0x0F`（最低抢占优先级，最高子优先级）
3. **IWDG**：CUAV V5 的 IWDG 硬件不可关闭（FLASH_OPTCR_IWDG_SW=0），需在时钟切换后立即喂狗
4. **HAL 残留**：确保 board.c 不调用任何 HAL_* 函数，否则链接器会拉入 stm32f7xx_hal_*.o

## 文件清单

### 修改
- `modules/rt-thread/bsp/stm32/stm32f765-cuav-v5/board/drivers_ll/stm32f7_clock_ll.c` — CMSIS 重写
- `modules/rt-thread/bsp/stm32/stm32f765-cuav-v5/board/board.c` — 去 HAL

### 后续阶段删除
- `board/CubeMX_Config/Src/stm32f7xx_hal_msp.c` — Phase 2 删除
