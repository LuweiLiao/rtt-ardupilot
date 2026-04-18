# CAN Driver Feasibility Report — RTT CUAV V5

## 1. RT-Thread STM32F7 CAN Driver

**✅ Available.** RT-Thread has full CAN driver stack for STM32:

- HAL driver: `modules/rt-thread/bsp/stm32/stm32f765-cuav-v5/packages/stm32f7_hal_driver-latest/Src/stm32f7xx_hal_can.c`
- RT-Thread BSP driver: `modules/rt-thread/bsp/stm32/libraries/HAL_Drivers/drivers/drv_can.c` (bxCAN)
- RT-Thread BSP driver: `modules/rt-thread/bsp/stm32/libraries/HAL_Drivers/drivers/drv_fdcan.c` (FDCAN, for F7 with FDCAN peripheral)
- RT-Thread CAN framework: `modules/rt-thread/components/drivers/can/dev_can.c`, `modules/rt-thread/components/drivers/include/drivers/dev_can.h`

## 2. AP_CANManager in AP_HAL_RTT

**❌ Not implemented.** Zero references to `AP_CAN` or `CANManager` in `libraries/AP_HAL_RTT/`. The RTT HAL has no CAN integration yet.

AP_HAL layer defines `AP_CAN_SLCAN_ENABLED` in `AP_HAL_Boards.h` (line 280-284) — currently enabled by default for boards that support it.

## 3. hwdef.dat CAN Pins (RTT)

```
# ---- CAN2 ----
PB12 CAN2_RX  CAN2
PB13 CAN2_TX  CAN2
PI8  GPIO_CAN2_SILENT  OUTPUT  PUSHPULL  SPEED_LOW  LOW  GPIO(71)

# ---- CAN1 ----
PI9  CAN1_RX  CAN1
PH13 CAN1_TX  CAN1

# ---- CAN3 silent ----
PH4  GPIO_CAN3_SILENT  OUTPUT  PUSHPULL  SPEED_LOW  LOW  GPIO(72)
```

Note: CAN1 and CAN2 have RX/TX defined. CAN3 only has a silent control pin (no RX/TX).

## 4. ChibiOS hwdef.dat CAN Pins (Reference)

```
PI9  CAN1_RX CAN1
PH13 CAN1_TX CAN1
PB12 CAN2_RX CAN2
PB13 CAN2_TX CAN2
PH2 GPIO_CAN1_SILENT OUTPUT PUSHPULL SPEED_LOW LOW GPIO(70)
PH3 GPIO_CAN2_SILENT OUTPUT PUSHPULL SPEED_LOW LOW GPIO(71)
PH4 GPIO_CAN3_SILENT OUTPUT PUSHPULL SPEED_LOW LOW GPIO(72)
```

ChibiOS has CAN1_SILENT (PH2) and CAN2_SILENT (PH3) defined; RTT hwdef is missing CAN1_SILENT (PH2).

## 5. AP_CANManager Library

Key files:
- `AP_CANManager.cpp/.h` — CAN bus manager
- `AP_CANDriver.h` — CAN driver interface (abstract)
- `AP_CANSensor.cpp/.h` — CAN sensor base
- `AP_SLCANIface.cpp/.h` — SLCAN (serial-CAN) interface
- `AP_MAVLinkCAN.cpp/.h` — MAVLink over CAN

## 6. Conclusion & Next Steps

| Item | Status |
|------|--------|
| RTT CAN HAL driver (bxCAN) | ✅ Available |
| RTT CAN framework (dev_can) | ✅ Available |
| hwdef CAN pin definitions | ✅ CAN1/CAN2 defined, missing CAN1_SILENT (PH2) |
| AP_HAL_RTT CAN integration | ❌ Not started |
| AP_CANManager (ArduPilot layer) | ✅ Exists, needs HAL backend |

**Work needed:**
1. Implement `AP_HAL_RTT::CANManager` (or `CANDriver`) that wraps RT-Thread's `rt_device_t` CAN interface
2. Fix hwdef.dat: add `PH2 GPIO_CAN1_SILENT OUTPUT PUSHPULL SPEED_LOW LOW GPIO(70)`
3. Enable RT-Thread CAN driver in menuconfig / rtconfig.h
4. Wire up in `AP_HAL_RTT_Main` constructor
