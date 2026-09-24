# はじめかた

minc のプログラムをビルドしてシミュレータで動かすまでの手順です。

## 1. 必要なもの

| ツール | 用途 | 必須 |
| --- | --- | --- |
| `gcc` | `mincc` / `mincasm` のビルド | ○ |
| `make` | 同上 | ○ |
| `iverilog` / `vvp` | シミュレーション | ○ |
| `python3` | テストの実行 | テストを走らせるなら |
| Gowin FPGA Designer | 実機で動かす | FPGA を使うなら |

Windows では **msys2 + mingw-w64** を推奨します。

## 2. ツールチェーンをビルドする

```sh
make
```

`target/mincc` と `target/mincasm` ができます（Windows では `.exe` 付き）。

## 3. 最初のプログラム

`hello.c` として保存します。

```c
char main() {
    return 42;
}
```

> **コメントは書けません。** `//` も `/* */` もエラーになります
> （[toolchain/mincc.md](../toolchain/mincc.md)）。

## 4. コンパイル → アセンブル

```sh
./target/mincc   < hello.c   > hello.asm 2>/dev/null
./target/mincasm < hello.asm > verilog/program.hex
```

`mincc` は標準エラーへ大量のデバッグログを出すので `2>/dev/null` で捨てています。

`hello.asm` の中身はこうなります。

```asm
mvi r14,0            ; ここから crt0
mvi r15,0
calr __on_entry
calr main
push r1              ; main の戻り値を積む
push r0
halt
__on_entry:          ; グローバル変数の初期化（今回は無し）
ret
main:
push r15
push r14
ldm r14,0            ; Y = SP （フレームポインタ）
ldm r15,1
mvi r2,42
mov r0,r2            ; 戻り値を r0 へ
mvi r1,0
stm 0,r14            ; SP = Y
stm 1,r15
pop r14
pop r15
ret
```

`verilog/program.hex` は1行1命令の5桁16進です。

## 5. シミュレーションする

```sh
cd verilog
iverilog -g2012 -DSIM -o sim.out minc_h.sv minc_tb.sv
vvp sim.out
```

> **`-g2012` などのフラグはファイル名より前に置いてください。**
> 後ろに置くと SystemVerilog として解釈されずエラーになります。

出力の最後がこうなれば成功です。

```text
PORTA: 0
PC: 6, TOP: 2a, SP: fffe
CYCLES: 80
```

| 項目 | 意味 |
| --- | --- |
| `TOP` | スタックトップ = `main` の戻り値。`0x2a` = 42 ✓ |
| `SP` | `fffe` なら push と pop の数が合っている |
| `CYCLES` | 実行に要したクロック数 |
| `PORTA` | PORT A 出力ラッチの値 |

### 中で何が起きているか見る

`-DVERBOSE` を足すと、命令が1つ終わるたびに PC・命令語・全レジスタが出ます。

```sh
iverilog -g2012 -DSIM -DVERBOSE -o sim.out minc_h.sv minc_tb.sv
vvp sim.out
```

```text
PC=10   insn=0822A  R0=00  R1=00  R2=2a  R3=00 ...
```

波形は `verilog/minc_tb.vcd` に残るので GTKWave でも開けます。

## 6. テストを走らせる

```sh
make test
```

50件以上のエンドツーエンドテストが通ればツールチェーンは健全です
（[toolchain/testing.md](../toolchain/testing.md)）。

## 次に読むもの

| やりたいこと | 読む場所 |
| --- | --- |
| C の書ける範囲を知る | [toolchain/mincc.md](../toolchain/mincc.md) |
| LED を光らせる | [io-and-peripherals.md](io-and-peripherals.md) |
| 割り込みを使う | [interrupts-howto.md](interrupts-howto.md) |
| アセンブリを混ぜる | [inline-asm.md](inline-asm.md) |
| FPGA で動かす | [cpu/fpga.md](../cpu/fpga.md) |

## つまずきやすいところ

| 症状 | 原因 |
| --- | --- |
| `Invalid token` | コメントを書いた／サポート外の文字を使った |
| `Undefined or invalid identifyer` | 関数を定義より前に呼んだ（前方宣言できません） |
| `Local variable frame does not fit...` | ローカル変数の合計が32バイトを超えた |
| `TOP: xx` | 未初期化の変数を返している |
| `[ERROR] PC == xxxx` | PC が未定義領域へ飛んだ（スタック破壊が疑わしい） |
| `Size cast requires SystemVerilog` | `iverilog` の `-g2012` をファイル名の後ろに置いた |
| ビット演算と `&&` を混ぜたら結果がおかしい | **優先順位が標準Cと違います**。括弧を付けてください |
