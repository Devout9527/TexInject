#!/usr/bin/env python3
"""从 3.9.5 地址生成签名 -> 直接套到 3.9.15 地址上验证（免全盘扫描，秒级）"""
import json, struct, sys
from elftools.elf.elffile import ELFFile

def insn_mask(w):
    if (w & 0x9F000000) == 0x90000000: return 0x9F00001F, w & 0x9F00001F  # ADRP
    if (w & 0x7C000000) == 0x14000000: return 0xFC000000, w & 0xFC000000  # B/BL
    if (w & 0xFF000010) == 0x54000000: return 0xFF00001F, w & 0xFF00001F  # B.cond
    if (w & 0x3B000000) == 0x18000000: return 0xFF00001F, w & 0xFF00001F  # LDR literal
    if (w & 0x7E000000) == 0x34000000: return 0xFF00001F, w & 0xFF00001F  # CBZ
    if (w & 0x7E000000) == 0x36000000: return 0xFFF8001F, w & 0xFFF8001F  # TBZ
    if (w & 0x1F000000) == 0x10000000: return 0x9F00001F, w & 0x9F00001F  # ADR
    return 0xFFFFFFFF, w

class SO:
    def __init__(self, p):
        self.f = open(p,'rb')
        self.segs=[(s['p_vaddr'],s['p_filesz'],s['p_offset'])
                   for s in ELFFile(self.f).iter_segments() if s['p_type']=='PT_LOAD']
    def read(self, va, n):
        for a,sz,off in self.segs:
            if a <= va < a+sz:
                self.f.seek(off+(va-a)); return self.f.read(n)
        return None

def build(sos, va, nbytes=48):
    """返回 [(mask, value)] 与原始字节"""
    d = sos.read(va, nbytes)
    if not d: return None, None
    out=[]
    for i in range(0, len(d)-3, 4):
        w=struct.unpack('<I', d[i:i+4])[0]
        k,v=insn_mask(w)
        for b in range(4):
            sh=8*b
            km=(k>>sh)&0xFF; vm=(v>>sh)&0xFF
            out.append((km, vm))
    return out, d

def check(pat, son, va):
    d = son.read(va, len(pat))
    if not d or len(d)<len(pat): return False, 'read fail'
    for i,(km,vm) in enumerate(pat):
        if (d[i] & km) != vm: return False, 'byte %d: got %02X want %02X&%02X' % (i, d[i], vm, km)
    return True, 'ok'

if __name__=='__main__':
    base=json.load(open(sys.argv[1]))
    so5=SO(sys.argv[2]); so15=SO(sys.argv[3])
    ok=bad=skip=0; fails=[]
    for e in base:
        a,b=e.get('ptr_395'),e.get('ptr_3915')
        if not a or not b: skip+=1; continue
        va5=int(a,16) if isinstance(a,str) else a
        va15=int(b,16) if isinstance(b,str) else b
        pat,_=build(so5,va5)
        if not pat: skip+=1; continue
        good,why=check(pat,so15,va15)
        if good: ok+=1
        else:
            bad+=1; fails.append((e['fn'],why))
    print('可验证 %d 条 | 签名跨版本匹配 %d | 不匹配 %d | 跳过 %d' % (ok+bad, ok, bad, skip))
    if fails:
        print('\n--- 不匹配的前 8 条 ---')
        for fn,why in fails[:8]: print('  %-62s %s' % (fn[:62], why))
    # 展示几个生成的签名
    print('\n--- 生成的签名样例 ---')
    n=0
    for e in base:
        a=e.get('ptr_395')
        if not a: continue
        va=int(a,16) if isinstance(a,str) else a
        pat,d=build(so5,va)
        if not pat: continue
        toks=[]
        for i in range(0,len(d)-3,4):
            w=struct.unpack('<I',d[i:i+4])[0]; k,v=insn_mask(w)
            for bb in range(4):
                sh=8*bb
                hi='%X'%((v>>(sh+4))&0xF) if (k>>(sh+4))&0xF else '?'
                lo='%X'%((v>>sh)&0xF) if (k>>sh)&0xF else '?'
                toks.append(hi+lo)
        print('  %-58s %s' % (e['fn'][:58], ' '.join(toks[:8]) + ' ...'))
        n+=1
        if n>=5: break
