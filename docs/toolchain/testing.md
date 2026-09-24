# テスト

```sh
make test                      # clean → build → 全テスト
python3 tests/test.py          # ビルド済み前提で全テスト
python3 tests/test.py <ケース名>  # E2E を1件だけ
python3 tests/test.py irq      # 割り込みテストだけ
python3 tests/test_pipeline.py # 3コアすべてに対して E2E を流す（make test には含まれない）
```

必要なもの: `gcc`, `python3`, `iverilog` / `vvp`（PATH 上にあること）。

## 何をテストしているか

**単体テストはありません。全部エンドツーエンドです。**
1ケースにつき、次を毎回丸ごと実行します。

```text
C ソース
  → mincc          （C → asm）
  → mincasm        （asm → hex）
  → verilog/test.hex に書き出し
  → iverilog でコア + minc_tb.sv をコンパイル（-DTEST -DVERBOSE -DSIM）
  → vvp で実行
  → 出力の最後の3行を読んで検証
```

検証するのは次の3つです。

| 項目 | 内容 |
| --- | --- |
| `TOP` | スタックトップの16bit値 = `main` の戻り値（crt0 が push している） |
| `SP` | 終了時に `65534` (`0xFFFE`) であること。**スタックの対称性チェック** |
| `PORTA` | `porta=` を指定したケースのみ。PORT A 出力ラッチの値 |

`SP` のチェックが効いていて、push と pop の数が合わないコードを生成すると
戻り値が合っていても落ちます。

## テストの構造

| ファイル | 役割 |
| --- | --- |
| `tests/test.py` | テストケースの定義とランナー |
| `tests/testfuncs.py` | 実行ヘルパ（`expect` / `expect_fail` / `test_e2e` / `test_irq` / `test_irq_e2e`） |
| `tests/test_pipeline.py` | 3つのコアに対して同じケースを流し、サイクル数を比較 |
| `tests/fixtures/irq_vector.asm` | 手書きアセンブリの割り込みテスト用フィクスチャ |

### ヘルパ

| 関数 | 用途 |
| --- | --- |
| `tf.expect(cmd, input, expected_stdout)` | コマンドが成功し、期待どおりの出力をすること |
| `tf.expect_fail(cmd, input)` | コマンドが**失敗する**こと（コンパイルエラー・アセンブルエラーの検査） |
| `tf.test_e2e(code, expected_top, title, ...)` | フルパイプライン |
| `tf.test_irq(asm_path, irq_cycle, expected_top, ...)` | 手書き asm + 割り込み（`minc_h.sv` 専用） |
| `tf.test_irq_e2e(code, irq_cycle, expected_top, ...)` | `[[isr=N]]` つき C のフルパイプライン |

`test_e2e` / `test_irq` / `test_irq_e2e` はいずれも `core: str = "minc_h.sv"`
引数を取るので、任意のケースを任意のコアへ向けられます。

## ケースを追加する

### 正常系（E2E）

`tests/test.py` の `E2E_CASES` 辞書に1行足すだけです。

```python
E2E_CASES = {
    ...
    "mycase": ("char main(){return 1+2;}", 3, {}),
    #  ^タイトル   ^Cソース                 ^期待TOP ^test_e2e への追加kwargs
}
```

追加の kwargs でよく使うもの:

| kwarg | 意味 |
| --- | --- |
| `{"verbose": True}` | シミュレータの出力を全部表示 |
| `{"porta": 0x55}` | PORT A の期待値も検査 |

期待値 `-1` は「`TOP` が `xx`（未初期化）であること」を意味します
（未初期化変数の挙動を確かめるケースで使います）。

### 異常系

「コンパイル・アセンブルが失敗すること」を確かめるものは
`tf.expect_fail` の直接呼び出しで書きます。

```python
tf.expect_fail("./target/mincc", """char main(){1+}""")
tf.expect_fail("./target/mincasm", "stm Y-40,r2")
```

### 割り込みのケース

テストベンチ `minc_tb.sv` は `` `ifdef IRQ_TEST `` 内で `irq_in` を駆動します。
制御は `vvp` の plusargs です。

| plusarg | 意味 |
| --- | --- |
| `+irq_cycle=N` | N サイクル目から割り込みを立てる |
| `+irq_mask=M` | どの `irq_in` ビットを立てるか（`1`=IRQ0, `4`=IRQ2 …） |
| `+irq_len=L` | 何サイクル立て続けるか（既定 6） |
| `+irq_period=P` | `0`（既定）なら1回だけ。`>0` なら P サイクルごとに繰り返す |

既存の割り込みテストが確認しているのは次の点です。

- 優先度どおりのベクタが選ばれること（`irq_mask=0b0101` なら IRQ0）
- 中断された命令から**正確に**再開すること
- ISR が `PSR` を意図的に壊しても、`reti` がシャドウから復元すること
- `[[isr=N]]` つき C ソースがベクタテーブル配置・レジスタ退避・`reti` まで
  含めて通ること（`test_irq_e2e`）

## コア間の比較

```sh
python3 tests/test_pipeline.py
```

`E2E_CASES` 全件を `minc_h.sv` / `minc_p2.sv` / `minc_p5.sv` の3つに対して
実行し、バイナリ互換性を確認したうえでケースごと・合計のサイクル数比較表を
出します。

> 現在、パイプライン版は旧オペコードマップのままなのでこのテストは通りません。
> [known-issues.md](../known-issues.md) 参照。

## 落ちたときに見るもの

テストが落ちると、次が自動的に表示されます。

- `mincc` が出したアセンブリ全文
- 入力の C ソース
- シミュレータの出力（`verbose=True` のとき）

さらに詳しく見るなら、波形が `verilog/minc_tb.vcd` に残っているので
GTKWave で開けます。

```sh
gtkwave verilog/minc_tb.vcd
```

`-DVERBOSE` が付いているので、`S_FETCH` に入るたび（＝直前の命令の
ライトバック完了時点）に PC・命令語・全レジスタが1行で表示されます。
これがいちばん速い切り分け手段です。

```text
PC=12   insn=0E020  R0=00  R1=00  R2=05  R3=03 ...
```

## タイムアウト

テストベンチは 131071 サイクルで打ち切ります。また `PC` が `xxxx` に
なった時点で「[ERROR] PC == xxxx」と表示して停止します。
無限ループやスタック破壊のときはこれらに引っかかります。
