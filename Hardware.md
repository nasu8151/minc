# minc-CPU ユーザーズマニュアル ハードウェア編

## 命令表

| Mnemonic  | Machine code       | Description                             |
| --------- | ------------------ | --------------------------------------- |
| mov rd,rs | 0000 000 dddd ssss | rd = rs                                 |
| add rd,rs | 0000 001 dddd ssss | rd = rd + rs                            |
| sub rd,rs | 0000 010 dddd ssss | rd = rd - rs                            |
| lt rd,rs  | 0000 011 dddd ssss | rd = 1 if rd - rs < 0, otherwise rd = 0 |
| mul rd,rs | 0000 100 dddd ssss | rd = rd * rs                            |
| or rd,rs  | 0000 101 dddd ssss | rd = rd \ rs                            |
| and rd,rs | 0000 110 dddd ssss | rd = rd & rs                            |
| xor rd,rs | 0000 111 dddd ssss | rd = rd ^ rs                            |
| push rs   | 0001 000 0000 ssss | (--sp) = rs                             |
| sts rs    | 0001 001 0000 ssss | SP = rs                                 |
| pop rd    | 0001 010 dddd 0000 | rd = (SP++)                             |
| lds rd    | 0001 011 dddd 0000 | rd = SP                                 |
| ret       | 0001 111 0000 0000 | PC = (SP++);PC = (SP++)                 |
| mvi rd,n  | 001 nnnn dddd nnnn | rd = n                                  |
| stm n,rs  | 010 nnnn ssss nnnn | [r15+n] = rs                            |
| ldm rd,n  | 011 nnnn dddd nnnn | rd = [r15+n]                            |
| jz n,rs   | 100 nnnn ssss nnnn | PC = PC + n + 1 if rs == 0              |
| call n    | 101 nnnn rrrr nnnn | (--sp) = PC + 1;PC = PC + {r, n} + 1    |
| jnz n,rs  | 110 nnnn ssss nnnn | PC = PC + n + 1 if rs != 0              |
| jr n      | 111 nnnn rrrr nnnn | PC = PC + {r, n} + 1                    |

## 予定かも

| Mnemonic     | Machine code        | Description                             |
| ------------ | ------------------- | --------------------------------------- |
| mov rd,rs    | 000000 00 dddd ssss | rd = rs                                 |
| or rd,rs     | 000001 00 dddd ssss | rd = rd \ rs                            |
| and rd,rs    | 000010 00 dddd ssss | rd = rd & rs                            |
| xor rd,rs    | 000011 00 dddd ssss | rd = rd ^ rs                            |
| add rd,rs    | 000100 00 dddd ssss | rd = rd + rs                            |
| adc rd,rs    | 000101 00 dddd ssss | rd = rd + rs + c                        |
| sub rd,rs    | 000110 00 dddd ssss | rd = rd - rs                            |
| sbc rd,rs    | 000111 00 dddd ssss | rd = rd - rs + c                        |
| lt rd,rs     | 001000 00 dddd ssss | rd = 1 if rd < rs, otherwise rd = 0     |
| ltc rd,rs    | 001000 00 dddd ssss | rd = 1 if rd < rs - c, otherwise rd = 0 |
| rr rd,rs     | 001010 00 dddd ssss | rshift with carry                       |
| mul rd,rs    | 010000 00 dddd ssss | rd = \(rd * rs\)\[7:0\]                 |
| mulh rd,rs   | 010001 00 dddd ssss | rd = \(rd * rs\)\[15:7\]                |
| stf \[c\\z\] | 011000 00 0000 00ic |                                         |
| clf \[c\\z\] | 011001 00 0000 00ic |                                         |
| push rs      | 011010 00 0000 sss0 | (--SP) = rs                             |
| (call m)     | 011010 00 0001 0000 | (--SP) = PC + 1;PC = m                  |
| pop rd       | 011011 00 ddd0 0000 | rd = (SP++)                             |
| ret          | 011011 00 0001 0000 | PC = (SP);SP = SP + 1;                  |
| sts rs       | 011110 00 0000 sss0 | SP = rs                                 |
| (jp m)       | 011110 00 0001 0000 | PC = m                                  |
| lds rd       | 011111 00 ddd0 0000 | rd = SP                                 |
| stm X+n,rs   | 1000 nnnn ssss nnnn | [{r13, r12}+signed'n] = rs              |
| ldm rd,X+n   | 1001 nnnn dddd nnnn | rd = [{r13, r12}+signed'n]              |
| stm Y+n,rs   | 1010 nnnn ssss nnnn | [{r15, r14}+signed'n] = rs              |
| ldm rd,Y+n   | 1011 nnnn dddd nnnn | rd = [{r15, r14}+signed'n]              |
| mvi rd,n     | 1100 nnnn dddd nnnn | rd = n                                  |
| jz n,rs      | 1101 nnnn ssss nnnn | PC = n if rd == 0                       |
| calr rn      | 1110 rnnn rrrr nnnn | (----SP) = PC + 1;PC = PC + {r, n} + 1  |
| jr rn        | 1111 rnnn rrrr nnnn | (----SP) = PC + 1;PC = PC + {r, n} + 1  |
