mvi r14,0
mvi r15,0
calr __on_entry
calr main
push r1
push r0
halt
__on_entry:
ret
mvi r13,0
mvi r12,4
ldm r2,X+0
mvi r13,0
mvi r12,5
ldm r3,X+0
mvi r13,0
mvi r12,8
ldm r4,X+0
mvi r13,0
mvi r12,9
ldm r5,X+0
mvi r13,0
mvi r12,10
ldm r6,X+0
mvi r13,0
mvi r12,11
ldm r7,X+0
mvi r13,0
mvi r12,12
ldm r8,X+0
mvi r13,0
mvi r12,13
ldm r9,X+0
mvi r13,0
mvi r12,14
ldm r10,X+0
uart_init:
push r15
push r14
ldm r14,0
ldm r15,1
decs 9
mvi r2,255
mvi r13,0
mvi r12,5
stm X+0,r2
mvi r2,3
mvi r13,0
mvi r12,11
stm X+0,r2
stm 0,r14
stm 1,r15
pop r14
pop r15
ret
uart_getch:
push r15
push r14
ldm r14,0
ldm r15,1
decs 9
ldm r2,Y-1
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
stm Y-1,r3
jr __L1
__L2:
mvi r3,14
mvi r13,0
mvi r12,13
stm X+0,r3
ldm r3,Y-1
stm 0,r14
stm 1,r15
pop r14
pop r15
ret
stm 0,r14
stm 1,r15
pop r14
pop r15
ret
uart_putch:
push r15
push r14
ldm r14,0
ldm r15,1
stm Y-1,r2
decs 9
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
ldm r2,Y-1
mvi r13,0
mvi r12,8
stm X+0,r2
stm 0,r14
stm 1,r15
pop r14
pop r15
ret
main:
push r15
push r14
ldm r14,0
ldm r15,1
decs 9
push r1
push r0
push r2
push r3
push r4
push r5
calr uart_init
pop r5
pop r4
pop r3
pop r2
pop r0
pop r1
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
stm Y-1,r2
ldm r2,Y-1
mvi r13,0
mvi r12,4
stm X+0,r2
push r1
push r0
push r2
ldm r2,Y-1
push r3
push r4
push r5
calr uart_putch
pop r5
pop r4
pop r3
pop r2
pop r0
pop r1
__L7:
jr __L5
__L6:
stm 0,r14
stm 1,r15
pop r14
pop r15
ret
