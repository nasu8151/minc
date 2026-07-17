# MinC : Minimal C CPU

## もくじ

- [MinC : Minimal C CPU](#minc--minimal-c-cpu)
  - [もくじ](#もくじ)
  - [概要](#概要)
  - [How to start](#how-to-start)

## 概要

このプロジェクトは、必要最低限のFPGAリソースでC言語を実行できるCPUソフトコアを開発することを目指します。
このプロジェクトには、CPUのVerilogソース、アセンブラ、および専用Cコンパイラが含まれています。

## How to start

- 必要なソフトウェア
  - `iverilog`(シミュレーションのみ)
  - `Gowin FPGA Designer`(Gowin FPGAでの実行のみ)
  - `Python`(テストの実行に必要)
  - `msys2`(windowsではこれを使用することを推奨)
  - `mingw-w64`(使いたいだけの場合)

```bash
make # do first only

cat <program.c> | ./target/mincc > <target.asm>
cat <target.asm> | ./target/mincasm > <program.hex>
```
