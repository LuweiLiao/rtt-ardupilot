#!/bin/bash
# Full test suite runner for ArduPilot RTT on CUAV v5
# Usage: bash tests/run_all.sh [--port /dev/ttyACM1]

set -e
cd "$(dirname "$0")/.."
source /home/llw/venv-ardupilot/bin/activate

PORT="/dev/ttyACM1"
if [ "$1" = "--port" ] && [ -n "$2" ]; then
    PORT="$2"
fi

if [ ! -e "$PORT" ]; then
    echo "ERROR: $PORT not found"
    # Try alternative
    for d in /dev/ttyACM*; do
        if [ "$d" != "/dev/ttyACM0" ] && [ -e "$d" ]; then
            PORT="$d"
            echo "Using alternative: $PORT"
            break
        fi
    done
fi

echo "=========================================="
echo "  ArduPilot RTT Full Test Suite"
echo "  $(date '+%Y-%m-%d %H:%M:%S')"
echo "  Port: $PORT"
echo "=========================================="

PASS=0
FAIL=0
TOTAL=0

run() {
    local name="$1"
    local script="$2"
    TOTAL=$((TOTAL+1))
    echo ""
    echo ">>> [$TOTAL] $name"
    local exit_code=0
    python3 "$script" --port "$PORT" 2>&1 | tee /tmp/test_last.log || exit_code=$?
    if [ $exit_code -eq 0 ]; then
        PASS=$((PASS+1))
        echo "    RESULT: PASS"
    else
        FAIL=$((FAIL+1))
        echo "    RESULT: FAIL"
    fi
}

# Core tests
run "MAVFTP" tests/test_mavftp.py
run "Mission Protocol" tests/test_mission_protocol.py
run "SET_MESSAGE_INTERVAL" tests/test_set_message_interval.py
run "Log Download" tests/test_log_download.py
run "Full Functional (5 rounds)" tests/test_full_functional.py -- --rounds 5

echo ""
echo "=========================================="
echo "  SUMMARY  $(date '+%Y-%m-%d %H:%M:%S')"
echo "=========================================="
echo "  PASS: $PASS / $TOTAL"
echo "  FAIL: $FAIL / $TOTAL"
if [ $FAIL -eq 0 ]; then
    echo "  RESULT: ALL PASS ✓"
else
    echo "  RESULT: $FAIL FAILURES ✗"
fi
