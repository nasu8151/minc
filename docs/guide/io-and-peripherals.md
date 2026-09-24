# I/O と周辺回路

minc に I/O 命令はありません。周辺回路は**すべてメモリマップド**で、
C からは `[[address = N]]` 付きのグローバル変数として叩きます。

```c
char [[address = 0x04]] PORTA_OUT;

char main() {
    PORTA_OUT = 0x55;
    return 0;
}
```

アドレスの一覧は [cpu/memory-map.md](../cpu/memory-map.md)、
各レジスタの詳細は [cpu/fpga.md](../cpu/fpga.md) にあります。

> **FPGA では `define` の確認を忘れずに。**
> 周辺回路は `gowin/minc/src/minc_gw_top.sv` の先頭にある
> `` `define UART `` / `PORTA` / `I2C` / `TIMER8` で有効・無効を切り替えます。
> 現在の作業ツリーでは**全部コメントアウト（無効）**になっています。

---

## PORT A（8bit GPIO）

| アドレス | 名前 | 内容 |
| --- | --- | --- |
| `0x04` | `PORTA_OUT` | 出力ラッチ。`PORTA_DIR` で 1 のビットだけ書き込まれる |
| `0x05` | `PORTA_DIR` | 方向（1 = 出力）。リセット時は**全入力** |
| `0x06` | `PORTA_IN` | 入力 |

**先に `PORTA_DIR` を設定しないと何も出ません。**
`PORTA_OUT` への書き込みは `data_out & port_a_dir` でマスクされます。

```c
char [[address = 0x04]] PORTA_OUT;
char [[address = 0x05]] PORTA_DIR;

char main() {
    PORTA_DIR = 0xFF;        // 全ビット出力
    PORTA_OUT = 0b10101010;
    return 0;
}
```

シミュレーションでは最終値が `PORTA:` として表示されます。
テストで `porta=` を指定すると自動検証されます
（[toolchain/testing.md](../toolchain/testing.md)）。

### 双方向で使う（LCD の 4bit 接続など）

`example/demo.c` は方向レジスタを切り替えながらパラレル接続の LCD を叩いています。

```c
void lcd_send4(char rs, char c) {
    PORTA_DIR = 0b11110011;
    char v = rs | c;
    PORTA_OUT = v;
    PORTA_OUT = 0b10 | v;     // E パルス
    PORTA_OUT = v;
    PORTA_DIR = 0;
}
```

---

## UART

Gowin の 16550 互換 IP です。オフセットは 16550 準拠。

| アドレス | 名前 | 用途 |
| --- | --- | --- |
| `0x08` | `DATA` | 送受信バッファ |
| `0x09` | `IER` | 割り込み許可 |
| `0x0A` | `IIR` | 割り込み識別 |
| `0x0B` | `LCR` | ライン制御（フォーマット設定） |
| `0x0C` | `MCR` | モデム制御 |
| `0x0D` | `LSR` | ラインステータス |
| `0x0E` | `MSR` | モデムステータス |

`INTR` はどこにも配線されていないので、**ポーリングで使います**。

```c
char [[address = 0x08]] UARTC_DATA;
char [[address = 0x0B]] UARTC_LCR;
char [[address = 0x0D]] UARTC_LSR;

void uart_init() {
    UARTC_LCR = 0x03;        // 8N1
}

void uart_putch(char c) {
    while ((UARTC_LSR & 0b00100000) ^ 0b00100000) { }   // THR 空き待ち
    UARTC_DATA = c;
}
```

受信は `LSR` bit0（データ有り）を見ます。

```c
char main() {
    uart_init();
    while (1) {
        if (UARTC_LSR & 0x01) {
            char c = UARTC_DATA;
            uart_putch(c);           // エコーバック
        }
    }
}
```

実例: `example/uart.c`, `example/echoback.c`, `example/demo.c`

> `UART` を `define` すると `WAIT`（ウェイトステート生成）も自動で有効になります。
> UART IP がバスに追従するために必要です。

---

## I2C マスタ

Gowin の I2C マスタ IP（OpenCores 互換のレジスタ配置）。

| アドレス | 書き込み | 読み出し |
| --- | --- | --- |
| `0x10` | プリスケーラ 下位 | 同 |
| `0x11` | プリスケーラ 上位 | 同 |
| `0x12` | `CONTROL` | 同 |
| `0x13` | `TRANSMIT` | `RECEIVE` |
| `0x14` | `COMMAND` | `STATUS` |

書き込みと読み出しで**別のレジスタになるアドレスがある**ので、
名前を2つ宣言しておくと読みやすくなります。

```c
char [[address = 0x10]] i2c_presc_l;
char [[address = 0x11]] i2c_presc_h;
int  [[address = 0x10]] i2c_presc;      /* 16bit でまとめて書ける */
char [[address = 0x12]] i2c_control;
char [[address = 0x13]] i2c_transmit;
char [[address = 0x13]] i2c_recieve;
char [[address = 0x14]] i2c_command;
char [[address = 0x14]] i2c_status;

void i2c_init() {
    i2c_presc = 54;              /* SCL の分周比 */
    i2c_control = 0b10000000;    /* コア有効 */
}

char i2c_start(char addr, char rw) {
    i2c_transmit = (addr * 2) | rw;
    i2c_command = 0x90;                    /* START + WRITE */
    while (i2c_status & 0b00000010) { }    /* TIP（転送中）が下りるまで */
    return 0;
}

char i2c_write(char data) {
    i2c_transmit = data;
    i2c_command = 0x10;                    /* WRITE */
    while (i2c_status & 0b00000010) { }
    return 0;
}

char i2c_read() {
    i2c_command = 0x20;                    /* READ */
    while (i2c_status & 0b00000010) { }
    return i2c_recieve;
}
```

同じアドレスに `char` と `int` の両方の名前を付けているのがコツです。
`int` 側に書けば `stm` が2回出るので、プリスケーラの上位・下位をまとめて設定できます。

実例: `example/i2c.c`（温度センサ読み出し）, `example/lcd.c`（I2C 接続 LCD）

---

## 8bit インターバルタイマ

自作 IP（`gowin/ips/timer/timer8.sv`）。

| アドレス | 名前 | 内容 |
| --- | --- | --- |
| `0x18` | `CONFIG` | bit0 = `EN`、bit1 = `IE_OVF`、bit2 = `IE_CMP`、bit[6:3] = プリスケーラ選択 |
| `0x19` | `COMPARE` | コンペア値 |
| `0x1A` | `OVERFLOW` | 周期（TOP）。カウンタはこの値で 0 に戻る |
| `0x1B` | `COUNTER` | 現在値（読み書き可） |
| `0x1C` | `STATUS` | bit0 = OVF 保留、bit1 = CMP 保留。**1を書くとクリア（W1C）** |

`O_OVF_INT`（`STATUS.OVF && CONFIG.IE_OVF`）が `irq_line[0]` に配線されています。
`O_CMP_INT` は未配線です。

```c
char [[address = 0x18]] TIMER8_CONFIG;
char [[address = 0x1A]] TIMER8_TOP;
char [[address = 0x1C]] TIMER8_STATUS;

char main() {
    TIMER8_TOP = 211;
    TIMER8_CONFIG = 0b00111011;
    sei();
    while (1) { }
}
```

`0b00111011` の内訳:

| ビット | 値 | 意味 |
| --- | --- | --- |
| bit0 `EN` | 1 | タイマ動作開始 |
| bit1 `IE_OVF` | 1 | オーバーフロー割り込み許可 |
| bit2 `IE_CMP` | 0 | コンペア割り込みは使わない |
| bit[6:3] | `0111` = 7 | プリスケーラ /2^7 = /128 |

> `CONFIG` への書き込みは IP 内部で `& 0x3F` されるため **bit6 と bit7 は常に0** です。
> プリスケーラ選択は実質3ビット（`0`〜`7`）で、**/1 〜 /128** の範囲になります。

**割り込みはレベル出力なので、ISR の先頭で `STATUS` に 1 を書いて
要因をクリアしないと即座に再突入します。**
詳しくは [interrupts-howto.md](interrupts-howto.md)。

### 周期の計算

```
割り込み周期 = (システムクロック周期) × (プリスケーラ分周比) × (TOP + 1)
```

27 MHz、プリスケーラ /128、TOP = 211 なら

```
(1/27e6) × 128 × 212 ≒ 1.005 ms
```

つまり約1ミリ秒。`example/blink.c` はこれで `millis()` を作っています。

---

## 自分で周辺回路を足す

1. `minc_gw_top.sv` にベースアドレスと `ce`（チップイネーブル）を足す

   ```systemverilog
   localparam MYDEV_ADDRESS_BASE = 16'h0020;
   localparam MYDEV_ADDRESS_LEN  = 3;   // 2^3 = 8 バイト
   wire mydev_ce = (address[15:MYDEV_ADDRESS_LEN] ==
                    MYDEV_ADDRESS_BASE[15:MYDEV_ADDRESS_LEN]);
   ```

2. `data_in` の mux に読み出しデータを足す
3. 遅い回路なら `WAIT` を有効にして `wait_ma` でストローブを作る
4. 割り込みが要るなら `irq_line[N]` に接続する

制約:

- 絶対アドレッシングが届くのは **`0x0000`–`0x03FF`** です
- `0x0000`–`0x0003` は CPU 内蔵レジスタ（SP / PSR）なので使えません
- `0x0100` 以降はグローバル変数と RAM の領域です。実際に使えるのは
  **`0x0004`–`0x00FF`** の範囲になります
