# minc-CPU ユーザーズマニュアル ハードウェア編

## 命令表

| Mnemonic  | Machine code       | Description                             |
| --------- | ------------------ | --------------------------------------- |
| mov rd,rs | 0000 000 dddd ssss | rd = rs                                 |
| add rd,rs | 0000 001 dddd ssss | rd = rd + rs                            |
| sub rd,rs | 0000 010 dddd ssss | rd = rd - rs                            |
| lt rd,rs  | 0000 011 dddd ssss | rd = 1 if rd - rs < 0, otherwise rd = 0 |
| mul rd,rs | 0000 100 dddd ssss | rd = rd * rs                            |
| or rd,rs  | 0000 101 dddd ssss | rd = rd \| rs                           |
| and rd,rs | 0000 110 dddd ssss | rd = rd & rs                            |
| xor rd,rs | 0000 111 dddd ssss | rd = rd ^ rs                            |
| push rs   | 0001 000 0000 ssss | (--sp) = rs                             |
| sts rs    | 0001 001 0000 ssss | SP = rs                                 |
| pop rd    | 0001 010 dddd 0000 | rd = (SP++)                             |
| lds rd    | 0001 011 dddd 0000 | rd = SP                                 |
| ret       | 000 1100 0000 0000 | PC = (SP++);PC = (SP++)                 |
| mvi rd,n  | 001 nnnn nnnn dddd | rd = n                                  |
| stm n,rs  | 010 nnnn nnnn ssss | [r15+n] = rs                            |
| ldm rd,n  | 011 nnnn nnnn dddd | rd = [r15+n]                            |
| jz n,rs   | 100 nnnn nnnn ssss | PC = n if rs == 0                       |
| call n    | 101 nnnn nnnn rrrr | (--sp) = PC + 1;PC = PC + {r, n} + 1    |
| jnz n,rs  | 110 nnnn nnnn ssss | PC = n if rs != 0                       |

## 予定かも

| Mnemonic  | Machine code       | Description                             |
| --------- | ------------------ | --------------------------------------- |
| mov rd,rs | 000000 00 dddd ssss | rd = rs                                 |
| or rd,rs  | 000001 00 dddd ssss | rd = rd \| rs                           |
| and rd,rs | 000010 00 dddd ssss | rd = rd & rs                            |
| xor rd,rs | 000011 00 dddd ssss | rd = rd ^ rs                            |
| add rd,rs | 000100 00 dddd ssss | rd = rd + rs                            |
| sub rd,rs | 000101 00 dddd ssss | rd = rd - rs                            |
| lt rd,rs  | 000110 00 dddd ssss | rd = 1 if rd - rs < 0, otherwise rd = 0 |
| rr rd,rs  | 000111 00 dddd ssss | 
| mul rd,rs | 001000 00 dddd ssss | rd = (rd * rs)[7:0]                     |
| mulh rd,rs| 001001 00 dddd ssss | rd = (rd * rs)[15:7]                    |
| push rs   | 001010 00 0000 ssss | (--sp) = rs                             |
| pop rd    | 001011 00 dddd 0000 | rd = (SP++)                             |
| sts rs    | 001110 00 0000 ssss | SP = rs                                 |
| lds rd    | 001111 00 dddd 0000 | rd = SP                                 |
| stm X+n,rs| 0100 nnnn nnnn ssss | [{r13, r12}+n] = rs                     |
| ldm rd,X+n| 0101 nnnn nnnn dddd | rd = [{r13, r12}+n]                     |
| stm Y+n,rs| 0110 nnnn nnnn ssss | [{r15, r14}+n] = rs                     |
| ldm rd,Y+n| 0111 nnnn nnnn dddd | rd = [{r15, r14}+n]                     |
| jz n,rs   | 1000 nnnn nnnn ssss | PC = n if rd == 0                       |
| jnz n,rs  | 1001 nnnn nnnn ssss | PC = n if rd != 0                       |
| call n    | 1011 nnnn nnnn rrrr | (----sp) = PC + 1;PC = PC + {r, n}      |
| ret       | 101100 00 0000 0000 | PC = (SP);SP = SP + 2;                  |
| jp m      | 101101 00 0000 0000 | 
| mvi rd,n  | 11mmmm 00 mmmm dddd | rd = m                                  |