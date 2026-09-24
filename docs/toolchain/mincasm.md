# mincasm — アセンブラ

```sh
./target/mincasm < prog.asm > prog.hex
```

標準入力から minc アセンブリを読み、1行1命令の 5桁16進数を標準出力へ書きます。

## 構造

単一パス + バックパッチです。

1. 1行ずつ読んでその場で機械語に変換し、ロケーションカウンタの位置に書く
2. 未解決のラベル参照は `Fixup` として記録しておく
3. 全部読み終えてから、記録した `Fixup` をまとめて解決する

このため**前方参照も後方参照も両方書けます**。

ニモニックの定義は `mincasm/main.c` の `g_inst_specs[]` テーブル1つにまとまっています。
各エントリが `InstKind`（`INST_ALU_RR`, `INST_MVI`, `INST_REL16`,
`INST_MEM_STORE` など）を持ち、それがオペランドの解釈方法を決めます。

## 行の書式

```asm
label:              ; ラベルだけの行
        mvi r0,42   ; 命令
loop:   jr loop     ; 同じ行にラベルと命令を書いてもよい
```

- **大文字小文字は区別しません**（ニモニックは小文字化されてから引かれます）。
- オペランドの区切りはカンマまたは空白。どちらでも構いません。
- `;` から行末まではコメント。
- 空行は無視されます。
- 1行は 512 バイトまで。

### ラベル

```
label = [A-Za-z_][A-Za-z0-9_]*
```

行の先頭トークン（空白を含まない部分）の直後に `:` があればラベル定義です。
ラベルは現在のロケーションカウンタを指します。

ラベルを使えるのは `jz` の変位と `calr` / `jr` の変位です。
アセンブラが自動で相対値へ変換します。

## ディレクティブ

| ディレクティブ | 内容 |
| --- | --- |
| `.org <addr>` | ロケーションカウンタを `addr`（0〜65535）に設定 |

`.org` はこれ1つだけです。

- 前方へ飛ばすと、隙間は **`0x00000` でゼロ埋め**されます。
  `0x00000` は `mov r0,r0`（実質 no-op、ただし C フラグは不定）になります。
- 現在位置より**手前**へ戻すこともできます。その場合、既にそこにある語は
  上書きされます。
- 割り込みベクタを置くのに使います（[cpu/interrupts.md](../cpu/interrupts.md)）。
  `mincc` も `[[isr=N]]` のとき内部でこれを使います。

```asm
.org 0x0000
        jr  start
.org 0x0001
        jr  isr0
.org 0x0005
start:
        mvi r0,1
```

## オペランドの書き方

### レジスタ

`r0` 〜 `r15`（`R0`〜`R15` も可）。

### 即値

`strtol(base 0)` で解釈されるので、`42` / `0x2A` / `052` が使えます。
**`0b` 2進表記はアセンブラでは使えません**（C 側の `mincc` では使えます）。

各命令の許容範囲:

| 命令 | 範囲 |
| --- | --- |
| `mvi rd,n` | `-128` 〜 `255` |
| `decs n` | `0` 〜 `255` |
| `jz n,rs` | `-128` 〜 `127`（ラベル可） |
| `calr n` / `jr n` | `-32768` 〜 `32767`（ラベル可） |
| `stm rp+n` / `ldm rp+n` の変位 | `-32` 〜 `31` |
| `stm n` / `ldm n` の絶対アドレス | `-512` 〜 `1023`（10bit にラップ） |

範囲外は `Immediate out of range` エラーになります（黙って切り詰めません）。

### メモリオペランド

```asm
stm X+0, r2        ; X = r12:r13
ldm r2, Y-1        ; Y = r14:r15
stm rp4+3, r2      ; 任意の偶数ペア（r4:r5）
ldm r2, r4+3       ; "rp" は省略可
stm 0x0004, r2     ; 絶対アドレス
ldm r2, 2          ; 絶対アドレス
```

- `X` / `Y` は `r12` / `r14` を基点とするペアの**別名**です。特別扱いではありません。
- ペアの基点は**偶数**でなければなりません
  （`Register pair base must be even`）。
- `+` または `-` が続かない `rN` は、レジスタペアではなく絶対アドレスとして
  解釈されようとしてエラーになります。ペア相対は必ず変位を書いてください
  （`X+0` のように 0 でも明示）。

## 命令一覧

エンコーディングを含む完全な表は [cpu/isa.md](../cpu/isa.md) にあります。
ここではニモニックとオペランドの形だけ挙げます。

```asm
; ALU (rd が第1オペランド兼書き込み先)
mov  rd,rs      or   rd,rs      and  rd,rs      xor  rd,rs
add  rd,rs      adc  rd,rs      sub  rd,rs      sbc  rd,rs
rr   rd,rs      lt   rd,rs      ltc  rd,rs      chz  rd,rs
mul  rd,rs      mulh rd,rs

; 即値
mvi  rd,n       decs n          jz   n,rs

; スタック / 制御
push rs         pop  rd         ret             reti
calr n          jr   n          halt

; メモリ
stm  rp+n,rs    stm  n,rs
ldm  rd,rp+n    ldm  rd,n
```

`halt` は `jr -1`（命令語 `3FFFF`）の別名です。

**`stf` / `clf` は削除されました。** 旧エンコーディングは現在 `mvi` / `decs` に
割り当てられているので、古いアセンブリに残っていると別の命令として実行されます。

## 出力形式

1行に1命令、5桁の大文字16進数。

```
0E000
0F000
20005
3FFFF
```

行番号がそのまま命令アドレスです。`$readmemh` にそのまま食わせます。

## エラー

エラーは行番号と該当行つきで標準エラーに出て、終了コード 1 で止まります。

```
Error: Immediate out of range 'Y-40'
in line 3
"stm Y-40,r2"
```

主なエラー:

| メッセージ | 意味 |
| --- | --- |
| `Unknown instruction` | ニモニックがテーブルにない |
| `Register out of range` | `r0`〜`r15` 以外 |
| `Immediate out of range` | 即値が命令のフィールドに入らない |
| `Register pair base must be even` | `rp3+0` のように奇数を基点にした |
| `Undefined label` | 参照されたラベルが定義されていない |
| `8-bit relative offset out of range` | `jz` の飛び先が ±127 を超えた |
| `16-bit relative offset out of range` | `jr`/`calr` の飛び先が ±32767 を超えた |
| `Invalid label` | ラベル名に使えない文字が入っている |
| `Duplicate mnemonic` | `g_inst_specs[]` の定義が重複（開発時のみ） |

## 手書きするとき

`mincc` を通さず直接アセンブリを書くことはできます。実例は
`tests/fixtures/irq_vector.asm`（割り込みテスト用のハンドラ）や
`example/*.asm`（`mincc` の出力）を見てください。

注意点:

- SP はリセット時 `0x0000` です。スタックを使うなら、そのまま下に伸びる前提で
  よい（`0xFFFF` から下へ）か、`stm 0,rN` / `stm 1,rN` で初期化してください。
- `Y`（`r14:r15`）をフレームポインタとして使うのは `mincc` の規約であって、
  ハードウェアの決まりではありません。手書きなら自由に使えます。
- C フラグが定義される命令は限られています（[cpu/isa.md](../cpu/isa.md)）。
  `mov` を1つ挟むだけで壊れます。
