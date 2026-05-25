# OpenOCD script: reset run, wait 8s, halt, dump all registers
init
reset run
sleep 8000
halt
puts "=== REGISTERS ==="
reg
puts "=== FAULT STATUS ==="
mdw 0xE000ED2C 1
mdw 0xE000ED28 1
mdw 0xE000ED24 1
puts "=== PC DETAIL ==="
reg pc
reg sp
reg r0
exit
