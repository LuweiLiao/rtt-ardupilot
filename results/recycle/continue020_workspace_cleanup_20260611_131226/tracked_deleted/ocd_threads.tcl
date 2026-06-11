# OpenOCD: list RT-Thread threads
init
reset run
sleep 10000
halt
# Print thread list - iterate through rt_thread_defunct list and print names
puts "=== THREAD: current ==="
eval reg 9
puts "=== THREADS ==="
arm semihosting
exit
