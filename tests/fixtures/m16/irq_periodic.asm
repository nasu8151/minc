; minc-16 periodic interrupt: proves `reti` re-arms IE (the ISR is entered
; repeatedly off one re-pulsing line), and that writing PSR_SHADOW (byte 0x0003)
; changes what `reti` restores -- the ISR disarms itself on the 3rd entry, so
; the final count is deterministic rather than racing the main loop.
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
    push r2
    mvi r1,1
    add r5,r1          ; bump the shared counter
    mov r2,r5
    mvi r1,3
    lt r2,r1           ; r2 = (r5 < 3)
    jnz r2,isr_done    ; not there yet -> leave IE armed for the next pulse
    mvi r1,0
    stb 3,r1           ; PSR_SHADOW = 0 -> reti leaves interrupts disabled
isr_done:
    pop r2
    pop r1
    reti

start:
    mvi r15,0
    mvi r5,0
    mvi r6,3
    mvi r0,2
    stb 2,r0           ; PSR = 0b10 -> IE = 1
wait:
    mov r7,r5
    lt r7,r6           ; r7 = (r5 < 3)
    jnz r7,wait
    push r5
    halt
