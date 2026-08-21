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

### minc-16

実装は `verilog/minc_16.sv`(モジュール名は `minc16` — バス幅が違うので `minc_tb.sv` にdrop-inできない)、テストベンチは `verilog/minc16_tb.sv` + `verilog/ssram16.sv`。アセンブラは `mincasm -16`。テストは `python3 tests/test_m16.py`(ケースは `tests/fixtures/m16/*.asm`)。

minc-8 とはバイナリ互換ではない別ISA。**mincc はまだ minc-16 を出力しない**ので、現状の利用は手書きアセンブラのみ。設計の背景・トレードオフは「minc-16 設計メモ」節を参照。

#### レジスタとABI

16本の**16bit**汎用レジスタ `r0`-`r15`。minc-8 と違いポインタが1レジスタに収まるので、レジスタペア(X/Y)という概念は**無くなる**。

| レジスタ | 用途 | 強制するもの |
| --- | --- | --- |
| r15 | SP (スタックポインタ) | **ハードウェア**。`push`/`pop`/`calr`/`ret`/`reti`/割り込み受付が暗黙に更新する |
| r14 | BP (フレームポインタ) | ソフトウェア規約のみ |
| r13 | X (汎用ポインタ・スクラッチ) | ソフトウェア規約のみ |
| r0 | 戻り値 | ソフトウェア規約のみ |
| r2- | 引数 / 式評価スタック | ソフトウェア規約のみ |

r0-r5 は caller-saved、r6-r12 は callee-saved(minc-8 の規約を踏襲)。minc-8 の SP はデータ空間 `0x0000`/`0x0001` にメモリマップされた専用レジスタだったが、minc-16 では **r15 そのもの**なので `add r15,rN` / `addi r15,-n` でスタックを直接操作できる。`0x0000`/`0x0001` は解放。

#### メモリモデル

- **バイトアドレッシング**。データ空間は 16bit アドレス = 64KB、バス幅16bit
- ワードアクセスはアドレス下位1bitを無視(ワードアラインのみ)。バイトアクセスは `address[0]` でレーンを選択
- スタックは常にワードアラインで、`push`/`pop`/`calr`/`ret` は SP を **±2** 動かす
- PC は16bitなので**リターンアドレスの push/pop が1メモリサイクル**で済む(minc-8 は上位/下位で2サイクル必要だった)
- 命令空間はハーバード分離のまま。命令語は18bit据え置き(Gowin pROMX9 ×2)

#### 命令フォーマット

```text
[17:16]=00  ALU / 即値 / 絶対アドレス / 制御
   [15:14]=00  ALU     [13:10]=subop  [9:8]=-      [7:4]=rd    [3:0]=rs
   [15:14]=01  即値    [13:12]=subop  [11:8]=n[7:4] [7:4]=rd   [3:0]=n[3:0]
   [15:14]=10  絶対    [13]=ld/st [12]=b/w [11:4]=abs8         [3:0]=data
   [15:14]=11  制御    [13:12]=subop  [11:4]=imm8              [3:0]=rs
[17:16]=01  メモリ(ベース+変位)
            [15]=ld/st [14]=b/w [13:8]=simm6  [7:4]=base       [3:0]=data
[17:16]=10  calr        [15:0]=simm16
[17:16]=11  jr          [15:0]=simm16
```

**ALUのA入力になるレジスタは必ず `[7:4]`(ポートA)側に置く。** これはALUのA入力に16bitマルチプレクサを足さないための制約で、アドレス計算をALUで行う設計の前提になっている(下記「設計メモ」参照)。該当するのは ALU群の `rd`、`addi` の `rd`、メモリ変位群の**ベース**の3つ。

即値群の `imm8` が `{[11:8], [3:0]}` と分割配置されているのはこのため — `addi rd,n` が `rd` を読む以上、`rd` は `[7:4]` になければならない(minc-8 の `mvi rd,n` = `001110 nnnn dddd nnnn` が即値を分割していたのも同じ理由)。

書き戻し先は **ALU群と即値群が `[7:4]`、ロード系と `pop` が `[3:0]`**。これは4bitマルチプレクサ1個で済む。

#### 命令一覧

`d`=デスティネーション/ベース、`s`=ソース、`n`=即値。`c flag` 列の `x` は不定(変化しない扱い)。

|   Mnemonic    |      Machine code       |                  Description                   | c flag  |     |
| ------------- | ----------------------- | ---------------------------------------------- | ------- | --- |
| mov rd,rs     | 00 00 0000 00 dddd ssss | rd = rs                                        | x       |     |
| or rd,rs      | 00 00 0001 00 dddd ssss | rd = rd \                                      | rs      | x   |
| and rd,rs     | 00 00 0010 00 dddd ssss | rd = rd & rs                                   | x       |     |
| xor rd,rs     | 00 00 0011 00 dddd ssss | rd = rd ^ rs                                   | x       |     |
| add rd,rs     | 00 00 0100 00 dddd ssss | rd = rd + rs                                   | carry   |     |
| adc rd,rs     | 00 00 0101 00 dddd ssss | rd = rd + rs + c                               | carry   |     |
| sub rd,rs     | 00 00 0110 00 dddd ssss | rd = rd - rs                                   | !borrow |     |
| sbc rd,rs     | 00 00 0111 00 dddd ssss | rd = rd - rs + c                               | !borrow |     |
| chz rd,rs     | 00 00 1000 00 dddd ssss | rd = (rd == 0) ? 1 : 0                         | x       |     |
| sxb rd,rs     | 00 00 1001 00 dddd ssss | rd = 符号拡張(rs[7:0])                         | x       |     |
| lt rd,rs      | 00 00 1010 00 dddd ssss | rd = (rd < rs) ? 1 : 0 (符号なし16bit)         | !borrow |     |
| ltc rd,rs     | 00 00 1011 00 dddd ssss | rd = (rd - rs - !c < 0) ? 1 : 0                | !borrow |     |
| rr rd,rs      | 00 00 1100 00 dddd ssss | {rd, c} = {c, rs >> 1}                         | rs[0]   |     |
| asr rd,rs     | 00 00 1101 00 dddd ssss | rd = rs >>> 1 (算術シフト)                     | rs[0]   |     |
| mul rd,rs     | 00 00 1110 00 dddd ssss | rd = \(rd * rs\)\[15:0\]                       | x       |     |
| mulh rd,rs    | 00 00 1111 00 dddd ssss | rd = \(rd * rs\)\[31:16\]                      | x       |     |
|               |                         |                                                |         |     |
| mvi rd,n      | 00 01 00 nnnn dddd nnnn | rd = 符号拡張(n8)                              | x       |     |
| mvih rd,n     | 00 01 01 nnnn dddd nnnn | rd[15:8] = n8 (下位は保持)                     | x       |     |
| addi rd,n     | 00 01 10 nnnn dddd nnnn | rd = rd + 符号拡張(n8)                         | carry   |     |
|               |                         |                                                |         |     |
| stw n,rs      | 00 10 0 0 nnnnnnnn ssss | mem16[n8] = rs                                 | x       |     |
| stb n,rs      | 00 10 0 1 nnnnnnnn ssss | mem8[n8] = rs[7:0]                             | x       |     |
| ldw rd,n      | 00 10 1 0 nnnnnnnn dddd | rd = mem16[n8]                                 | x       |     |
| ldb rd,n      | 00 10 1 1 nnnnnnnn dddd | rd = {8'b0, mem8[n8]}                          | x       |     |
|               |                         |                                                |         |     |
| jz rs,n       | 00 11 00 nnnnnnnn ssss  | PC = PC + 符号付きn8 + 1 if rs == 0            | x       |     |
| jnz rs,n      | 00 11 01 nnnnnnnn ssss  | PC = PC + 符号付きn8 + 1 if rs != 0            | x       |     |
| push rs       | 00 11 10 0000 0000 ssss | SP -= 2; mem16[SP] = rs                        | x       |     |
| pop rd        | 00 11 10 0001 0000 dddd | rd = mem16[SP]; SP += 2                        | x       |     |
| ret           | 00 11 10 0010 0000 0000 | PC = mem16[SP]; SP += 2                        | x       |     |
| reti          | 00 11 10 0011 0000 0000 | PC = mem16[SP]; SP += 2; PSR = PSR_SHADOW      | 復元    |     |
|               |                         |                                                |         |     |
| stw [rd+n],rs | 01 0 0 nnnnnn dddd ssss | mem16[rd + 符号付きn6] = rs                    | x       |     |
| stb [rd+n],rs | 01 0 1 nnnnnn dddd ssss | mem8[rd + 符号付きn6] = rs[7:0]                | x       |     |
| ldw rs,[rd+n] | 01 1 0 nnnnnn dddd ssss | rs = mem16[rd + 符号付きn6]                    | x       |     |
| ldb rs,[rd+n] | 01 1 1 nnnnnn dddd ssss | rs = {8'b0, mem8[rd + 符号付きn6]}             | x       |     |
|               |                         |                                                |         |     |
| calr n        | 10 nnnnnnnnnnnnnnnn     | SP -= 2; mem16[SP] = PC + 1; PC = PC + n16 + 1 | x       |     |
| jr n          | 11 nnnnnnnnnnnnnnnn     | PC = PC + n16 + 1                              | x       |     |
| halt          | 11 1111111111111111     | `jr -1`(自己ループ)。`SIM` 定義時は `$finish`  | x       |     |

未使用: ALU subop `0000 1001` 以外の空きは無し(sxb/asr で埋めた)。即値群 `[13:12]=11`、制御群 `[13:12]=11`、制御群 ext `0100`-`1111` が予約。

#### minc-8 からの主な変更点

- レジスタ・ALU・データバスが16bit化。ポインタが1レジスタに収まるので `{r13,r12}`(X) / `{r15,r14}`(Y) のペア表現と、`minc_h.sv` の `reg12`-`reg15` シャドウコピーが不要になる
- SP が専用レジスタ + MMIO(`0x0000`/`0x0001`) から汎用レジスタ r15 へ移動
- アドレッシングモードが X/Y/絶対 の3種から、**汎用ベース+符号付き6bit変位** と **8bit絶対** の2種へ。ベースレジスタは任意
- ロード/ストアにバイト/ワードの幅ビットが付いた(`ldw`/`ldb`/`stw`/`stb`)
- `jnz`/`sxb`/`asr`/`mvih`/`addi` を追加。`jnz` は minc-8 で `chz` + `jz` の2命令だった条件分岐を1命令にする
- `calr`/`jr` の16bit相対オフセットが連続ビット `[15:0]` になった(minc-8 は `{[15:12],[7:4],[11:8],[3:0]}` の分散配置)
- `jz` の被判定レジスタが `[7:4]` から `[3:0]` へ移動。ニモニックのオペランド順も `jz n,rs`(minc-8) から **`jz rs,n`**(minc-16) に入れ替わっている
- メモリオペランドの記法が変わった。ベース+変位は角括弧で `ldw rd,[rb+n]` / `stw [rb+n],rs`(括弧内に空白は置けない)、絶対は `ldw rd,n` / `stw n,rs`
- minc-8 の `stf`/`clf`(mincasm には存在するが `minc_h.sv` では未実装)は廃止。フラグ操作は PSR の MMIO 経由に一本化

## minc-16 設計メモ

仕様策定の根拠。実装時に迷ったらここを参照。

### 加算器を4本から2本に減らす

`minc_h.sv` には加算器が4本ある — ALU(8bit)、AGU(`addr_base + simm8`)、SP(`sp + delta_sp`)、PC(`pc + delta_pc`)。minc-16 では **SPを汎用レジスタに移す**ことと**アドレス計算をALUで行う**ことを組み合わせて、ALU(16bit、データ演算 + アドレス計算 + SP±2)と PC の**2本**まで減らす。

LUT単体で見ると、ALUのB入力マルチプレクサ(16bit 2:1)を足す分と専用AGU加算器を消す分がほぼ相殺するので大きな削減にはならない。効くのは以下:

- SPレジスタ(16FF)と `0x0000`/`0x0001` のMMIOデコード・書き戻し経路が消える
- `reg12`-`reg15` シャドウ(minc-16なら4×16=64FF相当)が消える
- `is_addr_x`/`is_addr_y`/`is_addr_n` の3モードデコードと `addr_base` の3:1マルチプレクサが、ベースレジスタ4bitフィールド1本に置き換わる

**制約**: ベースレジスタを常に `[7:4]`(ポートA)に置くこと。ALU入力を `A=port_a`, `B={port_b, simm}` に固定でき、A側にマルチプレクサが不要になる。ロードの書き戻し先が `[3:0]` になるのは4bitマルチプレクサで済むので誤差。

`pop`/`ret` だけは `address = SP`(生値)と `SP+2`(ALU出力)を同時に必要とするので、`address` に `{alu_out, port_a_val}` の2:1マルチプレクサが要る。

### 【重要】割り込み受付は全てのデコードより優先させること

ALUを共有したことで minc-8 には無かったハザードが1つ生まれる。**実装時に踏んだので明記しておく。**

割り込み受付の擬似命令は `S_FETCH` で `instr <= cur` と `servicing_irq <= take_irq` が同じエッジで起きるため、`S_DECEXEC` 以降は **`instr` に「これから遅延させる命令」が入ったまま** SP -= 2 とPCのpushを実行する。したがって `instr` をデコードした信号は、割り込み受付シーケンスにとって全部ノイズになる。

minc-8 では SP に専用加算器(`delta_sp`)があり、その先頭が `servicing_irq ? -1 : ...` だったので自動的に守られていた。minc-16 は同じALUを使い回すので、**以下すべてで `servicing_irq` を最優先にしなければならない**:

| 信号 | `servicing_irq` を優先しないとどうなるか |
| --- | --- |
| `alu_op` | 遅延命令がALU命令だと、ADDではなくその命令のsubop(`lt` 等)でSPが計算される |
| `alu_b` | 遅延命令が `is_disp` / `addi` だと、-2 ではなく変位や即値が加算される |
| `address` | 遅延命令が abs/disp だと、SPではなくそのアドレスへPCが書かれる |
| `we` | 遅延命令が `stb` だと、ワードpushのはずが片バイトレーンしか書かれない |

`avma`、`data_out`、レジスタ書き戻し、PSR更新、PC更新は元から `servicing_irq` を先に見ているので追加対応は不要。

実際に踏んだ症状は「割り込みを2回目以降に受けるとSPが `0x0000 → 0x0001` になる」というもので、遅延命令が `lt r7,r6` だったため `alu_op` が LT になり `alu_out` が比較結果(0/1)になっていた。その後 SP がRAM範囲外(`ce` が下がる)を指して `ret` が `z` を読み、PCが不定になって停止する。

### なぜワードアドレッシングではなくバイトアドレッシングか

ワードアドレッシング(1アドレス=1ワード)ならバイトレーンもアラインメントも不要でLUT的には安いが、

- 文字列リテラルが1文字あたり2バイトを消費する
- `sizeof(char) == sizeof(int) == 1` になり C のイディオムを広く壊す

バイトアドレッシングの追加コストは実測ベースで小さい:

- 読みのバイトレーン選択 = 8bit 2:1マルチプレクサ ≈ 8 LUT
- 書きはバイトを両レーンに複製 + ライトストローブ2本 ≈ 8-10 LUT
- Gowin側に特別なバイトイネーブルプリミティブは不要。8bit幅のSPブロックを2個 `wre` 独立で並べるだけ(現状すでに `BIT_WIDTH=4` ×2構成なので素直な再コンフィグ)

重要なのは、**バイトアドレッシングにしてもレジスタペア演算は復活しない**こと。レジスタ/ALUは16bitのままなので mincc 側の `gen_i8`/`gen_i16` の add/adc・sub/sbc・lt/ltc ペア展開と `cast_i8_to_i16`/`cast_i16_to_i8` は予定通り全て不要になり、バイト幅は**ロード/ストアの幅ビット1本**としてのみ残る。

実際に払う代償は変位フィールドで、幅ビットに1bit取られた結果 `simm6`(±32バイト)しか残らない。

### 変位±32バイトの使い切り方

mincc はローカル変数を **BP(minc-8ではY=r14:r15)相対の負オフセット**でアクセスする([codegen.c:250-254](mincc/codegen.c#L250-L254) / [codegen.c:320-324](mincc/codegen.c#L320-L324))。この配置のままだと `simm6` の負側32バイト = ワード変数16個分しか使えず、正側32バイトが丸ごと遊ぶ。

**BPをフレームの中央に置けば** `BP-32` から `BP+31` の64バイト全域が使えてワード変数32個分になる。プロローグでBPを作るときに定数を足すだけなのでISA側のコストはゼロ。フレームが64バイトを超える関数だけ `addi` でBPを振り直すか、ベースを別レジスタに作る。

さらに足りない場合の選択肢(今は採用しない):

1. レジスタを8本にして3bitフィールドにする → `2+1+1+8+3+3=18` で変位8bit(±128バイト)。ただし mincc の式評価スタックが r2 から上を使う設計なので深い式でスピルしやすくなる
2. BP相対を暗黙ベースの専用形式にする(Thumbのやり方)。ベースフィールドが不要になり10bit変位が取れる
3. 命令語を18bitより広げる(pROMX9 ×2 の境界を捨てる)

### 乗算器とDSP

`impl/pnr/minc.rpt.txt` によれば minc-8 の 8×8 乗算はすでに DSP 0.25/10 を使っている。GW1NR-9C には DSP が10個あるので 16×16 化しても DSP 1個程度で収まり、LUT の問題にはならない。DSP を持たないデバイスに載せる場合のみ `mul`/`mulh` をソフトウェアルーチンに落とすことを再検討する。

### 現状の実測リソース(minc-8 @ GW1NR-9C)

minc-16 の増減を測るときのベースライン。`gowin/minc/impl/pnr/minc.rpt.txt` より。

| リソース | 使用 | 全体 |
| --- | --- | --- |
| LUT,ALU | 513 (432 LUT + 81 ALU) | — |
| Register | 83 | 6480 |
| BSRAM | 3 (うち pROM 1) | 26 (18Kbit×26 = 468Kbit) |
| DSP | 0.25 | 10 |

データRAMは `Gowin_SP` が `ad[11:0]` = 4096×8bit = 4KB で BSRAM 2ブロック。BSRAM は23ブロック(≈51KB分)空いており、**このデバイス上ではデータRAMは逼迫していない**。タイトなのは LUT(513、目標~400)と命令ROM(pROM 1ブロック)。

### 未解決

- **文字列リテラル / 初期化済みデータの置き場が無い**。mincasm のディレクティブは `.org` だけ([mincasm/main.c:540](mincasm/main.c#L540))、グローバル変数はバンプポインタでアドレスを配るだけ([ast.c:111](mincc/ast.c#L111))、初期化子は `__on_entry` 内のランタイムストアに展開される([ast.c:116](mincc/ast.c#L116))。このまま文字列を載せると ROM 36bit/文字かかる。ROM常駐のread-onlyデータ経路(18bitワードに2バイト詰めれば9bit/文字)が必要で、`S_MA` では命令ROMポートが空いているので2ポート化せずに AVR の `LPM` 相当を実装できるはず
- **mincc が minc-16 を出力できない**。現状 minc-16 のテストは全部手書きアセンブラ(`tests/fixtures/m16/`)。ABI(BP中央配置を含む)とサイズ非依存化した codegen が必要
- パイプライン版(`minc_p2.sv`/`minc_p5.sv`)を minc-16 に追従させるか
- 合成してリソースを実測していない。minc-8 の 513 LUT / 83 FF / DSP 0.25 に対してどう動くかは未確認

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
