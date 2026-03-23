call __on_entry
call main
push r0
halt
__on_entry:
ret
main:
push r15
lds r15
mvi r0,-1
add r0,r15
sts r0
mvi r0,0
push r0
pop r0
stm -1,r0
__L1:
ldm r0,-1
push r0
mvi r0,25
push r0
pop r1
pop r0
lt r0,r1
push r0
pop r0
jz __L2,r0
ldm r0,-1
push r0
mvi r0,1
push r0
pop r1
pop r0
add r0,r1
push r0
pop r0
stm -1,r0
mvi r0,0
jz __L1,r0
__L2:
pop r0
sts r15
pop r15
ret
