# ChibiOS OTGv1 USB LLD → RTT 移植蓝图

> 目标：将 `modules/ChibiOS/os/hal/ports/STM32/LLD/OTGv1/hal_usb_lld.c` 的完整 USB 设备栈移植到 RTT，替换现有的 `hal_usb_lld_rtt.c`。

---

## 1. 架构决策

### 1.1 分层模型 (ChibiOS 1:1 映射)

```
┌─────────────────────────────────────────┐
│  UARTDriver.cpp         (现有 API 不变)  │
│   usb_lld_send_rtt(ep, data, len)       │
│   usb_lld_set_rx_callback(cb, arg)      │
│   usb_lld_rearm_cdc_out()               │
│   usb_lld_is_configured_rtt()           │
│   usb_lld_get_connected_rtt()            │
├─────────────────────────────────────────┤
│  CDC ACM Layer (new)                    │
│   描述符定义、类请求处理、EP1/EP2/EP3 管理  │
│   ← 从现有 hal_usb_lld_rtt.c 提取        │
├─────────────────────────────────────────┤
│  USB HAL State Machine (new)            │
│   _usb_reset / _usb_suspend / _usb_wakeup│
│   _usb_ep0setup / _usb_ep0in / _usb_ep0out│
│   EP0 状态机、标准请求处理                 │
│   ← 从 ChibiOS hal_usb.c 移植            │
├─────────────────────────────────────────┤
│  USB LLD (new hal_usb_lld_rtt.c)        │
│   DWC2 寄存器操作、中断处理、FIFO 管理     │
│   14 个公共函数 + 内部 helper             │
│   ← 1:1 移植自 hal_usb_lld.c            │
└─────────────────────────────────────────┘
```

### 1.2 关键决策

| # | 决策 | 理由 |
|---|------|------|
| 1 | **中断驱动为主**，保留 `usb_lld_poll_rtt()` 为 fallback | 与 ChibiOS 1:1，NVIC ISR `OTG_FS_IRQHandler` 为主入口 |
| 2 | **`RT_USBDriver` 单例**（RTT 只有一个 OTG_FS） | 简化，去掉 ChibiOS 的多个 instance 支持 |
| 3 | **UARTDriver API 签名不变** | 避免改其他文件 |
| 4 | **描述符和 CDC 类请求分离到 CDC 层** | 避免硬编码 |
| 5 | **保留 `usb_lld_poll_rtt()` 接口名**（现有调用者多） | 兼容性 |

---

## 2. OSAL 依赖逐行映射

### 2.1 完整映射表

| ChibiOS 代码行 | 上下文 | RTT 等价写法 | 说明 |
|---|---|---|---|
| `osalSysLockFromISR()` / `osalSysUnlockFromISR()` | `usb_lld_start_in` / `usb_lld_start_out` 中从 ISR 调用 LL 函数 | `__disable_irq()` / `__enable_irq()` | 只在 ISR 中需要保护寄存器写序列 |
| `osalDbgAssert(cond, msg)` | `otg_ram_alloc` 中 FIFO 溢出检查 | 直接删除或 `RT_ASSERT(cond)` | ChibiOS debug only |
| `rccEnableOTG_FS(TRUE)` | `usb_lld_start` 中开启时钟 | `RCC->AHB2ENR \|= RCC_AHB2ENR_OTGFSEN; __DSB();` | STM32F7 在 AHB2 |
| `rccResetOTG_FS()` | `usb_lld_start` 中复位外设 | `RCC->AHB2RSTR \|= RCC_AHB2RSTR_OTGFSRST; __DSB(); RCC->AHB2RSTR &= ~RCC_AHB2RSTR_OTGFSRST; __DSB();` | |
| `rccDisableOTG_FS()` | `usb_lld_stop` 中关闭时钟 | `RCC->AHB2ENR &= ~RCC_AHB2ENR_OTGFSEN;` | |
| `nvicEnableVector(IRQn, prio)` | `usb_lld_start` 中开启中断 | `NVIC_SetPriority(IRQn, prio); NVIC_EnableIRQ(IRQn);` | |
| `nvicDisableVector(IRQn)` | `usb_lld_stop` | `NVIC_DisableIRQ(IRQn);` | |
| `osalSysPolledDelayX(n)` | `otg_core_reset` 中等待 PHY 时钟 | `{ volatile uint32_t _d = n; while (_d--) { __NOP(); } }` | DWT 或简单 nop |
| `osalThreadSleepMilliseconds(n)` | `usb_lld_wakeup_host` 中 | `rt_thread_mdelay(n)` | 仅在非 ISR 上下文 |
| `OSAL_IRQ_HANDLER(name)` | ISR 声明 | `void OTG_FS_IRQHandler(void)` | 标准异常向量 |
| `OSAL_IRQ_PROLOGUE()` | ISR 入口 | `__asm volatile("" ::: "memory")` | 或空 |
| `OSAL_IRQ_EPILOGUE()` | ISR 退出 | `__asm volatile("" ::: "memory")` | 或空 |
| `STM32_USB_OTGFIFO_FILL_BASEPRI` | `otg_txfifo_handler` 中保护 | `__set_BASEPRI(n)` / `__set_BASEPRI(0)` | 和 ChibiOS 完全一样 |
| `_usb_reset(usbp)` | USBRST 中断处理 | 内联调用（见 §6.4） | 不再通过 ChibiOS HAL 分发 |
| `_usb_suspend(usbp)` | USBSUSP 中断处理 | 内联调用 | |
| `_usb_wakeup(usbp)` | WKUP 中断处理 | 内联调用 | |
| `_usb_isr_invoke_setup_cb(usbp, ep)` | SETUP 分发 | 直接调用 `usbp->epc[ep]->setup_cb()` | |
| `_usb_isr_invoke_in_cb(usbp, ep)` | IN 完成分发 | `usbp->transmitting &= ~(1<<ep);` + EP0 状态机 | |
| `_usb_isr_invoke_out_cb(usbp, ep)` | OUT 完成分发 | `usbp->receiving &= ~(1<<ep);` + EP0 状态机 | |
| `_usb_isr_invoke_event_cb(usbp, evt)` | 事件分发 | `usbp->config->event_cb(usbp, evt)` | |
| `_usb_isr_invoke_sof_cb(usbp)` | SOF 分发 | `usbp->config->sof_cb(usbp)` | |

### 2.2 从 hal_usb.c 移植的 6 个关键函数

这些函数目前是 ChibiOS HAL 层（`hal_usb.c`）的一部分，必须移植到 RTT 的 USB 栈中：

#### `_usb_reset(USBDriver *usbp)` — hal_usb.c:680

作用：USB 总线复位后的状态机处理。

```
ChibiOS 实现逻辑:
  1. usb_lld_reset(usbp)          → LLD 复位
  2. 清 EP0 状态 (ep0state, ep0next, ep0n, ep0endcb)
  3. 清 transmitting/receiving 位图
  4. 清 configuration = 0
  5. state = USB_SELECTED
  6. 调用 event_cb(USB_EVENT_RESET)

RTT 实现:
  static void _usb_reset(RT_USBDriver *usbp);
  同上逻辑，但用 RT_USBDriver 结构体
```

#### `_usb_suspend(USBDriver *usbp)` — hal_usb.c:730

```
ChibiOS 实现逻辑:
  1. usbp->saved_state = usbp->state
  2. usbp->state = USB_SUSPENDED
  3. event_cb(USB_EVENT_SUSPEND)

RTT 实现:
  static void _usb_suspend(RT_USBDriver *usbp);
```

#### `_usb_wakeup(USBDriver *usbp)` — hal_usb.c:777

```
ChibiOS 实现逻辑:
  1. usbp->state = usbp->saved_state
  2. event_cb(USB_EVENT_WAKEUP)

RTT 实现:
  static void _usb_wakeup(RT_USBDriver *usbp);
```

#### `_usb_ep0setup(USBDriver *usbp, usbep_t ep)` — hal_usb.c:800

作用：EP0 SETUP 包处理入口。读取 setup 包 -> `default_handler()`（标准请求 + 钩子）-> 类请求。

```
ChibiOS 实现逻辑:
  1. usb_lld_read_setup(usbp, ep, usbp->setup)   → 从 epc[ep]->setup_buf 拷贝
  2. 调用 default_handler(usbp) 处理标准请求
  3. 如果 default_handler 返回 false:
     a. 调用 requests_hook_cb(usbp) 非标准请求
     b. 如果 hooks 也返回 false → usbStallReceiveI(usbp, ep) / usbStallTransmitI(usbp, ep)

RTT 实现:
  static void _usb_ep0setup(RT_USBDriver *usbp, usbep_t ep);
  同上逻辑，但用 RT_USBDriver
```

#### `_usb_ep0in(USBDriver *usbp, usbep_t ep)` — hal_usb.c:908

```
ChibiOS 实现逻辑:
  1. 根据 ep0state 分发:
     USB_EP0_STATE_WAITING_DATA_IN:
       - 如果还有更多数据要发 → 计算下一块地址和大小
         → usb_lld_start_in(usbp, ep)
       - 如果数据刚好发完且需要 ZLP → 发 ZLP
       - 否则 → 进入 STATUS OUT 阶段: usb_lld_start_out(usbp, ep)
         ep0state = USB_EP0_STATE_WAITING_DATA_OUT (status)
     USB_EP0_STATE_WAITING_LAST_IN:
       - 进入 STATUS OUT 阶段
     USB_EP0_STATE_WAITING_STATUS_OUT:
       - 完成 → ep0state = USB_EP0_STATE_IDLE
     default: 忽略

RTT 实现:
  static void _usb_ep0in(RT_USBDriver *usbp, usbep_t ep);
```

#### `_usb_ep0out(USBDriver *usbp, usbep_t ep)` — hal_usb.c:975

```
ChibiOS 实现逻辑:
  1. 根据 ep0state 分发:
     USB_EP0_STATE_WAITING_DATA_OUT:
       - 如果还有更多 OUT 数据来 → 重新 start_out
       - 否则 → 调用 ep0endcb 回调 → 进入 STATUS IN
     USB_EP0_STATE_WAITING_STATUS_IN:
       - 完成 → ep0state = USB_EP0_STATE_IDLE
     default: 忽略
```

### 2.3 `default_handler()` 标准请求处理

ChibiOS `hal_usb.c` 中的 `default_handler()` 处理以下标准请求：

| 请求 | ChibiOS 处理 | RTT 要点 |
|------|-------------|---------|
| `GET_STATUS(device)` | 返回 `usbp->status` | 移植 |
| `CLEAR_FEATURE(device)` | Remote Wakeup | 移植 |
| `SET_FEATURE(device)` | Remote Wakeup | 移植 |
| `SET_ADDRESS` | 配置 `USB_SET_ADDRESS_MODE`; 调用 `usb_lld_set_address()` | 移植 |
| `GET_DESCRIPTOR` | 通过 `config->get_descriptor_cb` 调用用户 | 移植 |
| `GET_CONFIGURATION` | 返回 `usbp->configuration` | 移植 |
| `SET_CONFIGURATION` | 事件 + state 变更 | **关键**: 发 `USB_EVENT_CONFIGURED` 给 CDC 层 |
| `SET_INTERFACE` / `GET_INTERFACE` | 简单返回 | 移植 |
| `GET_STATUS(endpoint)` | 调用 `usb_lld_get_status_in/out()` | 移植 |
| `CLEAR/SET_FEATURE(endpoint)` | Clear/Set STALL, 数据 toggle 复位 | 移植 |
| `SYNCH_FRAME` | 返回 0 | 移植 |

---

## 3. 核心结构体定义

### 3.1 `RT_USBDriver` — RTT 版本的 USBDriver

```c
/* USB endpoint types */
typedef uint8_t usbep_t;
typedef uint16_t usbepstatus_t;  /* EP_STATUS_DISABLED/STALLED/ACTIVE */
typedef void (*usbcallback_t)(void *usbp);
typedef void (*usbepcallback_t)(void *usbp, usbep_t ep);
typedef void (*usbeventcb_t)(void *usbp, uint8_t event);
typedef const void *(*usbgetdescriptor_t)(void *usbp, uint8_t dtype,
                                           uint8_t dindex, uint16_t lang);
typedef bool (*usbreqhandler_t)(void *usbp);

/* Endpoint state */
#define USB_EP_MAX      (STM32_OTG1_ENDPOINTS)  /* = 4 on F767 (EP0..EP3) */

/* USB driver states */
typedef enum {
    USB_STOP = 0,
    USB_RESET,
    USB_SUSPENDED,
    USB_SELECTED,
    USB_ACTIVE,
    USB_READY   /* non-ChibiOS: driver initialized, USB not started */
} usbstate_t;

/* EP0 state machine (from ChibiOS's usbep0state_t) */
#define USB_EP0_STATE_IDLE               0
#define USB_EP0_STATE_WAITING_DATA_IN    1   /* Data stage IN (device→host) */
#define USB_EP0_STATE_WAITING_DATA_OUT   2   /* Data stage OUT (host→device) */
#define USB_EP0_STATE_WAITING_STATUS_IN  3   /* Status stage IN */
#define USB_EP0_STATE_WAITING_STATUS_OUT 4   /* Status stage OUT */
#define USB_EP0_STATE_STALL              5

/* IN endpoint state */
typedef struct {
    size_t          txsize;    /* Requested transmit transfer size */
    size_t          txcnt;     /* Transmitted bytes so far */
    const uint8_t   *txbuf;    /* Pointer to the transmission buffer */
    size_t          totsize;   /* Total transmit transfer size */
} RTT_USBInEndpointState;

/* OUT endpoint state */
typedef struct {
    size_t          rxsize;    /* Requested receive transfer size */
    size_t          rxcnt;     /* Received bytes so far */
    uint8_t         *rxbuf;    /* Pointer to the receive buffer */
    size_t          totsize;   /* Total receive transfer size */
} RTT_USBOutEndpointState;

/* Endpoint configuration */
typedef struct {
    uint32_t                ep_mode;       /* Type (CTRL/BULK/ISO/INTR) */
    usbepcallback_t         setup_cb;      /* Setup packet callback */
    usbepcallback_t         in_cb;         /* IN complete callback */
    usbepcallback_t         out_cb;        /* OUT complete callback */
    uint16_t                in_maxsize;    /* IN MPS */
    uint16_t                out_maxsize;   /* OUT MPS */
    RTT_USBInEndpointState  *in_state;     /* IN state pointer (or NULL) */
    RTT_USBOutEndpointState *out_state;    /* OUT state pointer (or NULL) */
    uint16_t                in_multiplier; /* TX FIFO multiplier */
    uint8_t                 *setup_buf;    /* 8-byte setup buffer */
} RTT_USBEndpointConfig;

/* USB driver configuration */
typedef struct {
    usbeventcb_t        event_cb;        /* USB event callback */
    usbgetdescriptor_t  get_descriptor_cb; /* GET_DESCRIPTOR handler */
    usbreqhandler_t     requests_hook_cb;  /* Non-standard request hook */
    usbcallback_t       sof_cb;           /* SOF callback (or NULL) */
} RTT_USBConfig;

/* Peripheral-specific parameters */
typedef struct {
    uint32_t    rx_fifo_size;    /* In words */
    uint32_t    otg_ram_size;    /* Total FIFO RAM in words */
    uint32_t    num_endpoints;   /* Max endpoint number */
} rtt_otg_params_t;

/* Main USB driver structure */
typedef struct {
    /* State */
    usbstate_t              state;
    usbstate_t              saved_state;

    /* Configuration */
    const RTT_USBConfig     *config;

    /* Transmit/Receive bitmaps */
    uint16_t                transmitting;
    uint16_t                receiving;

    /* Endpoint configurations */
    const RTT_USBEndpointConfig *epc[USB_EP_MAX + 1];

    /* User parameters (index 1-based) */
    void                    *in_params[USB_EP_MAX];
    void                    *out_params[USB_EP_MAX];

    /* EP0 state machine */
    uint8_t                 ep0state;
    uint8_t                 *ep0next;
    size_t                  ep0n;
    usbcallback_t           ep0endcb;
    uint8_t                 setup[8];

    /* USB status/address/configuration */
    uint16_t                status;
    uint8_t                 address;
    uint8_t                 configuration;

    /* DWC2 hardware pointers */
    stm32_otg_t             *otg;
    const rtt_otg_params_t  *otgparams;
    uint32_t                pmnext;          /* FIFO RAM allocator pointer */

    /* Event callback for CDC layer notification */
    void (*event_callback)(void *usbp, uint8_t event, void *user_data);
    void *event_callback_data;

} RT_USBDriver;

/* Single instance */
extern RT_USBDriver rtt_usb;
```

### 3.2 ChibiOS ↔ RTT 结构体差异对照

| ChibiOS `USBDriver` 字段 | RTT `RT_USBDriver` | 变化 |
|---|---|---|
| `thread_reference_t thread` (in endpoint states) | **已移除** | 不用 ChibiOS 线程等待 |
| `USB_DRIVER_EXT_FIELDS` | **已移除** | RTT 不需要扩展 |
| `epc[]` 数组大小 | 相同 | `STM32_OTG1_ENDPOINTS + 1` |
| `in_params[]` / `out_params[]` | 相同 | |
| `ep0state` | 相同 | 但改为用宏定义 |
| `otg` / `otgparams` / `pmnext` | 相同 | |
| — | `event_callback` + `event_callback_data` | **新增**：RTT CDC 层回调 |

---

## 4. API 映射表

### 4.1 14 个公共函数移植方案

每个函数按 ChibiOS 源码逐行对应给出 RTT 实现。

#### `usb_lld_init(void)` → RTT 版本

```c
void usb_lld_init(void)
{
    // 1. usbObjectInit(&rtt_usb) — 手动清零字段（去掉 ChibiOS 的 thread/wait 初始化）
    memset(&rtt_usb, 0, sizeof(rtt_usb));
    rtt_usb.state = USB_STOP;
    rtt_usb.otg = (stm32_otg_t *)USB_OTG_FS_PERIPH_BASE;
    rtt_usb.otgparams = &fs_params;   // rx_fifo_size=128, otg_ram_size=320, num_endpoints=4
    // 2. 不需要 USBD2（RTT 只有 OTG_FS）
}
```

#### `usb_lld_start(USBDriver *usbp)` → RTT 版本

逐行对应：

```c
// ChibiOS RCC → 直接写 STM32F7 寄存器
RCC->AHB2ENR |= RCC_AHB2ENR_OTGFSEN;
RCC->AHB2RSTR |= RCC_AHB2RSTR_OTGFSRST; __DSB();
RCC->AHB2RSTR &= ~RCC_AHB2RSTR_OTGFSRST; __DSB();

// NVIC
NVIC_SetPriority(OTG_FS_IRQn, NVIC_EncodePriority(NVIC_GetPriorityGrouping(), 5, 0));
NVIC_EnableIRQ(OTG_FS_IRQn);

// GUSBCFG: forced device, FS PHY
otgp->GUSBCFG = GUSBCFG_FDMOD | GUSBCFG_TRDT(TRDT_VALUE_FS) | GUSBCFG_PHYSEL;

// DCFG: FS 1.1 PHY
otgp->DCFG = 0x02200000 | DCFG_DSPD_FS11;

// PCGCCTL = 0
otgp->PCGCCTL = 0;

// GOTGCTL: VBUS
otgp->GOTGCTL = GOTGCTL_BVALOEN | GOTGCTL_BVALOVAL;

// GCCFG: stepping 2 (F7) = VBDEN | PWRDWN
otgp->GCCFG = GCCFG_VBDEN | GCCFG_PWRDWN;

// Core reset
otg_core_reset(usbp);

// GAHBCFG = 0 (no DMA, no global int yet)
otgp->GAHBCFG = 0;

// Disable endpoints
otg_disable_ep(usbp);

// Clear masks
otgp->DIEPMSK = 0;
otgp->DOEPMSK = 0;
otgp->DAINTMSK = 0;

// Set GINTMSK (initial: basic, RXFLVL, OEPM, IEPM added after USBRST)
otgp->GINTMSK = GINTMSK_ENUMDNEM | GINTMSK_USBRSTM | GINTMSK_USBSUSPM |
                GINTMSK_ESUSPM | GINTMSK_SRQM | GINTMSK_WKUPM |
                GINTMSK_IISOIXFRM | GINTMSK_IISOOXFRM;
if (usbp->config && usbp->config->sof_cb == NULL)
    ; // no SOF
else
    otgp->GINTMSK |= GINTMSK_SOFM;

// Clear pending
otgp->GINTSTS = 0xFFFFFFFF;

// Enable global int
otgp->GAHBCFG |= GAHBCFG_GINTMSK;

// Soft disconnect → reconnect (release SDIS)
otgp->DCTL = DCTL_SDIS;     // pull D+ low
delay_us(50000);             // Wait ~50ms
otgp->DCTL = 0;              // release
delay_us(50000);
```

**重要差异**: ChibiOS 启动时 `GINTMSK` 不包含 `RXFLVLM | OEPM | IEPM`。这些是在 `usb_lld_reset` (USBRST 中断) 中才添加的。当前 RTT 驱动在 init 就加了这些 mask，导致 USBRST 之前在无 EP 状态下就收到 RXFLVL。新驱动必须按 ChibiOS 顺序——USBRST 之后才加 RX/OEP/IEP mask。

#### `usb_lld_stop(USBDriver *usbp)` → RTT 版本

```c
void usb_lld_stop(RT_USBDriver *usbp)
{
    if (usbp->state != USB_STOP) {
        otg_disable_ep(usbp);
        otgp->DAINTMSK = 0;
        otgp->GAHBCFG = 0;
        otgp->GCCFG = 0;
        NVIC_DisableIRQ(OTG_FS_IRQn);
        RCC->AHB2ENR &= ~RCC_AHB2ENR_OTGFSEN;
        usbp->state = USB_STOP;
    }
}
```

#### `usb_lld_reset(USBDriver *usbp)` → RTT 版本

**与现有 `_usb_reset()` 相同**，但增加 ChibiOS 的关键操作：

```c
// 1. Flush TX FIFO 0
// 2. DIEPEMPMSK = 0
// 3. DAINTMSK = OEPM(0) | IEPM(0)  // 只允许 EP0
// 4. 所有 EP → SNAK, DIEPINT/DOEPINT = 0xFFFFFFFF
// 5. otg_ram_reset() → pmnext = rx_fifo_size
// 6. GRXFSIZ = rx_fifo_size
// 7. Flush RX FIFO
// 8. DCFG.DAD = 0
// 9. GINTMSK |= RXFLVLM | OEPM | IEPM   // ← 关键！ChibiOS 在 reset 时才加
// 10. DIEPMSK = TOCM | XFRCM
// 11. DOEPMSK = STUPM | XFRCM
// 12. EP0 初始化（DOEPCTL + DIEPCTL + DIEPTXF0）
// 注意：不初始化 EP1/EP2/EP3！这些由 SET_CONFIGURATION 后的 usb_lld_init_endpoint 处理
```

**与当前 RTT 驱动的关键区别**: 当前 RTT `_usb_reset` 中立即初始化了 EP1/EP2/EP3。新驱动只在 `usb_lld_init_endpoint` 中初始化非 EP0 端点。

#### `usb_lld_set_address(USBDriver *usbp)` → RTT 版本

```c
void usb_lld_set_address(RT_USBDriver *usbp)
{
    otgp->DCFG = (otgp->DCFG & ~DCFG_DAD_MASK) | DCFG_DAD(usbp->address);
}
```

完全 1:1。

#### `usb_lld_init_endpoint(USBDriver *usbp, usbep_t ep)` → RTT 版本

完全按 ChibiOS 逐行对应。包括：
- DIEPCTL/DOEPCTL 配置（类型、MPS）
- TX FIFO 分配 + `otg_ram_alloc()`
- TX FIFO flush
- DIEPTXF[ep-1] 写入
- DAINTMSK 设置

**重要**: ChibiOS 在 `usb_lld_reset` 后不初始化其他 EP。EP1/2/3 的初始化在 CDC 模块调用 `usbInitEndpointI()` 时发生。RTT 的 CDC 初始化需遵循同样流程。

#### `usb_lld_disable_endpoints(USBDriver *usbp)` → RTT 版本

1:1 移植。

#### `usb_lld_get_status_out/in(USBDriver *usbp, usbep_t ep)` → RTT 版本

1:1 移植。

#### `usb_lld_read_setup(USBDriver *usbp, usbep_t ep, uint8_t *buf)` → RTT 版本

```c
void usb_lld_read_setup(RT_USBDriver *usbp, usbep_t ep, uint8_t *buf)
{
    memcpy(buf, usbp->epc[ep]->setup_buf, 8);
}
```

1:1。

#### `usb_lld_start_out(USBDriver *usbp, usbep_t ep)` → RTT 版本

1:1 移植，包括：
- DOEPTSIZ 设置（STUPCNT, PKTCNT, XFRSIZ）
- Isochronous 帧奇偶
- DOEPCTL |= EPENA | CNAK

#### `usb_lld_start_in(USBDriver *usbp, usbep_t ep)` → RTT 版本

1:1 移植，包括：
- DIEPTSIZ 设置（PKTCNT, XFRSIZ, MCNT）
- Isochronous 帧奇偶
- DIEPCTL |= EPENA | CNAK
- DIEPEMPMSK |= INEPTXFEM(ep)

#### `usb_lld_stall_out/in` / `usb_lld_clear_out/in` → RTT 版本

简单 1:1 寄存器写。

### 4.2 宏移植

| ChibiOS 宏 | RTT 实现 | 说明 |
|---|---|---|
| `usb_lld_get_transaction_size(usbp, ep)` | `((usbp)->epc[ep]->out_state->rxcnt)` | 直接展开 |
| `usb_lld_connect_bus(usbp)` | `otgp->DCTL &= ~DCTL_SDIS` (stepping 2) | F7 用 stepping 2 |
| `usb_lld_disconnect_bus(usbp)` | `otgp->DCTL \|= DCTL_SDIS` (stepping 2) | |
| `usb_lld_wakeup_host(usbp)` | DCTL_RWUSIG + SOF 延迟 + 清除 | 用 `rt_thread_mdelay(2)` 替代 |

---

## 5. 中断处理方案

### 5.1 ISR 入口

```c
void OTG_FS_IRQHandler(void)
{
    // 1:1 对应 ChibiOS usb_lld_serve_interrupt
    // 但直接操作 rtt_usb 单例，无需 usbp 参数
    
    OSAL_IRQ_PROLOGUE();  // 空或 compiler barrier
    usb_lld_serve_interrupt(&rtt_usb);
    OSAL_IRQ_EPILOGUE();  // 空或 compiler barrier
}
```

### 5.2 `usb_lld_serve_interrupt()` 处理流程

完全按 ChibiOS 顺序 (hal_usb_lld.c:531-683)：

```
1. GINTSTS & GINTMSK → sts
2. GINTSTS = sts (clear)

顺序处理（保持此顺序！）:
  a. GINTSTS_USBRST  → _usb_reset(usbp) + return (后续事件被复位清掉)
  b. GINTSTS_WKUPINT → PCGCCTL 解门控 + DCTL.RWUSIG 清 + _usb_wakeup(usbp)
  c. GINTSTS_USBSUSP → otg_disable_ep(usbp) + _usb_suspend(usbp)
  d. GINTSTS_ENUMDNE → 根据 DSTS_ENUMSPD 调整 GUSBCFG.TRDT
  e. GINTSTS_SOF     → SOF 回调
  f. GINTSTS_IISOIXFR → otg_isoc_in_failed_handler(usbp)
  g. GINTSTS_IISOOXFR → otg_isoc_out_failed_handler(usbp)
  h. GINTSTS_RXFLVL  → otg_rxfifo_handler(usbp)  ← 必须在 OEPINT/IEPINT 之前
  i. GINTSTS_OEPINT  → DAINT → otg_epout_handler(usbp, ep)
  j. GINTSTS_IEPINT  → DAINT → otg_epin_handler(usbp, ep)
```

### 5.3 `usb_lld_poll_rtt()` — 轮询入口（保留兼容性）

```c
void usb_lld_poll_rtt(void)
{
    // 从非中断上下文调用 serve_interrupt
    // 等同于在 ISR 中调用
    
    // 检查是否有挂起中断
    uint32_t sts = rtt_usb.otg->GINTSTS;
    if (sts & rtt_usb.otg->GINTMSK) {
        usb_lld_serve_interrupt(&rtt_usb);
    }
}
```

这样 `HAL_RTT_Class.cpp` 和 `rt_board_init.c` 中现有的 `usb_lld_poll_rtt()` 调用继续工作。

### 5.4 RX FIFO Handler

完全按 ChibiOS `otg_rxfifo_handler()` (hal_usb_lld.c:285-318)：

```
GRXSTSP → PKTSTS 分发:

GRXSTSP_SETUP_DATA:
  → 读取 cnt 字节到 epc[ep]->setup_buf

GRXSTSP_SETUP_COMP:
  → no-op（ChibiOS: 在 DOEPINT.STUP 中处理 SETUP）

GRXSTSP_OUT_DATA:
  → 读取 cnt 字节到 out_state->rxbuf
  → out_state->rxbuf += cnt
  → out_state->rxcnt += cnt

GRXSTSP_OUT_COMP:
  → no-op（ChibiOS: 在 DOEPINT.XFRC 中处理 OUT 完成）

GRXSTSP_OUT_GLOBAL_NAK:
  → no-op
```

### 5.5 EP IN Handler

完全按 ChibiOS `otg_epin_handler()` (hal_usb_lld.c:374-407)：

```
DIEPINT 分发:

DIEPINT_TOC:
  → 超时，不处理（1:1）

DIEPINT_XFRC & DIEPMSK_XFRCM:
  → 如果 txcnt < totsize: 继续下一段传输
    osalSysLockFromISR() → usb_lld_start_in() → osalSysUnlockFromISR()
  → 否则: _usb_isr_invoke_in_cb(usbp, ep)

DIEPINT_TXFE & DIEPEMPMSK_INEPTXFEM(ep):
  → otg_txfifo_handler(usbp, ep) 填充更多数据
```

### 5.6 EP OUT Handler

完全按 ChibiOS `otg_epout_handler()` (hal_usb_lld.c:417-465)：

```
DOEPINT 分发:

DOEPINT_STUP & DOEPMSK_STUPM:
  → _usb_isr_invoke_setup_cb(usbp, ep)

DOEPINT_XFRC & DOEPMSK_XFRCM:
  → EP0 特殊处理:
    如果 rxcnt % out_maxsize == 0 && rxsize < totsize:
      继续下一段: osalSysLockFromISR → usb_lld_start_out → unlock
    否则: _usb_isr_invoke_out_cb(usbp, ep)
```

---

## 6. CDC ACM 集成方案

### 6.1 分层

```
usb_lld_* (核心 DWC2) → RT_USBDriver + USB HAL 层
  ^
  |
usb_cdc_rtt.c (CDC ACM 类实现)
  ^
  |
UARTDriver.cpp (用户 API)
```

### 6.2 CDC 层职责

`usb_cdc_rtt.c` 提供：

```c
// 初始化 CDC ACM 设备（在 usb_lld_init 之后调用）
void usb_cdc_init(void);

// 发送数据（对 UARTDriver 的 usb_lld_send_rtt 封装）
bool usb_cdc_send_data(const uint8_t *data, uint32_t len);

// 接收回调注册（对 UARTDriver 的 usb_lld_set_rx_callback 封装）
void usb_cdc_set_rx_callback(usb_rx_callback_t cb, void *arg);

// 发送 CDC 通知
bool usb_cdc_send_notification(uint16_t serial_state);

// 重武装 EP2 OUT
void usb_cdc_rearm_out(void);

// 检查是否配置
bool usb_cdc_is_configured(void);
bool usb_cdc_is_connected(void);
```

### 6.3 EP 映射

| EP | 方向 | 类型 | MPS | 用途 | TX FIFO (words) |
|----|------|------|-----|------|-----------------|
| 0 | IN/OUT | Control | 64 | EP0 (setup + data + status) | 16 |
| 1 | IN | Bulk | 64 | CDC 数据 (device→host) | 32 |
| 2 | OUT | Bulk | 64 | CDC 数据 (host→device) | — |
| 3 | IN | Interrupt | 8 | CDC 通知 (device→host) | 4 |

### 6.4 CDC 事件处理

当 `_usb_ep0setup()` 收到 SET_CONFIGURATION 时，触发 `USB_EVENT_CONFIGURED` 事件。CDC 层在事件回调中初始化 EP1/EP2/EP3：

```c
static void usb_event_cb(void *usbp, uint8_t event, void *user_data)
{
    if (event == USB_EVENT_CONFIGURED) {
        // 1. 调用 usb_lld_init_endpoint(usbp, 1) (bulk IN)
        // 2. 调用 usb_lld_init_endpoint(usbp, 2) (bulk OUT)
        // 3. 调用 usb_lld_init_endpoint(usbp, 3) (interrupt IN)
        // 4. 调用 usb_lld_start_out(usbp, 2)  (arm 接收)
    }
    if (event == USB_EVENT_RESET) {
        // 标记未配置
    }
}
```

---

## 7. 文件组织

```
libraries/AP_HAL_RTT/
├── hal_usb_lld_rtt.h         ← 新: 公共 API (usb_lld_send_rtt 等 + 新的 usb_lld_*)
├── hal_usb_lld_rtt.c         ← 新重写: DWC2 LLD + serve_interrupt + ISR
├── hal_usb_rtt.h             ← 新: RT_USBDriver 结构体、RTT_USBConfig 等
├── hal_usb_rtt.c             ← 新: _usb_reset/_usb_suspend/_usb_wakeup/
│                                    _usb_ep0setup/_usb_ep0in/_usb_ep0out
│                                    default_handler (标准请求处理)
│                                    usbSetupTransfer / usbObjectInit
├── usb_cdc_rtt.h             ← 新: CDC ACM 层 API
├── usb_cdc_rtt.c             ← 新: 描述符、类请求处理、EP1/2/3 管理
└── UARTDriver.cpp            ← 不变 (仅用 usb_lld_send_rtt 等)
```

### 旧文件清理

```
hal_usb_lld_rtt.c (现有)  → 删除
hal_usb_lld_rtt.h (现有)  → 删除
hal_usb_lld_rtt.c.bak     → 删除
hal_usb_lld_rtt.h.bak     → 删除
```

---

## 8. 与现有代码的兼容层

为了不修改 UARTDriver.cpp 和其他调用者，在 `hal_usb_lld_rtt.h` 中提供兼容包装：

```c
// 保留现有 API 签名不变
bool usb_lld_send_rtt(uint8_t ep, const uint8_t *data, uint32_t len);
void usb_lld_set_rx_callback(usb_rx_callback_t cb, void *arg);
void usb_lld_rearm_cdc_out(void);
bool usb_lld_is_configured_rtt(void);
bool usb_lld_get_connected_rtt(void);

// 这些在新方案中实现为：
static inline bool usb_lld_send_rtt(uint8_t ep, const uint8_t *data, uint32_t len)
{
    (void)ep; // 始终是 EP1
    return usb_cdc_send_data(data, len);
}

static inline void usb_lld_set_rx_callback(usb_rx_callback_t cb, void *arg)
{
    usb_cdc_set_rx_callback(cb, arg);
}

static inline void usb_lld_rearm_cdc_out(void)
{
    usb_cdc_rearm_out();
}
```

---

## 9. 中断上下文与锁

### 9.1 中断优先级

| 中断 | 优先级 | 说明 |
|------|--------|------|
| OTG_FS_IRQn | 5 (NVIC group prio) | 低优先级，允许 TX FIFO 填充期间不被更高优先级 USB 中断打断 |

### 9.2 FIFO 填充保护

ChibiOS 使用 BASEPRI 保护 FIFO 写操作（`STM32_USB_OTGFIFO_FILL_BASEPRI`）：
```c
// 在 otg_fifo_write_from_buffer 调用之前
__set_BASEPRI(CORTEX_PRIO_MASK(priority));
// ... FIFO 写操作 ...
__set_BASEPRI(0);
```

RTT 实现完全复用此机制。

---

## 10. EP0 状态机集成

### 10.1 状态迁移

```
IDLE
  → SETUP 包到达 (DOEPINT_STUP)
    → 调用 _usb_ep0setup(usbp, ep)
      → default_handler() 或钩子处理
        → 需要 IN 数据 → WAITING_DATA_IN
        → 需要 OUT 数据 → WAITING_DATA_OUT
        → 只需要状态 → WAITING_STATUS_IN
        → 错误 → STALL

WAITING_DATA_IN
  → IN 完成 (DIEPINT_XFRC)
    → 调用 _usb_ep0in(usbp, ep)
      → 还有更多数据 → WAITING_DATA_IN (继续)
      → 数据完成 → WAITING_STATUS_OUT

WAITING_DATA_OUT
  → OUT 完成 (DOEPINT_XFRC)
    → 调用 _usb_ep0out(usbp, ep)
      → 还有更多数据 → WAITING_DATA_OUT (继续)
      → 数据完成 → 调用 ep0endcb → WAITING_STATUS_IN

WAITING_STATUS_IN
  → IN 完成 (DIEPINT_XFRC)
    → 调用 _usb_ep0in(usbp, ep)
      → IDLE

WAITING_STATUS_OUT
  → OUT 完成 (DOEPINT_XFRC)
    → 调用 _usb_ep0out(usbp, ep)
      → IDLE
```

### 10.2 与当前 RTT EP0 逻辑的差异

| 当前 RTT 行为 | ChibiOS/新驱动行为 |
|--------------|-------------------|
| EP0 IN 直接在 `_ep0_send_data` 中写 FIFO + 轮询 XFRC | EP0 IN 通过中断驱动：`_usb_ep0in` 回调 + `DIEPINT_XFRC` |
| EP0 OUT 直接在 `_ep0_arm_out` 中间接处理 | EP0 OUT 通过 `DOEPINT_XFRC` 分发到 `_usb_ep0out` |
| 标准请求和类请求在一个函数处理 | 标准请求在 `default_handler`，类请求通过 `requests_hook_cb` |
| SET_ADDRESS 立即应用 | 通过 `USB_EARLY_SET_ADDRESS` + `usb_lld_set_address` |

---

## 11. 实现步骤

### Step 1: 创建 RT_USBDriver 结构体和初始化

- 在 `hal_usb_rtt.h` 中定义所有结构体
- 实现 `usbObjectInit()`（清空结构体）
- `usbSetupTransfer()`（配置 EP0 传输）

### Step 2: 移植 LLD 内部函数

- `otg_core_reset()` — 直接寄存器操作
- `otg_disable_ep()`, `otg_rxfifo_flush()`, `otg_txfifo_flush()`
- `otg_ram_reset()`, `otg_ram_alloc()`
- `otg_fifo_write_from_buffer()`, `otg_fifo_read_to_buffer()`

### Step 3: 移植 LLD 公共函数

- 移植全部 14 个 `usb_lld_*` 函数
- 暂时保留 `usb_lld_serve_interrupt()` 作为静态函数

### Step 4: 移植中断处理

- `usb_lld_serve_interrupt()` 完整
- `otg_rxfifo_handler()`, `otg_epin_handler()`, `otg_epout_handler()`
- `otg_txfifo_handler()` — TX FIFO 填充
- `OTG_FS_IRQHandler` ISR

### Step 5: 移植 HAL 层

- `_usb_reset()`, `_usb_suspend()`, `_usb_wakeup()`
- `_usb_ep0setup()`, `_usb_ep0in()`, `_usb_ep0out()`
- `default_handler()` — 所有标准请求

### Step 6: 创建 CDC ACM 层

- 从当前 `hal_usb_lld_rtt.c` 提取 CDC 描述符
- CDC 类请求处理
- EP1/EP2/EP3 初始化（在 `USB_EVENT_CONFIGURED` 时）
- `usb_cdc_send_data()` 实现

### Step 7: 添加兼容包装

- `usb_lld_send_rtt()` → `usb_cdc_send_data()`
- `usb_lld_set_rx_callback()` → `usb_cdc_set_rx_callback()`
- `usb_lld_rearm_cdc_out()` → `usb_cdc_rearm_out()`
- `usb_lld_poll_rtt()` → 非中断模式下轮询 `usb_lld_serve_interrupt`

### Step 8: 集成测试

1. 编译通过（`scons --v=ArduCopter --target=cuav_v5`）
2. L6_cdc test firmware 编译 + 枚举验证
3. OpenOCD halt/resume 无 HardFault
4. CDC 心跳验证

**关键测试点**: 中断频率！之前轮询模式大约每 1ms 调用一次 `usb_lld_poll_rtt`，中断模式下 OTG_FS_IRQHandler 会在每次 SOF (1ms) 加上每个 USB 事件时触发。确保 ISR 不长时间占用 CPU。

---

## 12. 注意事项

1. **`#include` 路径**: 新文件需要 `#include <stm32f7xx.h>`（或 CMSIS 头）和已有的 `stm32_otg.h`。不能依赖 `"hal.h"`（ChibiOS 专属）。

2. **F7 stepping**: STM32F767 是 stepping 2，所以 `GCCFG` = `GCCFG_VBDEN | GCCFG_PWRDWN`, `usb_lld_connect_bus` = `DCTL &= ~DCTL_SDIS`, `usb_lld_disconnect_bus` = `DCTL |= DCTL_SDIS`。

3. **FIFO 地址**: RTT 当前驱动用 `_FIFO(n)` 宏定义 FIFO 地址。新驱动用 `usbp->otg->FIFO[ep]`（ChibiOS 风格）或保持宏。

4. **DIEPTXF 索引**: ChibiOS 中 `DIEPTXF[ep - 1]` 用于 EP>0。`DIEPTXF0`（在 global reg 区）用于 EP0。注意当前 RTT 驱动用 `DIEPTXF0`（通过 `DIEPTXF0_HNPTXFSIZ` 寄存器）来配置 EP0 TX FIFO。

5. **延迟函数**: 需要 `osalSysPolledDelayX` 的简单 NOP 延迟。在 ISR 中不能用 `rt_thread_mdelay`。提供一个 `static void delay_nops(uint32_t n)` 用 DWT 或简单循环。

6. **OTG_FS 基地址**: F7 是 `0x50000000`（非 F7/H7 系列用 `0x40080000`）。新驱动直接通过 `stm32_otg_t*` 指针访问。

7. **__DSB() / __ISB() 使用**: ChibiOS LLD 中没有大量使用 DSB/ISB。当前 RTT 驱动过度使用了这些屏障。新驱动限制 DSB/ISB 到关键的寄存器序列后（时钟使能、EP enable 等），遵循 ChibiOS 的节奏。

8. **`rt_thread_mdelay` vs 延迟**: `osalThreadSleepMilliseconds` (在 `usb_lld_wakeup_host` 中) 可以保留为 `rt_thread_mdelay(2)`，因为那是线程上下文。但 `osalSysPolledDelayX` 必须在 ISR 中用 NOP 延迟。

9. **`OSAL_IRQ_PROLOGUE/EPILOGUE`**: ChibiOS 中这些宏在某些平台上处理中断嵌套/尾链。在 Cortex-M7/RTT 上可以内联为空或简单的 compiler barrier。

---

## 13. 验证标准

| 测试 | 标准 |
|------|------|
| 编译 | `scons --v=ArduCopter --target=cuav_v5` 无错误 |
| 链接 | 所有符号正确，无遗漏 |
| L6_cdc test | test 固件编译 + 在板子上 USB 枚举 |
| OpenOCD 验证 | `halt` 后检查 PC 位置正常，`resume` 30s 无 HardFault |
| CDC 心跳 | `pymavlink` 收到 HEARTBEAT，状态 STANDBY |
| 中断频率 | ISR 入口计数合理（每 SOF 1 次 + 事件次数） |
| CDC 数据 | 双向数据收发正常 |
