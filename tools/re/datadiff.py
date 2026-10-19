import sys, struct, difflib; sys.path.insert(0,'.')
from pe import *
def region(path, a0, a1):
    d,b,s=load(path); o=va2off(s,a0); n=(a1-a0)//4
    out=[]
    for i in range(n):
        raw=struct.unpack_from('<I',d,o+i*4)[0]; f=struct.unpack_from('<f',d,o+i*4)[0]
        if 0x400000<=raw<0xc00000: tok='ptr'
        elif raw==0: tok='0'
        elif 1e-6<abs(f)<1e7: tok='%.5g'%f
        else: tok='i:%#x'%raw
        out.append((a0+i*4,tok))
    return out
A=region('mw_speed.exe',0x8f8900,0x8f8e40); B=region('nfsc.exe',0xa63380,0xa63980)
sm=difflib.SequenceMatcher(a=[t for _,t in A], b=[t for _,t in B], autojunk=False)
for tag,i1,i2,j1,j2 in sm.get_opcodes():
    if tag=='equal': print(f'  = {i2-i1} same  MW {hex(A[i1][0])}  NFSC {hex(B[j1][0])}  [{" ".join(t for _,t in A[i1:i2])[:150]}]')
    else: print(f'  {tag}: MW {hex(A[i1][0]) if i1<len(A) else ""} {[t for _,t in A[i1:i2]]}  ->  NFSC {hex(B[j1][0]) if j1<len(B) else ""} {[t for _,t in B[j1:j2]]}')
