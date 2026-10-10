# 签名生成工具

从网易版 `libminecraftpe.so` 的地址反推出**版本无关的字节签名**。

## 背景

网易版 `libminecraftpe.so` 是**剥离过的**（无 `.symtab`），且被 `libunisec` 保护。
虽然 `FuncOffset-*.json` 里有函数名和绝对地址，但直接写死地址一换版本就废。

本工具把「符号 → 绝对地址」转成「符号 → ARM64 字节签名」，运行时扫描定位，
换版本只要签名还能匹配就无需改代码。

## 算法

逐条反汇编 ARM64 指令，把**PC 相关的立即数位**挖成 `?`：

| 指令 | 挖掉的位 |
|---|---|
| `ADRP` | immhi / immlo |
| `B` / `BL` | imm26 |
| `B.cond` / `CBZ` / `LDR (literal)` | imm19 |
| `TBZ` / `TBNZ` | imm14 |
| `ADR` | 立即数 |
| `ADD`/`SUB` imm（且源寄存器来自同段内 ADRP） | imm12 |
| 其余 | 保留（寄存器、结构体偏移、小常量跨版本稳定） |

## 用法

```bash
# 单条验证（生成 3.9.5 的签名 → 套到 3.9.15 地址上校验）
python3 verify2.py FuncOffset-3.9.15.scanned.json low.so pe.so

# 全量导出
python3 gen_all.py FuncOffset-3.9.15.scanned.json low.so pe.so
# -> signatures.json
```

## 实测结果

跨版本（3.9.5 → 3.9.15）签名匹配率 **90%**（62/69 可对照项）。

剩余不匹配的是「同段内多条 ADRP 复用寄存器」等需要真正数据流分析的情况，
实际使用时每条给 2~3 个候选签名按序尝试即可兜住。

## C++ 侧

扫描器在 `app/src/main/cpp/sig/`：

```cpp
sig::Module mcpe("libminecraftpe.so");
uintptr_t p = sig::find(mcpe, "F9 4C C0 00 D6 5F 03 C0");
```
