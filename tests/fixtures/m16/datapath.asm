; minc-16 smoke test: immediates, 16-bit arithmetic, stack, memory
    mvi r15,0          ; SP = 0 -> first push lands at 0xFFFE
    mvi r0,0x12
    mvih r0,0x34       ; r0 = 0x3412
    mvi r1,1
    add r0,r1          ; r0 = 0x3413
    addi r0,-3         ; r0 = 0x3410
    push r0
    pop r2             ; r2 = 0x3410
    mvi r3,0x40        ; byte address 0x40 (word aligned)
    stw [r3+0],r2      ; mem16[0x40] = 0x3410
    ldw r4,[r3+0]      ; r4 = 0x3410
    ldb r5,[r3+0]      ; r5 = 0x0010 (low byte, zero-extended)
    ldb r6,[r3+1]      ; r6 = 0x0034 (high byte)
    push r4
    halt
