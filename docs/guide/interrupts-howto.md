# 割り込みを使う

ハードウェア仕様は [cpu/interrupts.md](../cpu/interrupts.md)。
ここは「実際にどう書くか」です。

## 最小の例

```c
char [[address = 0x04]] PORTA_OUT;
char [[address = 0x05]] PORTA_DIR;

char [[address = 0x18]] TIMER8_CONFIG;
char [[address = 0x1A]] TIMER8_TOP;
char [[address = 0x1C]] TIMER8_STATUS;

char ticks;

void [[isr = 0]] on_timer() {
    TIMER8_STATUS = 0b00000001;    /* 最初に要因をクリア */
    ticks = ticks + 1;
}

char main() {
    ticks = 0;
    PORTA_DIR = 0xFF;
    TIMER8_TOP = 211;
    TIMER8_CONFIG = 0b00111011;    /* EN + IE_OVF + プリスケーラ/128 */
    sei();                          /* ← これを忘れると何も起きない */
    while (1) {
        PORTA_OUT = ticks;
    }
}
```

## 4つの約束ごと

### 1. `sei()` を呼ぶ

**リセット直後は割り込み禁止**です（`PSR` が `2'b00` にリセットされる）。
crt0 は `IE` を触らないので、`[[isr=N]]` を書いただけでは割り込みは来ません。
周辺回路の設定が済んだところで `sei()` を呼んでください。

### 2. ISR の先頭で要因をクリアする

要求線は**レベルトリガ**で、`reti` が `IE` を戻します。
要因が立ったままだと `reti` の直後に即座に再突入し、無限ループになります。

```c
void [[isr = 0]] on_timer() {
    TIMER8_STATUS = 0b00000001;   /* ← まずこれ。W1C なので 1 を書いてクリア */
    ...
}
```

### 3. ISR は引数も戻り値も持てない

```c
void [[isr = 0]] tick() { }          /* OK */
void [[isr = 0]] tick() { return; }  /* OK */

void [[isr = 0]] tick(char x) { }        /* コンパイルエラー */
void [[isr = 0]] tick() { return 1; }    /* コンパイルエラー */
tick();                                   /* コンパイルエラー（直接呼べない） */
```

`[[isr=N]]` の `N` は 0〜3 で、同じ番号を2つの関数が要求するとエラーです。

### 4. ISR と共有する変数のアクセスは囲む

ISR が更新する 2 バイト変数をメインループから読むと、
上位バイトと下位バイトの間で割り込みが入って**引き裂かれた値**を読む可能性があります。
`cli()` / `sei()` で囲んでください。

```c
int millis_count;

int millis() {
    cli();
    int m = millis_count;
    sei();
    return m;
}
```

> `cli()`/`sei()` は `PSR` のリード・モディファイ・ライトなので、
> 直接 `psr = 2;` と書くのと違ってキャリーフラグを壊しません。

## レジスタの退避は不要

ハードウェアは汎用レジスタを自動退避しません。しかし **`mincc` の `[[isr]]` は
本体が実際に触るレジスタをすべて自動で `push`/`pop` します**
（`r0`–`r13` と X ポインタ）。手書きアセンブリのように気を配る必要はありません。

生成されるコードの詳細は [toolchain/internals.md](../toolchain/internals.md)。

## 出力される形

`[[isr=N]]` が1つでもあると、出力の先頭にベクタテーブルが入ります。

```asm
.org 0x0000
jr __crt0_start
.org 0x0001
jr on_timer          ; [[isr=0]]
.org 0x0002
reti                 ; 未使用スロットは安全な reti で埋まる
.org 0x0003
reti
.org 0x0004
reti
.org 0x0005
__crt0_start:
mvi r14,0
...
```

`[[isr=N]]` を使わないプログラムの出力は従来どおり変わりません。

## 実用例: `millis()` を作る

`example/blink.c` は 1ms タイマ割り込みで Arduino 風の `millis()` を作り、
2つの LED を別々の周期で点滅させています。

```c
char [[address = 0x04]] PORTA_OUT;
char [[address = 0x05]] PORTA_DIR;

char [[address = 0x0018]] TIMER8_CONFIG;
char [[address = 0x0019]] TIMER8_COMPARE;
char [[address = 0x001A]] TIMER8_TOP;
char [[address = 0x001B]] TIMER8_COUNTER;
char [[address = 0x001C]] TIMER8_STATUS;

int millis_count;

void [[isr = 0]] timerinterrupt() {
    TIMER8_STATUS = 0b00000001;
    millis_count = millis_count + 1;
}

int millis() {
    cli();
    int m = millis_count;
    sei();
    return m;
}

char main() {
    millis_count = 0;
    TIMER8_TOP = 211;               /* 27MHz / 128 / 212 ≒ 1ms */
    TIMER8_CONFIG = 0b00111011;
    PORTA_DIR = 0xFF;
    sei();

    int previousMillis = millis();
    char state = 0;
    char porta;
    while (1) {
        int curr = millis();
        if ((curr - previousMillis) > 500) {
            state = !state;
            previousMillis = curr;
        }
        if (state) {
            porta = porta | 0x10;
        } else {
            porta = porta & 0xEF;
        }
        PORTA_OUT = porta;
    }
}
```

## 手書きアセンブリでハンドラを書く

`[[isr]]`（`=N` なし）は正しい形のハンドラを生成しますが、自動配置はしません。
`.org` でベクタを自分で敷く形になります。参考実装は
`tests/fixtures/irq_vector.asm` です。

```asm
.org 0x0000
        jr  start
.org 0x0001
        jr  isr0
.org 0x0005
start:
        ...
isr0:
        push r0          ; 使うレジスタは自分で退避する
        ...
        pop  r0
        reti
```

## デバッグ

シミュレーションでの割り込みは `minc_tb.sv` の `IRQ_TEST` ブロックが
plusargs で駆動します。

```sh
iverilog -g2012 -DSIM -DVERBOSE -DIRQ_TEST -o sim.out minc_h.sv minc_tb.sv
vvp sim.out +irq_cycle=40 +irq_mask=1 +irq_len=6 +irq_period=0
```

| plusarg | 意味 |
| --- | --- |
| `+irq_cycle=N` | N サイクル目から立てる |
| `+irq_mask=M` | どの線を立てるか（`1`=IRQ0, `2`=IRQ1, `4`=IRQ2, `8`=IRQ3） |
| `+irq_len=L` | 何サイクル立て続けるか |
| `+irq_period=P` | `0` = 1回だけ、`>0` = P サイクルごとに繰り返す |

## 制約

- **ネスト（多重割り込み）できません。** `PSR_SHADOW` は1段だけです。
- `irq_in` に同期化回路がありません。CPU と同一クロックドメインの信号を
  つないでください。
- 割り込みハードウェアを持つのは `minc_h.sv` だけです。
- Gowin トップで `irq_line[0]` につながっているのはタイマの `O_OVF_INT` だけです。
  UART の `INTR` は未配線です。
