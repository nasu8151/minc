mvi r14,0
mvi r15,0
calr __on_entry
calr main
push r0
halt
__on_entry:
ret
mvi r13,0
mvi r12,0
ldm r2,X+0
mvi r13,0
mvi r12,1
ldm r3,X+0
mvi r13,0
mvi r12,8
ldm r4,X+0
mvi r13,0
mvi r12,9
ldm r5,X+0
mvi r13,0
mvi r12,10
push r6
ldm r6,X+0
mvi r13,0
mvi r12,11
ldm r7,X+0
mvi r13,0
mvi r12,12
push r8
ldm r8,X+0
mvi r13,0
mvi r12,13
ldm r9,X+0
mvi r13,0
mvi r12,14
push r10
ldm r10,X+0
uart_init:
push r14
lds r14
mvi r0,0
mvi r1,0
add r0,r14
adc r1,r15
sts r0
mvi r2,255
mvi r13,0
mvi r12,1
stm X+0,r2
mvi r2,3
mvi r13,0
mvi r12,11
stm X+0,r2
sts r14
pop r14
ret
uart_getch:
push r14
lds r14
mvi r0,255
mvi r1,255
add r0,r14
adc r1,r15
sts r0
ldm r2,-1
__L1:
mvi r13,0
mvi r12,13
ldm r3,X+0
mvi r4,1
and r3,r4
mvi r4,0
or r3,r3
jz __L2,r3
mvi r13,0
mvi r12,8
ldm r3,X+0
stm -1,r3
jr __L1
__L2:
mvi r3,14
mvi r13,0
mvi r12,13
stm X+0,r3
ldm r3,-1
sts r14
pop r14
ret
sts r14
pop r14
ret
uart_putch:
push r14
lds r14
stm -1,r2
mvi r0,255
mvi r1,255
add r0,r14
adc r1,r15
sts r0
__L3:
mvi r13,0
mvi r12,13
ldm r2,X+0
mvi r3,32
and r2,r3
mvi r3,32
xor r2,r3
mvi r3,0
or r2,r2
jz __L4,r2
jr __L3
__L4:
ldm r2,-1
mvi r13,0
mvi r12,8
stm X+0,r2
sts r14
pop r14
ret
main:
push r14
lds r14
mvi r0,255
mvi r1,255
add r0,r14
adc r1,r15
sts r0
push r0
push r2
push r4
calr uart_init
pop r4
pop r2
__L5:
mvi r2,1
mvi r3,0
or r2,r2
jz __L6,r2
mvi r13,0
mvi r12,13
ldm r2,X+0
mvi r3,1
and r2,r3
mvi r3,0
or r2,r2
jz __L7,r2
mvi r13,0
mvi r12,8
ldm r2,X+0
stm -1,r2
ldm r2,-1
mvi r13,0
mvi r12,0
stm X+0,r2
push r0
push r2
ldm r2,-1
push r4
calr uart_putch
pop r4
pop r2
__L7:
jr __L5
__L6:
sts r14
pop r14
ret
