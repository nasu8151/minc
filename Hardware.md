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
| chz rd,rs  | 00001000 00 dddd ssss  | rd = (rs == 0) ? 1 : 0                      | x       |
| lt rd,rs   | 00001010 00 dddd ssss  | rd = 1 if rd - rs < 0, otherwise rd = 0     | !borrow |
| ltc rd,rs  | 00001011 00 dddd ssss  | rd = 1 if rd + rs + c < 0, otherwise rd = 0 | !borrow |
| rr rd,rs   | 00001100 00 dddd ssss  | {rd, c} = {c, rs}                           | rs[0]   |
| mul rd,rs  | 00001110 00 dddd ssss  | rd = \(rd * rs\)\[7:0\]                     | x       |
| mulh rd,rs | 00001111 00 dddd ssss  | rd = \(rd * rs\)\[15:8\]                    | x       |
|            |                        |                                             |         |
| jz n,rs    | 001100 nnnn ssss nnnn  | PC = n if rd == 0                           |         |
| mvi rd,n   | 001110 nnnn dddd nnnn  | rd = n                                      |         |
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

| Mnemonic    | Machine code            | Description                                 | c flag  |
|-------------|-------------------------|---------------------------------------------|---------|
| mov rd,rs   | 00000000 00 dddd ssss   | rd = rs                                     | x       |
| or rd,rs    | 00000001 00 dddd ssss   | rd = rd \ rs                                | x       |
| and rd,rs   | 00000010 00 dddd ssss   | rd = rd & rs                                | x       |
| xor rd,rs   | 00000011 00 dddd ssss   | rd = rd ^ rs                                | x       |
| add rd,rs   | 00000100 00 dddd ssss   | rd = rd + rs                                | carry   |
| adc rd,rs   | 00000101 00 dddd ssss   | rd = rd + rs + c                            | carry   |
| sub rd,rs   | 00000110 00 dddd ssss   | rd = rd - rs                                | !borrow |
| sbc rd,rs   | 00000111 00 dddd ssss   | rd = rd - rs + c                            | !borrow |
| chz rd,rs   | 00001000 00 dddd ssss   | rd = (rs == 0) ? 1 : 0                      | x       |
| lt rd,rs    | 00001010 00 dddd ssss   | rd = 1 if rd - rs < 0, otherwise rd = 0     | !borrow |
| ltc rd,rs   | 00001011 00 dddd ssss   | rd = 1 if rd + rs + c < 0, otherwise rd = 0 | !borrow |
| rr rd,rs    | 00001100 00 dddd ssss   | {rd, c} = {c, rs}                           | rs[0]   |
| mul rd,rs   | 00001110 00 dddd ssss   | rd = \(rd * rs\)\[7:0\]                     | x       |
| mulh rd,rs  | 00001111 00 dddd ssss   | rd = \(rd * rs\)\[15:8\]                    | x       |
|             |                         |                                             |         |
| jz n,rs     | 001000 nnnn ssss nnnn   | PC = PC + n if rd == 0                      |         |
| ret rd      | 001100 0000 dddd 0000                 |                                             |         |
| reti rd      | 001101 0000 dddd 0000                 |                                             |         |
| push rs     | 001011 0000 ssss 0000                  |                                             |         |
| pop rd      | 001111 0000 dddd 0000                 |                                             |         |
|             |                         |                                             |         |
| mvi rd,n    | 001101 nnnn dddd nnnn   | rd = n                                      |         |
| adi rd,n    | 001100 nnnn dddd nnnn   | rd = rd + n                                 |         |
| adic rd,n   | 001110 nnnn dddd nnnn   | rd = rd + n + c                             |         |
|             |                         |                                             |         |
| stm rp+n,rs | 01 00 nn nnnn ssss ppp0 | (rp + signed'n) = rs                        |         |
| ldm rd,rp+n | 01 01 nn nnnn dddd ppp0 | rd = (rp + signed'n)                        |         |
| stm n, rs   | 01 10 nn nnnn ssss mmmm | ({m, n}) = rs                               |         |
| ldm rd, n   | 01 11 nn nnnn dddd mmmm | rd = ({m, n})                               |         |
|             |                         |                                             |         |
| calr rn     | 10 rrrr nnnn rrrr nnnn  | (----SP) = PC + 1;PC = PC + {r, n} + 1      |         |
| jr rn       | 11 rrrr nnnn rrrr nnnn  | PC = PC + {r, n} + 1                        |         |
| halt        | 11 1111 1111 1111 1111  | stops the CPU. (equiv. with `jr -1`)        |         |

- `rp` means register pair. (e.g. rp14 means {r15, r14})

## 割り込み

`minc_h.sv` は4本のレベルトリガ割り込み要求線 `irq_in[3:0]`(`irq_in[0]` が最優先、固定優先度、多重割り込み・ネストは非対応・1段のみ)を持つ。命令セット上は新規追加された `reti` 以外に変更はなく、割り込みの有効化・状態確認は既存の `stm`/`ldm` 絶対アドレスモードでメモリマップされたレジスタを読み書きするだけで行う。

- `PSR`(Processor Status Register、データ空間アドレス `0x0002`): bit0 = キャリーフラグ(既存の`c`フラグと同一実体)、bit1 = `IE`(割り込み許可)、bit2-7 は予約(読み出すと常に0)。
- `PSR_SHADOW`(データ空間アドレス `0x0003`): `PSR` の1段自動退避先。割り込み受付時にハードウェアが自動的に現在の`PSR`をここへコピーし、`IE`ビットのみを自動クリアする(キャリーは変更しない)。`reti` はここから`PSR`をまとめて復元する(`ret`との唯一の違い)。
- 割り込みベクタ(命令アドレス空間、PC/ROM側。データ空間の`0x0002`/`0x0003`とは別のバスなので数字が近くても衝突しない): `irq_in[0]→0x0001`、`irq_in[1]→0x0002`、`irq_in[2]→0x0003`、`irq_in[3]→0x0004`。`mincasm`は`.org <addr>`ディレクティブで命令のアドレス(ロケーションカウンタ)を指定できるので、手書きの`.asm`側で `.org 0x0000`/`.org 0x0001`...と各アドレスに `jr <本処理へ>` / `jr <各ISR>` を配置すればこの4アドレスに到達できる(`tests/fixtures/irq_vector.asm`参照)。

### mincc からの利用: `[[isr]]` / `[[isr=N]]`

**割り込みはリセット直後は禁止されている**(`PSR`は`2'b00`にリセットされる)。`[[isr=N]]`を書いてもcrt0は`IE`を触らないので、割り込みを使うプログラムは自分で`sei()`(下記)を呼ぶ必要がある。実例は`example/blink.c`を参照。

`mincc`は関数定義に`[[isr]]`(または`[[isr=N]]`、`N`は0-3で`irq_in[N]`に対応)属性を付けることでISR関数をサポートする。`[[isr=N]]`を付けた関数は`mincasm`の`.org`を使ってmincc自身がベクタ`N`(命令アドレス`0x0001+N`)に自動配置する(この場合crt0は`.org 0x0005`以降にずれる。`[[isr=N]]`を使わないプログラムの出力は従来通り変化しない)。`N`を省略した`[[isr]]`単体では自動配置は行われず、`tests/fixtures/irq_vector.asm`のように手書き`.asm`側で`jr`を配置して呼び出す形になる。未使用のベクタスロットには安全なダミーとして`reti`が自動的に置かれる(スプリアスな`irq_in`パルスでも正しくレジューム復帰できる)。

ISR関数は戻り値・引数を持てず(`return式;`はコンパイルエラー、パラメータ付きの宣言もコンパイルエラー)、`calr`で直接呼び出すこともできない(`reti`で終わるため、通常の呼び出し規約で`calr`すると`PSR`が壊れる)。ISR本体が実際に使用するレジスタ(r0-r13、Xポインタのr12:r13含む)は下記の「汎用レジスタの自動退避は無い」という制約を踏まえてコンパイラが自動的に`push`/`pop`で退避・復元するため、手書き`.asm`のISRのように書き手が個々に気をつける必要はない。

### mincc からの利用: `sei()` / `cli()`

割り込みの許可・禁止(`PSR`の`IE`ビット)は、`mincc`の組み込み関数 `sei()` / `cli()` で行える(avr-libcと同じ名前)。`PSR`は`stm`/`ldm`の絶対アドレスモードでしか触れないので、どちらもリード・モディファイ・ライトに展開される。

```c
sei();  // ldm r0,2 / mvi r1,2   / or  r0,r1 / stm 2,r0   -- IE(bit1)を立てる
cli();  // ldm r0,2 / mvi r1,253 / and r0,r1 / stm 2,r0   -- IE(bit1)を落とす
```

`[[address=0x02]] psr`を宣言して`psr = 2;`と直接書く従来の方法と違い、読んだ値をそのまま書き戻すので`PSR`のbit0(キャリーフラグ)が保存される。使用するr0/r1はどの文の境界でも死んでいる(ISR内ではプロローグが無条件に退避する)ため、呼び出し側で退避する必要はない。

これらは**名前が他に宣言されていないときだけ**組み込みとして解釈される。`char sei = 5;`のように同名の変数・関数を宣言すればそちらが優先されるので、既存コードを壊さない。

### mincc からの利用: インラインアセンブラ `asm("...")`

`asm("...");`文で、アセンブリを`mincasm`へそのまま(verbatim)流し込める。C標準と同じく隣接する文字列リテラルは連結され、エスケープは`\n` `\t` `\r` `\\` `\"` `\'`をサポートする(`\0`は非対応 — 内部でNUL終端文字列として扱うため)。`mincasm`は行指向なので、複数命令を書くときは`\n`で区切ること(末尾に`\n`が無い場合はコンパイラが補う)。

```c
char [[address=0x05]] PORTA_DIR;

char main() {
    PORTA_DIR = 0xFF;
    asm("mvi r0,0x5A\n"
        "stm 4,r0\n");   // PORTA(0x04)へ直接書き込む
    return 0;
}
```

ラベルや`;`コメントも`mincasm`にそのまま渡るので使える。ただし**オペランドの結び付け(GCCの`asm(... : "=r"(x) ...)`相当)は無い** — C変数を名前で参照することはできないので、値の受け渡しには`[[address=N]]`付きのグローバル変数を使う。レジスタの保全も書き手の責任だが、r0/r1は文の境界では常に空いているので自由に使ってよい(`sei()`/`cli()`もこれを利用している)。

`asm(...)`は文であって式ではないため、`char x = asm("...");`のように値として使うことはできない(`sei()`/`cli()`も同様)。

### 既知の制約

- 汎用レジスタ(r0-r15)の自動退避は無い。手書き`.asm`でISRを書く場合は使うレジスタを既存の`push`/`pop`で退避すること(`mincc`の`[[isr]]`はこれを自動で行う — 上記参照)。
- `stf`/`clf`(フラグ操作命令、op6=`001000`/`001001`)は`mincasm`のニモニック表には存在するが、`minc_h.sv`ではデコーダがコメントアウトされており**実装されていない**(`minc_p2.sv`/`minc_p5.sv`も同様)。アセンブルは通るが実行しても何も起こらない(`rw_next`が`ra_val`にフォールスルーし、`rd`へ自分自身を書き戻すだけのno-opになる)ので使わないこと。フラグ操作は`PSR`(`0x0002`)への`stm`/`ldm`、または`mincc`の`sei()`/`cli()`で行う。
- `irq_in`に同期化(シンクロナイザ)は無い。CPUと同一クロックドメインである前提。
- レベルトリガ+自動マスクのため、割り込み要因をISR側でクリアする前に`PSR`の`IE`を(`reti`経由であれ`stm`直書きであれ)再び1にすると、即座に再突入するリトリガーループになりうる。
- `minc_p2.sv`/`minc_p5.sv`(パイプライン版)・`gowin/minc/src/minc_gw_top.sv`のUART `INTR`ピン配線は未対応。
