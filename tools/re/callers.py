import sys, struct; sys.path.insert(0,'.')
from pe import *
d,b,secs=load(sys.argv[1]); tgt=int(sys.argv[2],16)
n,va,vsz,raw,rsz=secs[0]
for i in range(raw, raw+rsz-5):
    if d[i]==0xE8:
        rel=struct.unpack_from('<i',d,i+1)[0]
        src=va+(i-raw)
        if src+5+rel==tgt: print(hex(src))
