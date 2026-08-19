; minc-16: calls, branches, byte lanes, absolute + negative displacement
    mvi r15,0
    calr main
    push r0
    halt

; sum(n in r2) -> r0
sum:
    mvi r0,0
sum_loop:
    add r0,r2
    addi r2,-1
    jnz r2,sum_loop
    ret

main:
    mvi r2,10
    calr sum            ; r0 = 55
    push r0

    mvi r3,0x50
    mvi r4,0xAB
    stb [r3+0],r4       ; low lane
    mvi r5,0xCD
    stb [r3+1],r5       ; high lane
    ldw r6,[r3+0]       ; r6 = 0xCDAB

    mvi r7,0x77
    stw 0x60,r7
    ldw r8,0x60         ; r8 = 0x0077

    mvi r9,0x70
    mvi r10,0x99
    stw [r9-2],r10
    ldw r11,[r9-2]      ; r11 = 0x0099

    mvi r12,5
    mvi r13,7
    lt r12,r13          ; r12 = 1  (5 < 7 unsigned)

    mvi r1,0
    jz r1,skip
    mvi r1,0xEE         ; must be skipped
skip:
    pop r0
    ret
