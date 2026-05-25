#!/usr/bin/env python3
"""OpenOCD with stdin piping — attempts two approaches for flash programming."""
import subprocess
import sys
import os

WORKDIR = "/data/firmare/pogo-apm"
BIN_PATH = os.path.join(WORKDIR, "build/rtt_cuav_v5/rtthread.bin")

# Approach 1: init - halt - program (command sequence without reset)
cmds1 = f"""
adapter speed 200
transport select hla_swd
init
halt
program {BIN_PATH} 0x08008000 verify
reset run
shutdown
"""

print("=" * 60)
print("APPROACH 1: init->halt->program->verify")
print("=" * 60)
sys.stdout.flush()

proc = subprocess.Popen(
    ["openocd", "-f", "interface/stlink.cfg", "-f", "target/stm32f7x.cfg"],
    stdin=subprocess.PIPE,
    stdout=subprocess.PIPE,
    stderr=subprocess.STDOUT,
    cwd=WORKDIR,
)
try:
    out, _ = proc.communicate(input=cmds1.encode(), timeout=90)
    output = out.decode(errors="replace")
    print(output)
except subprocess.TimeoutExpired:
    proc.kill()
    out, _ = proc.communicate(timeout=5)
    print(out.decode(errors="replace"))
    print("TIMED OUT")
