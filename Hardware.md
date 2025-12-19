# minc-CPU ユーザーズマニュアル ハードウェア編

## 命令表

| Mnemonic  | Machine code       | Description             |
| --------- | ------------------ | ----------------------- |
| mov rd,rs | 000 0000 dddd ssss | rd = rs                 |
| add rd,rs | 000 0001 dddd ssss | rd = rd + rs            |
| sub rd,rs | 000 0010 dddd ssss | rd = rd - rs            |
| cmp rd,rs | 000 0011 dddd ssss | rd - rs                 |
| mul rd,rs | 000 0100 dddd ssss | rd = rd * rs            |
| push rs   | 000 1000 0000 ssss | (--sp) = rs             |
| lds rs    | 000 1001 0000 ssss | SP = rs                 |
| pop rd    | 000 1010 dddd 0000 | rd = (SP++)             |
| sts rd    | 000 1011 dddd 0000 | rd = SP                 |
| ret       | 000 1100 0000 0000 | PC = (SP++) + 1         |
| mvi rd,n  | 001 nnnn nnnn dddd | rd = n                  |
| stm n,rs  | 010 nnnn nnnn ssss | [r15+n] = rs            |
| ldm rd,n  | 011 nnnn nnnn dddd | rd = [r15+n]            |
| jz n      | 100 nnnn nnnn 0000 | PC = n if Z flag is set |
| jc n      | 100 nnnn nnnn 0001 | PC = n if C flag is set |
| call n    | 101 nnnn nnnn 0000 | (--sp) = PC;PC = n      |
