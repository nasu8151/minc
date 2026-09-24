# FPGA 実装（Gowin）

`gowin/minc/` は Gowin IDE のプロジェクトです。CPU コア単体ではなく、
ブロックRAM と周辺回路を足した小さな SoC になっています。

| 項目 | 値 |
|---|---|
| デバイス | `GW1NR-LV9QN88PC6/I5`（GW1NR-9 / Tang Nano 9K 相当） |
| システムクロック | 27 MHz（`sys_clk`、周期 37.037 ns） |
| トップレベル | `gowin/minc/src/minc_gw_top.sv` |
| プロジェクト | `gowin/minc/minc.gprj` |
| 制約 | `src/minc.cst`（ピン）, `src/minc.sdc`（タイミング） |

## 構成

```
              ┌──────────────┐
 sys_clk ────▶│   minc       │  (verilog/minc_h.sv)
 sys_nrst ───▶│              │
              └──┬────┬──────┘
      address/we │    │ data_in
                 ▼    ▲
        ┌────────────────────────────────┐
        │  アドレスデコード + データmux   │
        └──┬──────┬──────┬──────┬────────┘
           ▼      ▼      ▼      ▼
        PORT A  UART   I2C   TIMER8   Gowin_SP (ブロックRAM)
        0x04-06 0x08-0F 0x10-17 0x18-1F   0x0100～
```

## 周辺回路の有効・無効

`minc_gw_top.sv` の先頭にある `` `define `` で切り替えます。

```systemverilog
// `define UART
// `define PORTA
// `define WAIT
// `define I2C
// `define TIMER8
```

- **現在の作業ツリーでは全部コメントアウト（＝全部無効）です。**
  何かを使いたければまずここを外してください。
- `UART` または `I2C` を有効にすると、`WAIT`（ウェイトステート生成）も
  自動的に有効になります。これらの IP はバスに追従するために
  数サイクルのウェイトを必要とするためです。

## 周辺回路レジスタマップ

### PORT A（`PORTA`）— `0x0004`–`0x0006`

| アドレス | 名前 | 内容 |
|---|---|---|
| `0x0004` | `PORTA_OUT` | 出力ラッチ。**`PORTA_DIR` で 1 のビットだけ**書き込まれる |
| `0x0005` | `PORTA_DIR` | 方向（1 = 出力）。リセット時は全入力 |
| `0x0006` | `PORTA_IN` | 入力値 |

トップレベルのポート `port_a[7:0]` に直結しています。

### UART（`UART`）— `0x0008`–`0x000F`

Gowin の 16550 互換 IP（`src/uart_master/`）です。オフセットは 16550 準拠。

| アドレス | 名前 |
|---|---|
| `0x0008` | `DATA`（送受信バッファ） |
| `0x0009` | `IER` |
| `0x000A` | `IIR` |
| `0x000B` | `LCR` |
| `0x000C` | `MCR` |
| `0x000D` | `LSR` |
| `0x000E` | `MSR` |

`INTR` 出力は**現在どこにも配線されていません**（ポーリングのみ）。
使用例は `example/uart.c` / `example/echoback.c`。

### I2C マスタ（`I2C`）— `0x0010`–`0x0017`

Gowin の I2C マスタ IP（`src/i2c_master/`）。OpenCores 互換のレジスタ配置です。

| アドレス | 名前 |
|---|---|
| `0x0010` | プリスケーラ 下位 |
| `0x0011` | プリスケーラ 上位 |
| `0x0012` | `CONTROL` |
| `0x0013` | 書き込み時 `TRANSMIT` / 読み出し時 `RECEIVE` |
| `0x0014` | 書き込み時 `COMMAND` / 読み出し時 `STATUS` |

使用例は `example/i2c.c` / `example/lcd.c`。

### 8bit インターバルタイマ（`TIMER8`）— `0x0018`–`0x001F`

自作 IP（`gowin/ips/timer/timer8.sv`）。

| アドレス | 名前 | R/W | 内容 |
|---|---|---|---|
| `0x0018` | `CONFIG` | RW | bit0 = `EN`、bit1 = `IE_OVF`、bit2 = `IE_CMP`、bit[6:3] = プリスケーラ選択（2^n 分周） |
| `0x0019` | `COMPARE` | RW | コンペア閾値 |
| `0x001A` | `OVERFLOW` | RW | 周期（TOP）。カウンタはこの値に達すると 0 に戻る |
| `0x001B` | `COUNTER` | RW | 現在値（ソフトウェアから書ける） |
| `0x001C` | `STATUS` | RW | bit0 = OVF 保留、bit1 = CMP 保留。**1を書くとクリア（W1C）** |
| `0x001D`–`0x001F` | — | — | 予約（読むと0、書き込み無視） |

`CONFIG` への書き込みは `& 0x3F` されるので **bit6/bit7 は常に0** です。
結果としてプリスケーラ選択は実質3ビット（`0`〜`7`）＝ **/1 〜 /128** になります。
カウンタは `0` から `OVERFLOW` まで数えて 0 に戻るので、割り込み周期は

```text
(クロック周期) × 2^プリスケーラ選択 × (OVERFLOW + 1)
```

です（27 MHz・/128・`OVERFLOW = 211` で約 1.005 ms）。

割り込み出力:

| 信号 | 条件 | 配線先 |
|---|---|---|
| `O_OVF_INT` | `STATUS.OVF && CONFIG.IE_OVF`（レベル） | `irq_line[0]` |
| `O_CMP_INT` | `STATUS.CMP && CONFIG.IE_CMP`（レベル） | 未配線 |
| `O_OVERFLOW` / `O_COMPARE` | 1クロックパルス | 未配線 |

レベル出力なので、ISR の**先頭で `STATUS` に 1 を書いて要因をクリア**しないと
再突入ループになります（[interrupts.md](interrupts.md) の「既知の制約」）。

使用例は `example/blink.c` と [guide/interrupts-howto.md](../guide/interrupts-howto.md)。

## メモリ

- 命令ROM は `minc_h.sv` の中に `$readmemh("program.hex")` で埋め込まれます。
  合成時に `syn_romstyle = "BLOCK_ROM"` でブロックRAMに落ちます（4096語）。
- データRAMは Gowin のシングルポートRAM IP（`Gowin_SP`）。
  `ram_ce = address > 0x00FF` なので、**`0x0100` 以降が RAM** です。
  それより下は周辺回路と CPU 内蔵レジスタの領域です。

## デバッグ用の出力

トップレベルには観測用のポートが出ています。

| ポート | 内容 |
|---|---|
| `address_out[7:0]` | `we` のとき `data_out`、それ以外は `data_in`（バスの中身） |
| `address_out2[5:0]` | `~address[5:0]`（LED向けに反転） |
| `wait_req_out` / `we_out` | バス制御信号 |
| `avma_out` | **`irq_line[0]`** が出ています（名前と中身が一致していないので注意） |
| `pc_out[15:0]` | PC |

## 主なピン割り当て

| 信号 | ピン |
|---|---|
| `sys_clk` | 52 |
| `sys_nrst` | 3 |
| `port_a[7:0]` | 38, 37, 36, 39, 25, 26, 27, 28 |
| `uart_tx` / `uart_rx` | 29 / 30 |
| `i2c_scl` / `i2c_sda` | 34 / 33 |

完全な一覧は `gowin/minc/src/minc.cst` を参照してください。

## ビルド手順

1. `mincc` → `mincasm` でプログラムをビルドし、`program.hex` を作る
   （[toolchain/overview.md](../toolchain/overview.md)）
2. `program.hex` を Gowin プロジェクトから見える場所に置く
   （`$readmemh` の相対パス解決は合成の作業ディレクトリ基準）
3. Gowin IDE で `gowin/minc/minc.gprj` を開き、Synthesize → Place & Route
4. Programmer で書き込み

> `gowin/minc_16/` は 16bit 化の実験用に途中まで作られたプロジェクトで、
> トップレベルがまだありません（未追跡ファイル）。現時点では動きません。
