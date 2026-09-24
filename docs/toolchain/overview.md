# ツールチェーン概要

```
   .c  ──[ mincc ]──▶  .asm  ──[ mincasm ]──▶  .hex
                                                 │
                              ┌──────────────────┴──────────────────┐
                              ▼                                     ▼
                   iverilog + vvp（シミュレーション）      Gowin IDE（FPGA）
                   verilog/test.hex                        verilog/program.hex
```

両方とも **標準入力から読み、標準出力へ書く** 単純なフィルタです。
コマンドライン引数もオプションもありません。

## ビルド

```sh
make            # target/mincc, target/mincasm を作る
make clean      # target/ と *.o を消す
make test       # clean + build + tests/test.py
```

必要なもの:

| ツール | 用途 |
|---|---|
| `gcc` | `mincc` / `mincasm` のビルド（C99） |
| `iverilog` / `vvp` | シミュレーション・テスト |
| `python3` | テストランナー |
| Gowin FPGA Designer | FPGA 向けの合成（実機で動かす場合のみ） |

Windows では msys2 + mingw-w64 を推奨します（`target/mincc.exe` が生成されますが、
テストスクリプトはそのまま動きます）。

## 手で1本流す

```sh
./target/mincc   < example/demo.c  > out.asm
./target/mincasm < out.asm         > verilog/program.hex

cd verilog
iverilog -o sim.out minc_h.sv minc_tb.sv -g2012 -DVERBOSE -DSIM
vvp sim.out
```

`mincc` は**標準エラー出力に大量のデバッグ情報**（トークン、変数の割り当て、
AST のダンプなど）を出します。標準出力にはアセンブリしか出ないので、
リダイレクトで分けてください。

```sh
./target/mincc < prog.c > prog.asm 2>/dev/null
```

## 中間形式

### アセンブリ（`.asm`）

行指向のテキストです。`mincasm` の入力形式は [mincasm.md](mincasm.md) を参照。

### hex（`.hex`）

1行に1命令、**5桁の大文字16進数**（18bit を左0詰め）。

```
0E000
0F000
20005
...
```

Verilog 側が `$readmemh` でそのまま読みます。行番号がそのまま命令アドレスです。

## mincc が必ず付ける crt0

`mincc` は出力の先頭に小さなスタートアップコードを置きます。

```asm
mvi r14,0        ; Y = 0（フレームポインタの初期値）
mvi r15,0
calr __on_entry  ; グローバル変数の初期化
calr main
push r1          ; main の戻り値を積む（テストがここを見る）
push r0
halt
__on_entry:
...              ; グローバル変数の初期化代入がここに並ぶ
ret
```

- `__on_entry` はグローバル変数の初期化子（`char x = 5;` の `= 5` の部分）を
  実行するためのものです。初期化子がなければ `ret` だけになります。
- `main` の戻り値を push してから `halt` するので、シミュレーション終了時の
  スタックトップが戻り値になります。テストはこれを見ています。
- **割り込みは有効化されません。** `[[isr=N]]` を使う場合は自分で `sei()` を
  呼んでください。

`[[isr=N]]` を使ったプログラムでは、この crt0 の前にベクタテーブルが入り、
crt0 自体は `.org 0x0005` へ移動します（[cpu/interrupts.md](../cpu/interrupts.md)）。

## 例と成果物

`example/` には `.c` とビルド済みの `.asm` / `.hex` が入っています。

| ファイル | 内容 |
|---|---|
| `blink.c` | タイマ割り込みで `millis()` を作り、2つのLEDを別周期で点滅 |
| `uart.c` | UART へ文字を送る |
| `echoback.c` | UART エコーバック |
| `i2c.c` | I2C マスタ |
| `lcd.c` | I2C 接続のキャラクタLCD |
| `demo.c` | UART + パラレル接続LCD の総合デモ |

`.c` を変更したら `.asm` / `.hex` も作り直してください（自動生成ではありません）。

## ドキュメント

- C の言語仕様 → [mincc.md](mincc.md)
- アセンブラの文法 → [mincasm.md](mincasm.md)
- コンパイラの内部設計 → [internals.md](internals.md)
- テスト → [testing.md](testing.md)
