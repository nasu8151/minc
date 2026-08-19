; minc-16 one-shot interrupt: exactly one ISR entry, then resume
.org 0x0000
    jr start
.org 0x0001
    jr isr0
.org 0x0002
    reti
.org 0x0003
    reti
.org 0x0004
    reti

.org 0x0005
isr0:
    push r1
    mvi r1,1
    add r5,r1
    pop r1
    reti

start:
    mvi r15,0
    mvi r5,0
    mvi r4,0x5A        ; must survive the ISR untouched
    mvi r0,2
    stb 2,r0           ; IE = 1
wait:
    jz r5,wait         ; spin until the ISR fires
    mvi r3,0
    ldb r3,2           ; read PSR back: IE must still be 1 after reti
    push r5
    halt
