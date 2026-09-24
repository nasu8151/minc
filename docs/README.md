# minc ドキュメント

`minc` は「最小のFPGAリソースでC言語を動かす」ことを目標にした8ビットCPUコアと、
その専用ツールチェーン（Cコンパイラ `mincc` / アセンブラ `mincasm`）を
同時に設計するプロジェクトです。

命令セット・コンパイラ・ハードウェアが**同時に**設計されているため、
どれか1つだけを読んでも全体像はつかめません。
このディレクトリは、目的別に読む場所を分けたドキュメント一式です。

## どこから読むか

| やりたいこと | 読む場所 |
|---|---|
| とりあえずビルドして動かしたい | [guide/getting-started.md](guide/getting-started.md) |
| minc で C を書きたい（言語仕様） | [toolchain/mincc.md](toolchain/mincc.md) |
| LED・UART・I2C・タイマを叩きたい | [guide/io-and-peripherals.md](guide/io-and-peripherals.md) |
| 割り込みを使いたい | [guide/interrupts-howto.md](guide/interrupts-howto.md) |
| アセンブリを書きたい / 命令を調べたい | [cpu/isa.md](cpu/isa.md), [toolchain/mincasm.md](toolchain/mincasm.md) |
| CPU の内部構造を知りたい | [cpu/overview.md](cpu/overview.md), [cpu/cores.md](cpu/cores.md) |
| コンパイラを改造したい | [toolchain/internals.md](toolchain/internals.md) |
| 命令を追加・変更したい | [下記「命令セットを変更するとき」](#命令セットを変更するとき) |
| 動かないんだけど | [known-issues.md](known-issues.md) |

## 構成

```
docs/
├── README.md                     ← いまここ
├── known-issues.md               既知の不具合・制限の一覧
├── cpu/                          ── CPU本体（ハードウェア）
│   ├── overview.md               設計思想・レジスタ・実行サイクル
│   ├── isa.md                    命令セットリファレンス（全命令・エンコーディング）
│   ├── memory-map.md             命令空間 / データ空間 / メモリマップドレジスタ
│   ├── interrupts.md             割り込みハードウェア（PSR・ベクタ・reti）
│   ├── cores.md                  4つのコア実装の比較（minc_h / p2 / p5 / legacy）
│   └── fpga.md                   Gowin FPGA での実装と周辺回路
├── toolchain/                    ── コンパイラ・アセンブラ
│   ├── overview.md               ツールチェーン全体の流れとビルド
│   ├── mincc.md                  mincc がサポートする C の仕様
│   ├── mincasm.md                アセンブラの文法・ディレクティブ・エラー
│   ├── internals.md              mincc の内部設計（呼び出し規約・コード生成）
│   └── testing.md                テストの走らせ方・書き方
└── guide/                        ── 使い方（アプリケーションノート）
    ├── getting-started.md        最初の一歩
    ├── io-and-peripherals.md     GPIO / UART / I2C / タイマ
    ├── interrupts-howto.md       割り込みで millis() を作る
    └── inline-asm.md             インラインアセンブラと手書き .asm
```

## リポジトリ全体の地図

| パス | 中身 |
|---|---|
| `mincc/` | C コンパイラ（C → minc アセンブリ） |
| `mincasm/` | アセンブラ（アセンブリ → 18bit hex） |
| `verilog/` | CPU の RTL・テストベンチ・メモリモデル |
| `gowin/` | Gowin FPGA 向けプロジェクト（周辺回路つき SoC） |
| `example/` | サンプルプログラム（`.c` と、ビルド済みの `.asm`/`.hex`） |
| `tests/` | Python によるエンドツーエンドテスト |
| `temp/` | 使い捨ての実験用（gitignore 済み。一時ファイルはここへ） |
| `target/` | ビルド成果物（`mincc` / `mincasm`） |

ルートには他に、他CPUコアとの規模・性能比較をまとめた
[`Architecture_Comparison.md`](../Architecture_Comparison.md) があります。

> **`Hardware.md` について**
> ルートの `Hardware.md` はこの `docs/` 以前からある命令表＋割り込み仕様のメモです。
> 同じ内容は [cpu/isa.md](cpu/isa.md) と [cpu/interrupts.md](cpu/interrupts.md) に
> 実装から取り直した形で入っているので、今後はそちらを正とするのが安全です。

## 命令セットを変更するとき

ISA は3箇所に同時に存在します。1つだけ直すと必ず壊れます。

1. `mincasm/main.c` の `g_inst_specs[]` — ニモニック → エンコーディング
2. `verilog/minc_h.sv` の `is_*` ワイヤ群 — デコーダ
3. `mincc/codegen.c` — どの命令列を吐くか

さらに、副次的に次も追随が必要です。

4. `verilog/minc_p2.sv` / `verilog/minc_p5.sv` — 別実装のデコーダ
   （※現在は旧オペコードマップのまま。[known-issues.md](known-issues.md) 参照）
5. `docs/cpu/isa.md` — 命令表
6. `tests/test.py` — 必要ならテストケース

変更後は必ず `make test` を通してください（[toolchain/testing.md](toolchain/testing.md)）。
