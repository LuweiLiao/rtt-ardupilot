#!/usr/bin/env python3
"""Burn + verify CUAV V5 firmware via PX4 bootloader."""
import os, sys, time, subprocess
import struct

WORKDIR = "/data/firmare/pogo-apm"
APJ = "build/rtt_cuav_v5/arducopter.apj"

def run(cmd, timeout=60):
    print(f"RUN: {cmd}")
    result = subprocess.run(cmd, shell=True, capture_output=True, text=True, timeout=timeout, cwd=WORKDIR)
    if result.returncode != 0:
        print(f"EXIT={result.returncode}")
        print(result.stderr[:500])
    print(result.stdout[:1000])
    return result

def main():
    # Step 1: Check current board state
    print("=== Step 1: Check ACM ports ===")
    run("ls -la /dev/ttyACM* 2>&1")
    run("lsusb 2>&1")

    # Step 2: Send MAVLink reboot to bootloader
    print("\n=== Step 2: Try MAVLink reboot to PX4 bootloader ===")
    try:
        from pymavlink import mavutil
        master = mavutil.mavlink_connection("/dev/ttyACM0", baud=115200)
        master.wait_heartbeat(timeout=5)
        print(f"Heartbeat from system {master.target_system}, type {master.target_type}")

        # MAV_CMD_PREFLIGHT_REBOOT_SHUTDOWN, param1=1 (reboot), param2=1 (jump to bootloader)
        master.mav.command_long_send(
            master.target_system, master.target_component,
            246,  # MAV_CMD_PREFLIGHT_REBOOT_SHUTDOWN
            0,    # confirmation
            1,    # param1: reboot
            1,    # param2: bootloader
            0, 0, 0, 0, 0
        )
        print("Sent reboot-to-bootloader command")
        time.sleep(0.5)
        master.close()
    except Exception as e:
        print(f"MAVLink approach failed: {e}")
        print("The board may not be running RTT firmware, or may not have MAVLink")

    # Step 3: Wait for bootloader to appear
    print("\n=== Step 3: Wait for PX4 bootloader (1209:5741, max 15s) ===")
    found_bl = False
    for i in range(30):
        time.sleep(0.5)
        result = subprocess.run("lsusb", shell=True, capture_output=True, text=True)
        if "1209:5741" in result.stdout or "BL" in result.stdout:
            print(f"Bootloader detected at t={i*0.5:.1f}s: {result.stdout.strip()}")
            found_bl = True
            break
        if i % 4 == 0:
            print(f"  Waiting... (lsusb: {result.stdout[:200].strip()})")

    if not found_bl:
        print("Bootloader not found. Trying alternative...")
        result = subprocess.run("ls -la /dev/ttyACM* 2>&1", shell=True, capture_output=True, text=True)
        print(f"ACM ports: {result.stdout}")
        return False

    # Step 4: Upload firmware
    print(f"\n=== Step 4: Upload firmware via uploader.py ===")
    time.sleep(1)  # stable bootloader
    result = subprocess.run(
        f"python3 Tools/scripts/uploader.py --port /dev/ttyACM0 --force {APJ}",
        shell=True, capture_output=True, text=True,
        timeout=60, cwd=WORKDIR
    )
    print(f"Upload stdout: {result.stdout[:2000]}")
    print(f"Upload stderr: {result.stderr[:1000]}")
    if result.returncode != 0:
        print(f"Upload failed with code {result.returncode}")
        return False

    # Step 5: Wait for board to restart (RTT firmware)
    print("\n=== Step 5: Wait for RTT firmware to start (15s) ===")
    time.sleep(15)

    # Step 6: Verify with MAVLink
    print("\n=== Step 6: MAVLink verification ===")
    try:
        master = mavutil.mavlink_connection("/dev/ttyACM0", baud=115200)
        master.wait_heartbeat(timeout=15)
        print(f"HEARTBEAT received: system={master.target_system}, type={master.target_type}")
        master.close()
        print("SUCCESS: Firmware running and MAVLink active")
        return True
    except Exception as e:
        print(f"MAVLink verification failed: {e}")
        # Check if ACM port exists after upload
        subprocess.run("ls -la /dev/ttyACM* 2>&1", shell=True)
        subprocess.run("lsusb 2>&1", shell=True)
        return False

if __name__ == "__main__":
    success = main()
    print(f"\n=== RESULT: {'SUCCESS' if success else 'FAILED'} ===")
    sys.exit(0 if success else 1)
