file build/rtt_deploy/cuav_v5/rt-thread.elf
target remote localhost:3333
# Read scheduler _initialized
p schedulerInstance._initialized
# Read _timer_thread_ctx and check if it's running
set $tt = (struct rt_thread*)schedulerInstance._timer_thread_ctx
printf "timer thread @ 0x%x sp=0x%x entry=0x%x\n", $tt, $tt->sp, $tt->entry
# Check main thread
set $mt = (struct rt_thread*)schedulerInstance._main_thread_id
printf "main thread @ 0x%x sp=0x%x entry=0x%x\n", $mt, $mt->sp, $mt->entry
# What's the main thread's saved return address?
set $main_sp = $mt->sp
printf "Saved LR at sp+4 = 0x%x\n", *(unsigned long*)($main_sp + 4)
printf "Saved PC at sp+24 = 0x%x\n", *(unsigned long*)($main_sp + 24)
detach
