#!/usr/bin/env python3
"""从网易版 libminecraftpe.so + FuncOffset 地址表，反推出版本无关的字节签名。

思路：
  1. 符号表拿到 fn -> vaddr
  2. vaddr 经 PT_LOAD 映射成文件偏移，读一段代码
  3. 逐条反汇编 ARM64 指令，把【PC 相关】的立即数位挖掉（打 '?'）
     —— 这些位在换版本时一定会变；其余位（寄存器、结构体偏移、小常量）保持固定
  4. 输出 LeviLaunchroid 那套签名格式： "FD 03 00 91 ? ? ? 94 ..."
"""
import sys, struct
from elftools.elf.elffile import ELFFile

# ---------- 需要打码的 ARM64 PC 相关指令 ----------
def insn_mask(w):
    """返回 (keep_mask, keep_value)：keep_mask 为 1 的位必须匹配。"""
    if (w & 0x9F000000) == 0x90000000:      # ADRP   immhi/immlo
        return 0x9F00001F, w & 0x9F00001F
    if (w & 0x7C000000) == 0x14000000:      # B / BL          imm26
        return 0xFC000000, w & 0xFC000000
    if (w & 0xFF000010) == 0x54000000:      # B.cond          imm19
        return 0xFF00001F, w & 0xFF00001F
    if (w & 0x3B000000) == 0x18000000:      # LDR (literal)   imm19
        return 0xFF00001F, w & 0xFF00001F
    if (w & 0x7E000000) == 0x34000000:      # CBZ / CBNZ      imm19
        return 0xFF00001F, w & 0xFF00001F
    if (w & 0x7E000000) == 0x36000000:      # TBZ / TBNZ      imm14
        return 0xFFF8001F, w & 0xFFF8001F
    if (w & 0x1F000000) == 0x10000000:      # ADR / ADD/SUB imm
        return 0x9F00001F, w & 0x9F00001F
    return 0xFFFFFFFF, w                    # 其余保持固定


def vaddr_to_off(elf, vaddr):
    for seg in elf.iter_segments():
        if seg['p_type'] != 'PT_LOAD':
            continue
        va, sz, off = seg['p_vaddr'], seg['p_filesz'], seg['p_offset']
        if va <= vaddr < va + sz:
            return off + (vaddr - va)
    return None


def load_syms(path):
    with open(path, 'rb') as f:
        elf = ELFFile(f)
        out = {}
        for secname in ('.symtab', '.dynsym'):
            s = elf.get_section_by_name(secname)
            if not s:
                continue
            for sym in s.iter_symbols():
                if sym['st_value'] and sym['st_info']['type'] == 'STT_FUNC':
                    out.setdefault(sym.name, sym['st_value'])
        return out


def read_code(path, vaddr, length):
    with open(path, 'rb') as f:
        elf = ELFFile(f)
        off = vaddr_to_off(elf, vaddr)
        if off is None:
            return None
        f.seek(off)
        return f.read(length)


def gen(path, syms, fn, length=48):
    va = syms.get(fn)
    if not va:
        return None, 'symbol not found'
    data = read_code(path, va, length)
    if not data or len(data) < 16:
        return None, 'read failed'
    # 跳过函数开头的 bti/pac 之类前导指令？先不跳，直接从入口开始
    toks = []
    for i in range(0, min(len(data), length) - 3, 4):
        w = struct.unpack('<I', data[i:i+4])[0]
        keep, val = insn_mask(w)
        for b in range(4):
            bit = 8 * b
            if (keep >> bit) & 0xFF == 0xFF:
                toks.append('%02X' % ((val >> bit) & 0xFF))
            else:
                # 半字节级通配
                hi = '?' if not ((keep >> (bit+4)) & 0xF) else '%X' % ((val >> (bit+4)) & 0xF)
                lo = '?' if not ((keep >> bit) & 0xF) else '%X' % ((val >> bit) & 0xF)
                toks.append(hi + lo)
    return ' '.join(toks), None


if __name__ == '__main__':
    so = sys.argv[1]
    import json
    base = json.load(open(sys.argv[2]))
    syms = load_syms(so)
    print('符号表函数数: %d' % len(syms))
    ok = miss = 0
    for e in base:
        fn = e['fn']
        sig, err = gen(so, syms, fn)
        if sig:
            ok += 1
            if ok <= 5:
                print('\n--- %s' % fn[:70])
                print('    %s' % sig)
        else:
            miss += 1
            if miss <= 5:
                print('\n--- %s  ✗ %s' % (fn[:70], err))
    print('\n生成成功 %d / 失败 %d' % (ok, miss))
