#ifndef MINCC_CODEGEN_16_H
#define MINCC_CODEGEN_16_H

#include "nodes.h"

// Deliberately does NOT pull in codegen.h (nor ast.h, which includes it): that
// header declares the minc-8 back end's generate()/push_regstack()/... as extern,
// which would collide with this file's own static versions of the same names.
#ifndef NO_EXPECTED_SIZE
#define NO_EXPECTED_SIZE -1
#endif

/*
minc-16 code generation (mincc -16). A separate ISA from minc-8, so this is a
separate back end rather than a mode of codegen.c -- see Hardware.md "### minc-16".

呼び出し規約 (minc-16 ABI)
・r0  : 戻り値 / アドレス計算用スクラッチ (どの文の境界でも死んでいる)
・r1~5: Caller責任側。r1 から上を式評価スタックとして使い、引数もそこに置く
・r6~13: Callee責任側 (プロローグで実際に触った分だけ push する)
・r14 : ベースポインタ (BP) — ソフトウェア規約のみ
・r15 : スタックポインタ (SP) — ハードウェアが push/pop/calr/ret で暗黙に更新

レジスタが16bitになったので char/int/ポインタはすべて1レジスタに収まる。
minc-8 のようなレジスタペアは無く、式評価スタックは常に1本ずつ伸びる。

型のセマンティクス: char はメモリ上でのみ8bitで、レジスタに載った時点で16bitに
ゼロ拡張される (C の整数昇格と同じ)。切り詰めは stb による格納時にだけ起きる。
minc-8 のように演算のたびに8bitへ丸めることはしない — 16bitマシンでそれをやると
マスク用の定数ロードが毎回必要になるため。

すべて static で、外に出るのは m16_generate_top() だけ。codegen.c (minc-8) と
同名の関数を大量に持つので、リンク時に衝突させないための措置。
*/

// Emit the whole translation unit as minc-16 assembly.
void m16_generate_top(Node *code, long count);

#endif // MINCC_CODEGEN_16_H
