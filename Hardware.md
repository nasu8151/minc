# minc-CPU ユーザーズマニュアル ハードウェア編

## 命令表

| Mnemonic  | Machine code       | Description        |
| --------- | ------------------ | ------------------ |
| mov rd,rs | 000 0000 dddd ssss | rd = rs            |
| add rd,rs | 000 0001 dddd ssss | rd = rd + rs       |
| sub rd,rs | 000 0010 dddd ssss | rd = rd - rs       |
| mul rd,rs | 000 0011 dddd ssss | rd = rd * rs       |
| push rs   | 000 0100 0000 ssss | (--sp) = rs        |
| lds rs    | 000 0100 0001 ssss | SP = rs            |
| pop rd    | 000 0101 dddd 0000 | rd = (SP++)        |
| sts rd    | 000 0101 dddd 0001 | rd = SP            |
| ret       | 000 0101 0000 0010 | PC = (SP++) + 1    |
| mvi rd,n  | 001 nnnn nnnn dddd | rd = n             |
| stm n,rs  | 010 nnnn nnnn ssss | [r15+n] = rs       |
| ldm rd,n  | 011 nnnn nnnn dddd | rd = [r15+n]       |
| jz n,rs   | 100 nnnn nnnn ssss | PC = n if rs == 0  |
| call n    | 101 nnnn nnnn 0000 | (--sp) = PC;PC = n |
