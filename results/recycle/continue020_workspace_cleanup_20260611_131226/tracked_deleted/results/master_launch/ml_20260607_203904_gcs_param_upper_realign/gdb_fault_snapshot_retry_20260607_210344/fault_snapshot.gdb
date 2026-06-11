target extended-remote :3333
monitor halt
info registers pc msp psp lr
x/4wx 0xE000ED28
x/1wx 0xE000ED2C
bt
monitor resume
detach
quit
