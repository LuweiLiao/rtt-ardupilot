#!/usr/bin/env python3
"""OpenOCD flash programmer via socket to telnet interface."""
import socket
import time
import os
import subprocess
import signal

WORKDIR = "/data/firmare/pogo-apm"
BIN_PATH = os.path.join(WORKDIR, "build/rtt_cuav_v5/rtthread.bin")

def run_openocd():
    proc = subprocess.Popen(
        ["openocd",
         "-f", "interface/stlink.cfg",
         "-f", "target/stm32f7x.cfg",
         "-c", "adapter speed 200",
         "-c", "transport select hla_swd",
         "-c", "init",
         "-c", "halt"],
        cwd=WORKDIR,
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
    )
    # Wait for telnet port to be ready
    for _ in range(20):
        time.sleep(0.5)
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.settimeout(2)
        try:
            s.connect(("localhost", 4444))
            s.close()
            print("OpenOCD ready on port 4444")
            return proc
        except:
            s.close()
    raise RuntimeError("OpenOCD did not start")

def send_cmd(sock, cmd, timeout=30):
    sock.sendall((cmd + "\n").encode())
    time.sleep(0.3)
    output = b""
    deadline = time.time() + timeout
    while time.time() < deadline:
        try:
            sock.settimeout(0.3)
            data = sock.recv(4096)
            if not data:
                break
            output += data
            if b"> " in output:
                break
        except socket.timeout:
            break
        except:
            break
    return output.decode(errors="replace")

def main():
    proc = run_openocd()
    
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.settimeout(10)
    sock.connect(("localhost", 4444))
    
    # Read greeting
    time.sleep(0.5)
    greeting = send_cmd(sock, "", timeout=1)
    # Don't print full greeting to avoid clutter
    
    # Step 1: Try flash write_image with work-area at end of SRAM (0x20040000)
    print("Step 1: Set work area to SRAM2 (0x20040000)")
    out = send_cmd(sock, "targets", timeout=5)
    print(out)
    
    # Step 2: Try to write flash using program command
    print(f"\nStep 2: Program flash with {BIN_PATH}")
    out = send_cmd(sock, f"flash write_image erase {BIN_PATH} 0x08008000", timeout=120)
    print(out)
    
    if "error" not in out.lower():
        # Step 3: Verify
        print("\nStep 3: Verify")
        out = send_cmd(sock, f"verify_image {BIN_PATH} 0x08008000", timeout=60)
        print(out)
    else:
        # Try sector-by-sector approach
        print("\nFlash write_image failed. Trying manual approach...")
        # Check flash status register
        out = send_cmd(sock, "mdw 0x40023C0C 1", timeout=5)
        print(f"FLASH_SR: {out}")
        
        # Try to unlock and erase
        out = send_cmd(sock, "mww 0x40023C04 0x45670123", timeout=5)
        out = send_cmd(sock, "mww 0x40023C04 0xCDEF89AB", timeout=5)
        print("Unlock done")
        
        # Try erase first
        out = send_cmd(sock, "flash erase_sector 0 1 9", timeout=60)
        print(f"Erase: {out}")
        
        # Try write again
        out = send_cmd(sock, f"flash write_image {BIN_PATH} 0x08008000", timeout=120)
        print(f"Write: {out}")
        
        # Verify
        out = send_cmd(sock, f"verify_image {BIN_PATH} 0x08008000", timeout=60)
        print(f"Verify: {out}")
    
    # Reset and run
    print("\nStep 4: Reset & run")
    out = send_cmd(sock, "reset run", timeout=10)
    print(out)
    
    sock.close()
    proc.terminate()
    proc.wait(timeout=5)
    print("\nDONE")

if __name__ == "__main__":
    main()
