#!/usr/bin/env python3
"""
OpenOCD flash programmer via telnet interface.
Erases sectors 1-9 (application area) and writes rtthread.bin.
"""
import telnetlib
import time
import os
import sys
import subprocess
import signal

WORKDIR = "/data/firmare/pogo-apm"
BIN_PATH = os.path.join(WORKDIR, "build/rtt_cuav_v5/rtthread.bin")

def run_openocd():
    """Start OpenOCD in background."""
    cmd = [
        "openocd",
        "-f", "interface/stlink.cfg",
        "-f", "target/stm32f7x.cfg",
        "-c", "adapter speed 200",
        "-c", "transport select hla_swd",
        "-c", "init",
        "-c", "halt",
        "-c", "gdb_port 0",  # disable GDB server
    ]
    proc = subprocess.Popen(
        cmd, cwd=WORKDIR,
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
        preexec_fn=lambda: signal.signal(signal.SIGCHLD, signal.SIG_DFL)
    )
    time.sleep(3)
    return proc

def telnet_cmd(tn, cmd, timeout=30):
    """Send command to OpenOCD telnet and return output."""
    tn.write(cmd.encode() + b"\n")
    time.sleep(0.2)
    # Read until next prompt
    output = b""
    deadline = time.time() + timeout
    while time.time() < deadline:
        try:
            data = tn.read_very_eager()
            output += data
            if b"> " in output:
                break
        except:
            break
        time.sleep(0.1)
    return output.decode(errors="replace")

def main():
    print("Starting OpenOCD...")
    proc = run_openocd()
    
    print("Connecting via telnet...")
    tn = telnetlib.Telnet("localhost", 4444, timeout=10)
    time.sleep(0.5)
    
    # Read greeting
    greeting = tn.read_very_eager().decode(errors="replace")
    print(f"Greeting: {greeting[:200]}")
    
    # Check flash info
    print("\n=== Flash info ===")
    out = telnet_cmd(tn, "flash list")
    print(out)
    
    # Try to erase sectors 1-9 first (application area: 0x08008000 onwards)
    # Sector 0 = 0x08000000 (32KB bootloader)
    # Sector 1-3 = 32KB each (96KB)
    # Sector 4 = 128KB
    # Sector 5+ = 256KB each
    # Application starts at sector 1 (0x08008000)
    print("\n=== Erasing sectors 1-9 ===")
    out = telnet_cmd(tn, "flash erase_sector 0 1 9", timeout=60)
    print(out)
    
    # Check if sector erase was successful
    if "error" in out.lower() and "timeout" in out.lower():
        print("Sector erase timed out. Trying manual unlock...")
        # Unlock flash
        out = telnet_cmd(tn, "mww 0x40023C04 0x45670123")
        print(f"Key1: {out}")
        out = telnet_cmd(tn, "mww 0x40023C04 0xCDEF89AB")
        print(f"Key2: {out}")
        
        # Try erase again
        out = telnet_cmd(tn, "flash erase_sector 0 1 9", timeout=60)
        print(out)
    
    # Write flash
    print(f"\n=== Writing {BIN_PATH} ===")
    abs_path = BIN_PATH  # Already absolute
    out = telnet_cmd(tn, f"flash write_image {abs_path} 0x08008000", timeout=120)
    print(out)
    
    if "error" not in out.lower():
        # Verify
        print("\n=== Verify ===")
        out = telnet_cmd(tn, f"verify_image {abs_path} 0x08008000", timeout=60)
        print(out)
    
    # Reset and run
    print("\n=== Reset & run ===")
    out = telnet_cmd(tn, "reset run")
    print(out)
    
    tn.close()
    print("\nDone.")
    
    # Cleanup
    proc.terminate()
    proc.wait(timeout=5)

if __name__ == "__main__":
    main()
