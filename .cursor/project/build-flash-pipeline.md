# Build → Flash → Verify Pipeline Report

**Date:** 2026-04-18 20:53 CST
**Project:** pogo-apm (ArduPilot RT-Thread, CUAV V5 / STM32F767)

---

## Step 1: Clean Build ✅

**Command:**
```bash
/home/llw/venv-ardupilot/bin/scons --target=cuav_v5 -j4 --clean
/home/llw/venv-ardupilot/bin/scons --target=cuav_v5 -j4
```

**Result:** Success, no errors.
**Build time:** 1m31s (5m18s user, 0m38s sys — 4 cores)

**Memory usage:**
| Region | Used | Total | % |
|--------|------|-------|---|
| ROM | 1,279,852 B | 1,504 KB | 83.10% |
| RAM | 177,920 B | 512 KB | 33.94% |

**ELF size:** text=1,279,852 data=5,252 bss=172,628 total=1,457,732

---

## Step 2: Verify Output Files ✅

```
build/rtt_deploy/cuav_v5/rtthread.bin  1,285,104 bytes
build/rtt_deploy/cuav_v5/rt-thread.elf 38,437,944 bytes
build/rtt_cuav_v5/rtthread.bin          1,285,104 bytes
```

---

## Step 3: ST-Link Detection ✅

**Result:** STLINK V2J41S7 detected, Cortex-M7 r1p0, target voltage 3.244V, 8 breakpoints / 4 watchpoints.

---

## Step 4: Full Flash (Bootloader + App) ✅

**Command:**
```bash
openocd -f interface/stlink.cfg -f target/stm32f7x.cfg \
  -c "init; halt; \
      program Tools/bootloaders/CUAVv5_bl.bin 0x08000000 verify; \
      program build/rtt_deploy/cuav_v5/rtthread.bin 0x08008000 verify; \
      reset run; shutdown"
```

**Flash time:** 25s
**Bootloader:** Verified OK
**App:** Verified OK
**Flash size:** 2048 KiB (STM32F767)

---

## Step 5: MAVLink Verification ✅

**First attempt (after openocd reset run):** HEARTBEAT TIMEOUT
**Second attempt (after GDB reset run):** HEARTBEAT OK

```
HEARTBEAT OK type=2 autopilot=3 base_mode=0x51
59 msgs, 15 types in 5s
```

- type=2 (MAV_TYPE_QUADROTOR), autopilot=3 (MAV_AUTOPILOT_ARDUPILOTMEGA)
- base_mode=0x51 = MAV_MODE_FLAG_SAFETY_ARMED | MAV_MODE_FLAG_STABILIZE_ENABLED | MAV_MODE_FLAG_HIL_ENABLED... (system ready)

### ⚠️ Issue: openocd "reset run; shutdown" doesn't fully start CPU

The `shutdown` command races with the boot process and can halt the CPU. After openocd exits, the MCU appears to be stuck. GDB `monitor reset run` followed by detach fixes this.

---

## Step 6: GDB Register Check ✅

CPU running normally in `AP_HAL::UARTDriver::write()` when halted — indicates active MAVLink streaming.

Key registers: pc=0x806b0c0, sp=0x200522e4 (in PSP), msp=0x2000d484, xPSR=0x21000000.

---

## One-Command Pipeline Script

Save as `flash.sh` in project root:

```bash
#!/bin/bash
set -e
cd /home/llw/firmare/pogo-apm

SCONS=/home/llw/venv-ardupilot/bin/scons
GDB=/opt/gcc-arm-none-eabi-10-2020-q4-major/bin/arm-none-eabi-gdb
OPENOCD=openocd
PYTHON=/home/llw/venv-ardupilot/bin/python3
TARGET=cuav_v5
BL=Tools/bootloaders/CUAVv5_bl.bin
APP=build/rtt_deploy/$TARGET/rtthread.bin
ELF=build/rtt_deploy/$TARGET/rt-thread.elf
PORT=/dev/ttyACM1

echo "=== BUILD ==="
time $SCONS --target=$TARGET -j4

echo "=== FLASH ==="
time $OPENOCD -f interface/stlink.cfg -f target/stm32f7x.cfg \
  -c "init; halt; program $BL 0x08000000 verify; program $APP 0x08008000 verify; shutdown"

echo "=== GDB RESET ==="
$GDB -batch -ex "set remotetimeout 10" \
  -ex "target remote | $OPENOCD -f interface/stlink.cfg -f target/stm32f7x.cfg -c 'gdb_port pipe; log_output /dev/null'" \
  -ex "monitor halt" -ex "monitor reset run" -ex "quit" $ELF

echo "=== WAIT & VERIFY ==="
sleep 20
$PYTHON -c "
import pymavlink.mavutil as mu
m = mu.mavlink_connection('$PORT', baud=115200)
hb = m.wait_heartbeat(timeout=15)
if hb:
    print(f'HEARTBEAT OK type={hb.type} autopilot={hb.autopilot} base_mode=0x{hb.base_mode:02x}')
else:
    print('HEARTBEAT TIMEOUT')
    exit(1)
"

echo "=== ALL DONE ==="
```

---

## Known Gotchas & Workarounds

1. **openocd `reset run; shutdown` race condition** — Use GDB `monitor reset run` + `quit` instead for reliable CPU start.
2. **USB port is `/dev/ttyACM1`** (NOT ttyACM0). The CUAV V5 appears as a second CDC device.
3. **Always flash both bootloader AND app together** — The bootloader validates the app CRC on boot.
4. **Wait 20 seconds after flash** — Bootloader init + RT-Thread startup takes ~15-20s.
5. **Build uses scons only** — Do NOT use waf.
6. **ST-Link speed negotiation** — openocd may downgrade from 2000kHz to 1800kHz; harmless warning.
7. **ROM at 83%** — Approaching limit; watch for future growth.
