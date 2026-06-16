# minc-CPU ユーザーズマニュアル ハードウェア編

## 命令表

| Mnemonic   | Machine code        | Description                                 | c flag  |
| ---------- | ------------------- | ------------------------------------------- | ------- |
| mov rd,rs  | 000000 00 dddd ssss | rd = rs                                     | x       |
| or rd,rs   | 000001 00 dddd ssss | rd = rd \ rs                                | x       |
| and rd,rs  | 000010 00 dddd ssss | rd = rd & rs                                | x       |
| xor rd,rs  | 000011 00 dddd ssss | rd = rd ^ rs                                | x       |
| add rd,rs  | 000100 00 dddd ssss | rd = rd + rs                                | carry   |
| adc rd,rs  | 000101 00 dddd ssss | rd = rd + rs + c                            | carry   |
| sub rd,rs  | 000110 00 dddd ssss | rd = rd - rs                                | !borrow |
| sbc rd,rs  | 000111 00 dddd ssss | rd = rd - rs + c                            | !borrow |
| lt rd,rs   | 001000 00 dddd ssss | rd = 1 if rd - rs < 0, otherwise rd = 0     | !borrow |
| ltc rd,rs  | 001001 00 dddd ssss | rd = 1 if rd + rs + c < 0, otherwise rd = 0 | !borrow |
| rr rd,rs   | 001010 00 dddd ssss | {rd, c} = {c, rs}                           | rs\[0\] |
| mul rd,rs  | 001110 00 dddd ssss | rd = \(rd * rs\)\[7:0\]                     | x       |
| mulh rd,rs | 001111 00 dddd ssss | rd = \(rd * rs\)\[15:8\]                    | x       |
| stf #c     | 010000 00 0000 000c | sets c flag                                 | c       |
| clf #c     | 010001 00 0000 000c | clears c flag                               | c       |
| push rs    | 011100 00 sss0 0000 | (--SP) = rs;(--SP) = rs+1                   |         |
| pop rd     | 011101 00 ddd0 0000 | rd = (SP++); rd+1 = (SP++)                  |         |
| ret        | 011101 00 0001 0000 | PC = (SP);SP = SP + 1;                      |         |
| sts rs     | 011110 00 sss0 0000 | SP = {rs, rs+1}                             |         |
| lds rd     | 011111 00 ddd0 0000 | {rd, rd+1} = SP                             |         |
| stm X+n,rs | 1000 nnnn ssss nnnn | ({r13, r12} + signed'n) = rs                |         |
| ldm rd,X+n | 1001 nnnn dddd nnnn | rd = ({r13, r12} + signed'n)                |         |
| stm Y+n,rs | 1010 nnnn ssss nnnn | ({r15, r14} + signed'n) = rs                |         |
| ldm rd,Y+n | 1011 nnnn dddd nnnn | rd = ({r15, r14} + signed'n)                |         |
| mvi rd,n   | 1100 nnnn dddd nnnn | rd = n                                      |         |
| jz n,rs    | 1101 nnnn ssss nnnn | PC = n if rd == 0                           |         |
| calr rn    | 1110 nnnn rrrr nnnn | (----SP) = PC + 1;PC = PC + {r, n} + 1      |         |
| jr rn      | 1111 nnnn rrrr nnnn | PC = PC + {r, n} + 1                        |         |

| Mnemonic   | Machine code           | Description                                 | c flag  |
|------------|------------------------|---------------------------------------------|---------|
| mov rd,rs  | 00000000 00 dddd ssss  | rd = rs                                     | x       |
| or rd,rs   | 00000001 00 dddd ssss  | rd = rd \ rs                                | x       |
| and rd,rs  | 00000010 00 dddd ssss  | rd = rd & rs                                | x       |
| xor rd,rs  | 00000011 00 dddd ssss  | rd = rd ^ rs                                | x       |
| add rd,rs  | 00000100 00 dddd ssss  | rd = rd + rs                                | carry   |
| adc rd,rs  | 00000101 00 dddd ssss  | rd = rd + rs + c                            | carry   |
| sub rd,rs  | 00000110 00 dddd ssss  | rd = rd - rs                                | !borrow |
| sbc rd,rs  | 00000111 00 dddd ssss  | rd = rd - rs + c                            | !borrow |
| lt rd,rs   | 00001000 00 dddd ssss  | rd = 1 if rd - rs < 0, otherwise rd = 0     | !borrow |
| ltc rd,rs  | 00001001 00 dddd ssss  | rd = 1 if rd + rs + c < 0, otherwise rd = 0 | !borrow |
| rr rd,rs   | 00001011 00 dddd ssss  | {rd, c} = {c, rs}                           | rs[0]   |
|            |                        |                                             |         |
| mul rd,rs  | 000100 00 00 dddd ssss | rd = \(rd * rs\)\[7:0\]                     | x       |
| mulh rd,rs | 000101 00 00 dddd ssss | rd = \(rd * rs\)\[15:8\]                    | x       |
| stf #c     | 001000 00 00 0000 000c | sets c flag                                 | c       |
| clf #c     | 001001 00 00 0000 000c | clears c flag                               | c       |
| jz n,rs    | 001100 nnnn ssss nnnn  | PC = n if rd == 0                           |         |
| mvi rd,n   | 011100 nnnn dddd nnnn  | rd = n                                      |         |
|            |                        |                                             |         |
| stm X+n,rs | 010000 nnnn ssss nnnn  | ({r13, r12} + signed'n) = rs                |         |
| ldm rd,X+n | 010001 nnnn dddd nnnn  | rd = ({r13, r12} + signed'n)                |         |
| stm Y+n,rs | 010010 nnnn ssss nnnn  | ({r15, r14} + signed'n) = rs                |         |
| ldm rd,Y+n | 010011 nnnn dddd nnnn  | rd = ({r15, r14} + signed'n)                |         |
| stm n, rs  | 010100 nnnn ssss nnnn  |                                             |         |
| ldm rd, n  | 010101 nnnn dddd nnnn  |                                             |         |
| push rs    | 011100 00 00 ssss 0000 | (--SP) = rs;(--SP) = rs+1                   |         |
| pop rd     | 011101 00 00 dddd 0000 | rd = (SP++); rd+1 = (SP++)                  |         |
| ret        | 011111 00 00 0000 0000 | PC = (SP);SP = SP + 1;                      |         |
|            |                        |                                             |         |
| calr rn    | 10rrrr nnnn rrrr nnnn  | (----SP) = PC + 1;PC = PC + {r, n} + 1      |         |
| jr rn      | 11rrrr nnnn rrrr nnnn  | PC = PC + {r, n} + 1                        |         |
