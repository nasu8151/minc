# minc ユーザーズマニュアル ハードウェア編

## 命令表

| Mnemonic | Machine code | Description                   |
| -------- | ------------ | ----------------------------- |
| push     | 0ii          | SP++, [SP] <- ii              |
| add      | 400          | [SP-1] <- [SP-1] + [SP], SP-- |
| sub      | 500          | [SP-1] <- [SP-1] + [SP], SP-- |
| mul      | 600          | [SP-1] <- [SP-1] + [SP], SP-- |
| halt     | 800          | Stops the CPU                 |
