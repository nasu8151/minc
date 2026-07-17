# minc-CPU ユーザーズマニュアル ハードウェア編

## 命令表

### minc-8

|  Mnemonic  |      Machine code      |                 Description                 | c flag  |
| ---------- | ---------------------- | ------------------------------------------- | ------- |
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
| mul rd,rs  | 00001110 00 dddd ssss  | rd = \(rd * rs\)\[7:0\]                     | x       |
| mulh rd,rs | 00001111 00 dddd ssss  | rd = \(rd * rs\)\[15:8\]                    | x       |
|            |                        |                                             |         |
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
| push rs    | 011100 00 00 ssss 0000 | (--SP) = rs;                                |         |
| pop rd     | 011101 00 00 dddd 0000 | rd = (SP++);                                |         |
| ret        | 011111 00 00 0000 0000 | PC = (SP++++);                              |         |
| reti       | 011110 00 00 0000 0000 | PC = (SP++++); PSR = PSR_SHADOW             |         |
|            |                        |                                             |         |
| calr rn    | 10rrrr nnnn rrrr nnnn  | (----SP) = PC + 1;PC = PC + {r, n} + 1      |         |
| jr rn      | 11rrrr nnnn rrrr nnnn  | PC = PC + {r, n} + 1                        |         |

## 割り込み

`minc_h.sv` は4本のレベルトリガ割り込み要求線 `irq_in[3:0]`(`irq_in[0]` が最優先、固定優先度、多重割り込み・ネストは非対応・1段のみ)を持つ。命令セット上は新規追加された `reti` 以外に変更はなく、割り込みの有効化・状態確認は既存の `stm`/`ldm` 絶対アドレスモードでメモリマップされたレジスタを読み書きするだけで行う。

- `PSR`(Processor Status Register、データ空間アドレス `0x0002`): bit0 = キャリーフラグ(既存の`c`フラグと同一実体)、bit1 = `IE`(割り込み許可)、bit2-7 は予約(読み出すと常に0)。
- `PSR_SHADOW`(データ空間アドレス `0x0003`): `PSR` の1段自動退避先。割り込み受付時にハードウェアが自動的に現在の`PSR`をここへコピーし、`IE`ビットのみを自動クリアする(キャリーは変更しない)。`reti` はここから`PSR`をまとめて復元する(`ret`との唯一の違い)。
- 割り込みベクタ(命令アドレス空間、PC/ROM側。データ空間の`0x0002`/`0x0003`とは別のバスなので数字が近くても衝突しない): `irq_in[0]→0x0001`、`irq_in[1]→0x0002`、`irq_in[2]→0x0003`、`irq_in[3]→0x0004`。`mincasm`にはORG/絶対アドレス配置が無いため、この4アドレスに到達するには手書きの`.asm`側で先頭に `jr <本処理へ>` を1語置いてベクタスロットを飛び越し、続く4語に `jr <各ISR>` を並べる、という慣習に従う必要がある(`tests/fixtures/irq_vector.asm`参照)。

### 既知の制約

- 汎用レジスタ(r0-r15)の自動退避は無い。ISRが使うレジスタは既存の`push`/`pop`で退避すること。
- `irq_in`に同期化(シンクロナイザ)は無い。CPUと同一クロックドメインである前提。
- レベルトリガ+自動マスクのため、割り込み要因をISR側でクリアする前に`PSR`の`IE`を(`reti`経由であれ`stm`直書きであれ)再び1にすると、即座に再突入するリトリガーループになりうる。
- `minc_p2.sv`/`minc_p5.sv`(パイプライン版)・`gowin/minc/src/minc_gw_top.sv`のUART `INTR`ピン配線は未対応。
