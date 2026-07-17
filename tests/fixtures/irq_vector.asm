; mincasm has no ORG/fixed-address directive, so the "vector table" here is
; just a convention this file follows by hand: word0 jumps over the 4 fixed
; vector words (IRQ_VECTOR = 0x0001..0x0004 in minc_h.sv), words 1-4 are the
; vectors themselves.
;
; Tests: the interrupt fires mid-padding, the correct ISR (by vector number)
; runs, execution resumes exactly where it was interrupted, and RETI restores
; PSR from the shadow (0x0003) even though the ISR deliberately clobbers PSR
; to 0 before returning -- proving RETI actually restores rather than just
; leaving PSR untouched.
jr MAIN         ; word 0
jr ISR0         ; word 1 == IRQ_VECTOR for irq_in[0]
jr ISR1         ; word 2 == IRQ_VECTOR for irq_in[1]
jr ISR2         ; word 3 == IRQ_VECTOR for irq_in[2]
jr ISR3         ; word 4 == IRQ_VECTOR for irq_in[3]
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
