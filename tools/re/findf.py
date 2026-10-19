import sys, struct; sys.path.insert(0,'.')
from pe import *
d,b,secs=load(sys.argv[1])
def hits(bs):
    out=[];i=d.find(bs)
    while i!=-1: out.append(off2va(secs,i)); i=d.find(bs,i+1)
    return out
tbl=struct.pack('<ff',2.5,15.0)
print('SmoothRPMDecel {2.5,15}:', [hex(x) for x in hits(tbl) if x])
for name,v in [('0.55',0.55),('0.45',0.45)]:
    hs=[x for x in hits(struct.pack('<f',v)) if x]
    print(name, [hex(x) for x in hs][:20])
    # code refs: fmul ds:addr  (D8 0D addr) or fld (D9 05)
    for a in hs:
        for op in (b'\xd8\x0d',b'\xd9\x05',b'\xd8\x05',b'\xd8\x25',b'\xd8\x2d'):
            for r in hits(op+struct.pack('<I',a)):
                if r and r<0x9c0000: print('   ref', hex(r), op.hex(), hex(a))
