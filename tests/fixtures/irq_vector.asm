; Vector table pinned to its fixed addresses (IRQ_VECTOR = 0x0001..0x0004 in
; minc_h.sv) via .org, rather than relying on word-count convention.
;
; Tests: the interrupt fires mid-padding, the correct ISR (by vector number)
; runs, execution resumes exactly where it was interrupted, and RETI restores
; PSR from the shadow (0x0003) even though the ISR deliberately clobbers PSR
; to 0 before returning -- proving RETI actually restores rather than just
; leaving PSR untouched.
.org 0x0000
jr MAIN         ; reset vector
.org 0x0001
jr ISR0         ; IRQ_VECTOR for irq_in[0]
.org 0x0002
jr ISR1         ; IRQ_VECTOR for irq_in[1]
.org 0x0003
jr ISR2         ; IRQ_VECTOR for irq_in[2]
.org 0x0004
jr ISR3         ; IRQ_VECTOR for irq_in[3]
.org 0x0005
MAIN:
    mvi r1,3        ; PSR = 0b11 (IE=1, carry=1)
    stm 2,r1
    mvi r4,0
    mvi r4,0
    mvi r4,0
    mvi r4,0
    mvi r4,0
    mvi r4,0
    mvi r4,0
    mvi r4,0
    mvi r4,0
    mvi r4,0
    ldm r8,2        ; PSR readback after the interrupt has come and gone
    push r9         ; TOP high byte = ISR sentinel
    push r8         ; TOP low byte  = PSR readback (expect 0b11, restored by RETI)
    halt
ISR0:
    mvi r9,0xA0
    mvi r10,0
    stm 2,r10       ; deliberately clobber PSR; RETI must overwrite this from the shadow
    reti
ISR1:
    mvi r9,0xA1
    mvi r10,0
    stm 2,r10
    reti
ISR2:
    mvi r9,0xA2
    mvi r10,0
    stm 2,r10
    reti
ISR3:
    mvi r9,0xA3
    mvi r10,0
    stm 2,r10
    reti
