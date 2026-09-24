# コア実装の比較

`verilog/` には4つの CPU 実装があります。**どれを対象にした作業なのかを
最初に確認してください。** 特に `minc.sv` は名前が紛らわしいですが別物です。

| ファイル | 位置づけ | 命令幅 | 実行方式 | 割り込み |
|---|---|---|---|---|
| `minc_h.sv` | **リファレンス実装**（現行） | 18 bit | 4状態 非パイプライン | ○ |
| `minc_p2.sv` | 2段パイプライン版 | 18 bit | Fetch / Execute | × |
| `minc_p5.sv` | 5段パイプライン版 | 18 bit | IF/ID/EX/MEM/WB | × |
| `minc.sv` | **旧世代**（別ISA、非互換） | 15 bit | 4状態 | × |

`minc_h.sv` / `minc_p2.sv` / `minc_p5.sv` が現在の開発対象です。

---

## `minc_h.sv` — リファレンス実装

- `S_FETCH → S_DECEXEC → S_MA → S_WB` の4状態。**全命令が一律4サイクル**
- ALU の加算器と AGU（アドレス生成）が**同一のキャリーチェーン**を共有
- レジスタペア読み出し用に `regs_hi[]`（奇数レジスタのミラー）を持つ
- ROM は 4096 語（ブロックRAM推論のため下位12bitのみでアドレッシング）
- 割り込みハードウェアを持つ唯一のコア
- SP / PSR / PSR_SHADOW をデータ空間 `0x0000`–`0x0003` にメモリマップ

仕様の詳細は [overview.md](overview.md) / [isa.md](isa.md) / [interrupts.md](interrupts.md)。

---

## `minc_p2.sv` — 2段パイプライン（PicoBlaze 風）

Fetch と Execute の2段。**フォワーディング回路が要りません**。

理由: アーキテクチャ状態（レジスタファイル・SP・キャリー）に触るのは
Execute 段にいる1命令だけで、Fetch 段はアーキテクチャ状態を一切読まないため。
必要なのは

- 構造ハザードによるストール（Execute が塞がっている間）
- 分岐時のフラッシュ（予測なし・常にフラッシュ）

の2つだけです。Fetch 段には深さ1のスキッドバッファがあります。

Execute 段の内部状態は `XS_DE → XS_MA → XS_WB` の3フェーズで、
ALU 系の命令は `XS_DE` だけで完結します。

---

## `minc_p5.sv` — 5段パイプライン（教科書型）

IF / ID / EX / MEM / WB の5段 + フォワーディング。

| 項目 | 扱い |
|---|---|
| `jz` / `jr` / `calr` | EX で解決 → `ifid` + `idex` をフラッシュ（2バブル） |
| `ret` | 2バイトの逐次読み出しが必要なため **WB で解決** → `ifid`+`idex`+`exmem` をフラッシュ |
| `stm`/`ldm`/`push`/`pop`/`calr`/`ret` | MEM を最低2フェーズ占有（phase0 = アドレス設定、phase1 = 実バス転送） |
| `wait_req` | `stm`/`ldm` のみが見る（`minc_h.sv` と同じ挙動） |
| RAW ハザード | EX/MEM・MEM/WB から EX へフォワード + ID 段で pending WB をバイパス |
| ロードユース | `ldm`/`pop` の直後に使う場合のみ1サイクルストール |

---

## `minc.sv` — 旧世代コア

- **15bit 命令幅**、オペコードマップも状態機械も別物
- 現行の `mincasm` 出力とは**バイナリ互換がありません**
- SP は `[14:0]` + `sp0` という別表現

歴史的経緯の参照用です。新規の作業対象にはなりません。

---

## バイナリ互換性の現状

`minc_p2.sv` / `minc_p5.sv` のデコーダは**旧オペコードマップのまま**です。

```systemverilog
// minc_p2.sv / minc_p5.sv（現状）
is_jz    = (op6 == 6'b001100);   // 現行では ret
is_mvi   = (op6 == 6'b001110);   // 現行では push
is_stm_x = (op6 == 6'b010000);   // 現行では stm rp+n（ただしフィールド割りが違う）
```

現行の `mincasm` が出す hex をこれらのコアで実行することはできません。
[known-issues.md](../known-issues.md) を参照。

## コア間の比較テスト

`tests/test_pipeline.py` が `tests/test.py` の `E2E_CASES` 全件を
3つのコア（`minc_h.sv` / `minc_p2.sv` / `minc_p5.sv`）に対して実行し、
バイナリ互換性の確認とサイクル数の比較表を出します。

```sh
python3 tests/test_pipeline.py
```

`make test` には含まれていません（単体で実行します）。
`tests/testfuncs.py` の `test_e2e` / `test_irq` は `core:str = "minc_h.sv"`
引数を取るので、任意のケースを任意のコアへ向けられます。

テストベンチ `verilog/minc_tb.sv` の `irq_in` 配線は `` `ifdef IRQ_TEST `` で
囲まれているため、`irq_in` ポートを持たない `minc_p2.sv`/`minc_p5.sv` でも
同じテストベンチがビルドできます。
