#!/bin/bash
set -e
cd /home/llw/firmare/pogo-apm

BASELINE=libraries/AP_HAL_RTT/hwdef/cuav_v5/hwdef.dat.baseline
HWDEF=libraries/AP_HAL_RTT/hwdef/cuav_v5/hwdef.dat
BUILD_LOG=/tmp/bisect_uart.log

flash_and_test() {
    echo "  Flashing..."
    openocd -f interface/stlink.cfg -f target/stm32f7x.cfg \
        -c "init; halt; program Tools/bootloaders/CUAVv5_bl.bin 0x08000000 verify; program build/rtt_deploy/cuav_v5/rtthread.bin 0x08008000 verify; reset run; shutdown" 2>&1 | tail -3

    echo "  Waiting for boot + heartbeat..."
    sleep 20
    timeout 12 /home/llw/venv-ardupilot/bin/python3 -c "
import pymavlink.mavutil as mu
m=mu.mavlink_connection('/dev/ttyACM1',baud=115200)
hb=m.wait_heartbeat(timeout=10)
print('OK' if hb else 'FAIL')
" 2>&1
}

build_test() {
    local desc="$1"
    local extra_pins="$2"
    
    echo ""
    echo "========================================="
    echo "TEST: $desc"
    echo "Pins: $extra_pins"
    echo "========================================="
    
    # Restore baseline
    cp "$BASELINE" "$HWDEF"
    
    # Add pins if any
    if [ -n "$extra_pins" ]; then
        echo "$extra_pins" >> "$HWDEF"
    fi
    
    # Build
    echo "  Building..."
    rm -rf build/rtt_cuav_v5 build/rtt_deploy
    if ! /home/llw/venv-ardupilot/bin/scons --target=cuav_v5 -j4 2>&1 | tail -3; then
        echo "  BUILD FAILED - skipping"
        return 2
    fi
    
    # Flash and test
    local result
    result=$(flash_and_test)
    echo "  Result: $result"
    if echo "$result" | grep -q "^OK$"; then
        echo "  >>> PASS"
        return 0
    else
        echo "  >>> HANG"
        return 1
    fi
}

# Step 1: Verify baseline boots
echo "===== STEP 1: Verify baseline ====="
cp "$BASELINE" "$HWDEF"
rm -rf build/rtt_cuav_v5 build/rtt_deploy
echo "  Building baseline..."
/home/llw/venv-ardupilot/bin/scons --target=cuav_v5 -j4 2>&1 | tail -3
result=$(flash_and_test)
echo "  Baseline result: $result"
if ! echo "$result" | grep -q "^OK$"; then
    echo "FATAL: Baseline doesn't boot!"
    exit 1
fi
echo "  Baseline OK, proceeding..."

# Step 2: Test each pair
echo ""
echo "===== STEP 2: Test each UART flow control pair ====="

PAIRS=(
    "USART2:PD3_CTS,PD4_RTS|PD3  USART2_CTS  USART2  AF7
PD4  USART2_RTS  USART2  AF7"
    "USART3:PD11_CTS,PD12_RTS|PD11 USART3_CTS  USART3  AF7
PD12 USART3_RTS  USART3  AF7"
    "USART6:PG12_CTS,PG13_RTS|PG12 USART6_CTS  USART6  AF8
PG13 USART6_RTS  USART6  AF8"
    "UART7:PE10_CTS,PF8_RTS|PE10 UART7_CTS  UART7  AF8
PF8  UART7_RTS  UART7  AF8"
)

failed_pairs=()
for entry in "${PAIRS[@]}"; do
    name="${entry%%|*}"
    pins="${entry#*|}"
    if build_test "Pair $name" "$pins"; then
        : # pass
    else
        rc=$?
        if [ $rc -eq 2 ]; then
            echo "  Build error, skipping pair $name"
        else
            echo "  FAIL: Pair $name causes hang!"
            failed_pairs+=("$entry")
        fi
    fi
done

# Step 3: For failed pairs, test individual pins
if [ ${#failed_pairs[@]} -gt 0 ]; then
    echo ""
    echo "===== STEP 3: Narrow down individual pins ====="
    
    for entry in "${failed_pairs[@]}"; do
        name="${entry%%|*}"
        pins_raw="${entry#*|}"
        # Split into individual pin lines
        cts_pin=$(echo "$pins_raw" | head -1)
        rts_pin=$(echo "$pins_raw" | tail -1)
        
        echo ""
        echo "--- Narrowing $name ---"
        
        build_test "$name CTS only" "$cts_pin" || true
        build_test "$name RTS only" "$rts_pin" || true
    done
fi

# Step 4: If all pairs pass individually, test combinations
if [ ${#failed_pairs[@]} -eq 0 ]; then
    echo ""
    echo "===== STEP 4: All pairs pass individually. Testing all 4 together ====="
    ALL_PINS="PD3  USART2_CTS  USART2  AF7
PD4  USART2_RTS  USART2  AF7
PD11 USART3_CTS  USART3  AF7
PD12 USART3_RTS  USART3  AF7
PG12 USART6_CTS  USART6  AF8
PG13 USART6_RTS  USART6  AF8
PE10 UART7_CTS  UART7  AF8
PF8  UART7_RTS  UART7  AF8"
    build_test "All 8 flow control pins" "$ALL_PINS" || true
    
    echo ""
    echo "===== STEP 5: Testing pairs in combinations of 2 ====="
    declare -a PASS_PAIRS=(
        "PD3  USART2_CTS  USART2  AF7
PD4  USART2_RTS  USART2  AF7"
        "PD11 USART3_CTS  USART3  AF7
PD12 USART3_RTS  USART3  AF7"
        "PG12 USART6_CTS  USART6  AF8
PG13 USART6_RTS  USART6  AF8"
        "PE10 UART7_CTS  UART7  AF8
PF8  UART7_RTS  UART7  AF8"
    )
    # Test 2-pair combos
    for i in 0 1 2 3; do
        for j in $(seq $((i+1)) 3); do
            combo="${PASS_PAIRS[$i]}
${PASS_PAIRS[$j]}"
            names=("USART2" "USART3" "USART6" "UART7")
            build_test "Pair ${names[$i]} + ${names[$j]}" "$combo" || true
        done
    done
fi

echo ""
echo "===== COMPLETE ====="
