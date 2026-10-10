#!/usr/bin/env python3
"""用 FuncOffset 里的【绝对地址】直接反推字节签名（.so 已剥符号，不查名字）。"""
import sys, json, struct
from elftools.elf.elffile import ELFFile

def insn_mask(w):
    if (w & 0x9F000000) == 0x90000000: return 0x9F00001F, w & 0x9F00001F   # ADRP
    if (w & 0x7C000000) == 0x14000000: return 0xFC000000, w & 0xFC000000   # B/BL
    if (w & 0xFF000010) == 0x54000000: return 0xFF00001F, w & 0xFF00001F   # B.cond
    if (w & 0x3B000000) == 0x18000000: return 0xFF00001F, w & 0xFF00001F   # LDR literal
    if (w & 0x7E000000) == 0x34000000: return 0xFF00001F, w & 0xFF00001F   # CBZ
    if (w & 0x7E000000) == 0x36000000: return 0xFFF8001F, w & 0xFFF8001F   # TBZ
    if (w & 0x1F000000) == 0x10000000: return 0x9F00001F, w & 0x9F00001F   # ADR
    return 0xFFFFFFFF, w

class SO:
    def __init__(self, path):
        self.f = open(path, 'rb')
        elf = ELFFile(self.f)
        self.segs = [(s['p_vaddr'], s['p_filesz'], s['p_offset'])
                     for s in elf.iter_segments() if s['p_type'] == 'PT_LOAD']
    def off(self, va):
        for base, sz, off in self.segs:
            if base <= va < base + sz: return off + (va - base)
        return None
    def read(self, va, n):
        o = self.off(va)
        if o is None: return None
        self.f.seek(o); return self.f.read(n)

def gen_sig(so, va, nbytes=48):
    data = so.read(va, nbytes)
    if not data or len(data) < 16: return None
    toks = []
    for i in range(0, len(data) - 3, 4):
        w = struct.unpack('<I', data[i:i+4])[0]
        keep, val = insn_mask(w)
        for b in range(4):
            sh = 8 * b
            hi = '%X' % ((val >> (sh+4)) & 0xF) if (keep >> (sh+4)) & 0xF else '?'
            lo = '%X' % ((val >> sh) & 0xF) if (keep >> sh) & 0xF else '?'
            toks.append(hi + lo)
    return ' '.join(toks)

def scan(so, sig, expect=None):
    """在 .so 里找签名，返回所有命中地址。"""
    pat = []
    for t in sig.split():
        if t == '??': pat.append((0, 0))
        else:
            m = v = 0
            if t[0] != '?': m |= 0xF0; v |= int(t[0],16)<<4
            if t[1] != '?': m |= 0x0F; v |= int(t[1],16)
            pat.append((m, v))
    hits = []
    for base, sz, off in so.segs:
        so.f.seek(off); data = so.f.read(sz)
        step = max(1, len(pat))
        for i in range(0, len(data) - len(pat) + 1, 1):
            ok = True
            for j,(m,v) in enumerate(pat):
                if (data[i+j] & m) != v: ok = False; break
            if ok:
                hits.append(base + i)
                if len(hits) > 20: return hits
    return hits

if __name__ == '__main__':
    base = json.load(open(sys.argv[1]))
    so_old = SO(sys.argv[2])       # 签名从这里生成
    so_new = SO(sys.argv[3])       # 在这里验证（应该也能扫到）
    key_old, key_new = sys.argv[4], sys.argv[5]
    import time
    ok = bad = tested = 0
    for e in base:
        a, b = e.get(key_old), e.get(key_new)
        if not a or not b: continue
        va_old = int(a, 16) if isinstance(a, str) else a
        va_new = int(b, 16) if isinstance(b, str) else b
        sig = gen_sig(so_old, va_old)
        if not sig: continue
        tested += 1
        t0 = time.time()
        hits = scan(so_new, sig)
        hit = any(abs(h - va_new) < 64 for h in hits)
        if hit and len(hits) <= 3:
            ok += 1
            if ok <= 6:
                print('\n✅ %s' % e['fn'][:66])
                print('   %s' % sig[:110])
                print('   新地址 %x -> 命中 %s' % (va_new, [hex(h) for h in hits[:3]]))
        else:
            bad += 1
            if bad <= 4:
                print('\n❌ %s  命中%d处 %s' % (e['fn'][:60], len(hits), [hex(h) for h in hits[:3]]))
        if tested >= 12: break
    print('\n本次测试 %d 条: 成功 %d / 失败 %d' % (tested, ok, bad))
