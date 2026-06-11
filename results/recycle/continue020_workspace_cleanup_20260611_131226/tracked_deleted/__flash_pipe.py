#!/usr/bin/env python3
"""OpenOCD flash programmer — sends commands via pipe/stdin then reads all output."""
import subprocess
import time
import os
import signal

WORKDIR = "/data/firmare/pogo-apm"
BIN_PATH = os.path.join(WORKDIR, "build/rtt_cuav_v5/rtthread.bin")

# Build all commands into a single script for OpenOCD stdin
cmds = f"""
adapter speed 200
transport select hla_swd
init
halt
flash write_image erase {BIN_PATH} 0x08008000
verify_image {BIN_PATH} 0x08008000
reset run
shutdown
"""

proc = subprocess.Popen(
    ["openocd", "-f", "interface/stlink.cfg", "-f", "target/stm32f7x.cfg"],
    stdin=subprocess.PIPE,
    stdout=subprocess.PIPE,
    stderr=subprocess.STDOUT,
    cwd=WORKDIR,
)

out, _ = proc.communicate(input=cmds.encode(), timeout=180)
output = out.decode(errors="replace")

# Extract key lines
for line in output.split("\n"):
    lower = line.lower()
    if any(k in lower for k in ["error", "verified", "wrote", "program", "halted",
                                 "flash size", "wrote", "erased", "timed out",
                                 "reset run", "shutdown"]):
        print(line)

print(f"\nExit code: {proc.returncode}")

# Check if firmware was written successfully (look for verify success)
if "verified" in output.lower():
    print("\n*** FIRMWARE WRITTEN AND VERIFIED SUCCESSFULLY ***")
elif "error" in output.lower():
    print(f"\n*** ERRORS DETECTED ***")
else:
    print(f"\n*** CHECK OUTPUT ABOVE ***")
