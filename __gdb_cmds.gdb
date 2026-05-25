target remote localhost:3333
monitor reset halt
monitor flash write_image erase /data/firmare/pogo-apm/build/rtt_cuav_v5/rtthread.bin 0x08008000
monitor verify_image /data/firmare/pogo-apm/build/rtt_cuav_v5/rtthread.bin 0x08008000
monitor reset run
quit
