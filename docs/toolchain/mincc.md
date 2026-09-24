# mincc がサポートする C

`mincc` は C のサブセットを実装した再帰下降コンパイラです。
標準 C の一部ですが、**標準 C と挙動が違う箇所があります**。
特に演算子の優先順位（後述）は要注意です。

```sh
./target/mincc < prog.c > prog.asm 2>/dev/null
```

## いちばん最初に知っておくこと

### コメントが書けません

**プリプロセッサがなく、コメント構文も一切ありません。**
`//` も `/* */` もトークナイザが知らないので、コメントを書くと
`Invalid token` エラーになります。

`#include` / `#define` / `#ifdef` も同様に使えません。

（アセンブリ側には `;` コメントがあるので、`asm("...")` の中には書けます。）

### 関数は使う前に定義する

前方宣言（プロトタイプ）が書けません。関数名は**定義を読んだ時点で**
登録されるので、呼び出しより前に定義しておく必要があります。

```c
char helper() { return 1; }
char main() { return helper(); }   // OK

char main() { return helper(); }   // NG: Undefined or invalid identifyer
char helper() { return 1; }
```

再帰は問題なく書けます（関数名は本体を読む前に登録されるため）。

## 型

| 型 | サイズ | 備考 |
|---|---|---|
| `char` | 1 バイト | |
| `uint8_t` | 1 バイト | `char` と完全に同じ |
| `int` | 2 バイト | リトルエンディアン |
| `void` | 0 | 戻り値専用 |
| `T *` | 2 バイト | ポインタ。多段可（`int **`） |

- **符号はありません。** 内部的にはすべて符号なしとして扱われます。
  比較には符号なし比較命令（`lt`/`ltc`）が使われます。
  `char c = -3;` は `253` として格納されます。
- `char` ↔ `int` の変換は代入・引数渡し・演算のたびに暗黙に行われます
  （1→2バイトはゼロ拡張、2→1バイトは上位バイトを捨てる）。
- 明示的なキャスト構文 `(int)x` は**ありません**。
  幅を変えたいときは一度変数に代入してください。

```c
int add(char a, int b) {
    int aa = a;        // char → int の拡張はここで起きる
    return aa + b;
}
```

**未対応**: `long`, `short`, `unsigned`/`signed` 修飾, `float`/`double`,
構造体, 共用体, 列挙型, `typedef`, 配列, 文字列リテラル（値としては使えない）。

## 文

| 構文 | 対応 |
|---|---|
| 式文 `expr;` | ○ |
| `return expr;` / `return;` | ○ |
| `if` / `else` | ○ |
| `for (init; cond; inc)` | ○（3つとも省略可、`init` に宣言可） |
| `while` | ○ |
| `break` | ○ |
| `{ ... }` ブロック | ○（独立したスコープを作る） |
| `asm("...")` | ○（[inline-asm.md](../guide/inline-asm.md)） |
| `continue` | **×** |
| `do ... while` | **×** |
| `switch` / `case` | **×** |
| `goto` / ラベル | **×** |

`break` は最も内側のループを抜けます。

### スコープ

スコープを作るのは **関数本体**、**`{ }` ブロック**、**`for` の初期化節** です。

```c
char main() {
    char j = 0;
    for (char i = 0; i < 7; i = i + 1) { }   // i は for の中だけ
    char i;                                   // 別の変数（未初期化）
    return i;
}
```

## 演算子

### ⚠ 優先順位が標準 C と違います

実装されている優先順位は、**弱い順**に次のとおりです。

| 順位 | 演算子 | 結合 |
|---|---|---|
| 1（最も弱い） | `=` | 右 |
| 2 | `\|` | 左 |
| 3 | `^` | 左 |
| 4 | `&` | 左 |
| 5 | `&&` | 左 |
| 6 | `\|\|` | 左 |
| 7 | `==` `!=` | 左 |
| 8 | `<` `<=` `>` `>=` | 左 |
| 9 | `+` `-` | 左 |
| 10 | `*` | 左 |
| 11（最も強い） | 単項 `+` `-` `~` `!` `*` `&` | |

標準 C との違いが2つあります。

1. **ビット演算 `| ^ &` が `&& ||` より弱い。**
   C では `a | b && c` は `(a | b) && c` ですが、minc では `a | (b && c)` です。
2. **`&&` が `||` より弱い。**
   C では `a && b || c` は `(a && b) || c` ですが、minc では `a && (b || c)` です。

混ぜて書くときは**必ず括弧を付けてください**。

### 単項演算子の落とし穴

単項 `+` `-` `~` は、オペランドとして `primary`（数値・括弧・識別子・関数呼び出し）
しか取りません。`unary` を再帰的に呼ばないので、**単項演算子を重ねられません**。

```c
-x        // OK
-(-x)     // OK（括弧で primary にする）
--x       // NG: パースできない
-*p       // NG
!x        // OK（! と * と & は unary を再帰的に呼ぶので重ねられる）
```

### 実装されていない演算子

`/` `%` `<<` `>>` `++` `--` `+=` などの複合代入、三項演算子 `? :`、
カンマ演算子、`sizeof`。

除算・剰余・シフトは**命令自体がありません**。必要ならループか、
`mul` を使った定数倍で代用してください（`x * 16` は左4シフト相当）。

### 演算の実装メモ

- `~x` は `0xFF ^ x` に展開されます。**8bit 前提**なので `int` に対しては
  期待どおりになりません。
- `!x` と `&&`/`||` は `chz` 命令（ゼロ判定）を組み合わせて 1/0 を作ります。
  **短絡評価はしません**（両辺とも必ず評価されます）。
- 掛け算は `mul`（下位8bit）/ `mulh`（上位8bit）。`int` 同士の掛け算は
  コード生成が用意されていません。

## ポインタ

```c
char main() {
    char a = 3;
    char *b = &a;
    *b = 5;
    return a;        // 5
}
```

- `&` は変数のアドレスを取ります（ローカル・グローバルとも）。
- `*` はデリファレンス。ポインタ型でないものに `*` を付けるとコンパイルエラー。
- 多段ポインタ（`int **`）も動きます。
- **ポインタ演算（`p + 1` でサイズ分進む）はありません。** `+` は単なる
  2バイト整数の加算です。
- 配列がないので、`&` と `*` は主に「参照渡し」と MMIO のために使います。

## グローバル変数と属性

```c
char counter;              // 0x0100 から自動割り当て
int  total = 0;            // 初期化子は crt0 (__on_entry) で実行される
char [[address = 0x04]] PORTA_OUT;   // メモリマップドI/O
```

### `[[address = N]]`

変数を指定したデータアドレスに置きます。MMIO レジスタを叩くための唯一の
手段です（[cpu/memory-map.md](../cpu/memory-map.md)）。

同じアドレスに別の型で複数の名前を割り当てても構いません。

```c
char [[address = 0x0010]] i2c_presc_l;
char [[address = 0x0011]] i2c_presc_h;
int  [[address = 0x0010]] i2c_presc;   // 16bit まとめてアクセス
```

### `[[isr]]` / `[[isr=N]]`

割り込みハンドラ。詳細は [cpu/interrupts.md](../cpu/interrupts.md) と
[guide/interrupts-howto.md](../guide/interrupts-howto.md)。

```c
void [[isr = 0]] on_timer() {
    TIMER8_STATUS = 1;
    ticks = ticks + 1;
}
```

属性はカンマ区切りで複数書けます（`[[a=1, b=2]]`）。

## 組み込み関数

| 関数 | 内容 |
|---|---|
| `sei()` | `PSR.IE = 1`（割り込み許可） |
| `cli()` | `PSR.IE = 0`（割り込み禁止） |

どちらも**値を返さない文**なので、`char x = sei();` はエラーです。
同名の変数・関数を宣言するとそちらが優先されます（シャドウ可能）。

## インラインアセンブラ

```c
asm("mvi r0,0x5A\n"
    "stm 4,r0\n");
```

文であって式ではありません。詳細は [guide/inline-asm.md](../guide/inline-asm.md)。

## 数値リテラル

| 書き方 | 例 |
|---|---|
| 10進 | `42` |
| 16進 | `0x2A` |
| 2進 | `0b101010` |
| 8進 | `052`（`strtol` の base 0 経由） |

範囲は `-0x10000` 〜 `0xFFFF`。外れると警告が出て 16bit に切り詰められます。

文字リテラル `'a'` は**使えません**（数値で書いてください）。

## その他の制限

- トップレベルの宣言は **255 個まで**（`Node code[256]`）。
- ローカル変数のフレームは **32 バイトまで**。`ldm/stm rp+n` の変位が
  符号付き6bitしかないためです。超えるとコンパイルエラーになります
  （黙って壊れることはありません）。
- グローバル変数の自動割り当てはアドレスを **1バイトずつ**進めます。
  `int` のグローバルを複数置くと重なります（[known-issues.md](../known-issues.md)）。

## エラーメッセージ

エラーはソース位置つきで標準エラーに出ます。

```
char main(){1+}
              ^ Expected number or identifier
```

`mincc` は同じ標準エラーにデバッグログも大量に出すので、
エラーだけ見たいときは末尾を見てください。

## 文法（EBNF）

`mincc/ast.h` 冒頭のコメントが実装と同期した一次資料です。

```
program     = toplevel*
toplevel    = type attr? ident "=" assign ";"
            | type attr? ident "(" params? ")" "{" block
block       = (stmt | decr)* "}"
decr        = type ("[[" attr "]]")? ident_name ("=" expr)? ";"
stmt        = expr ";"
            | "return" expr? ";"
            | "if" "(" expr ")" stmt ("else" stmt)?
            | "for" "(" (decr | stmt)? ";" expr? ";" expr? ")" stmt
            | "while" "(" expr ")" stmt
            | "break" ";"
            | "asm" "(" string+ ")" ";"
            | "{" block
expr        = assign
assign      = bitwise_or ("=" assign)?
bitwise_or  = bitwise_xor ("|" bitwise_xor)*
bitwise_xor = bitwise_and ("^" bitwise_and)*
bitwise_and = and ("&" and)*
and         = or ("&&" or)*
or          = equality ("||" equality)*
equality    = relational ("==" relational | "!=" relational)*
relational  = add ("<" add | "<=" add | ">" add | ">=" add)*
add         = mul ("+" mul | "-" mul)*
mul         = unary ("*" unary)*
unary       = ("+" | "-" | "~") primary
            | ("*" | "&" | "!") unary
            | primary
primary     = num | "(" expr ")" | ident | builtin
type        = ("uint8_t" | "void" | "int" | "char") "*"*
attr        = "address" "=" num | "isr" ("=" num)?
builtin     = ("sei" | "cli") "(" ")"
```

予約語は `return` `if` `else` `for` `while` `int` `uint8_t` `char` `void`
`break` `asm` の11個です。
