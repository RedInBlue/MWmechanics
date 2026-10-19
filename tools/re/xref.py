import sys, struct, re; sys.path.insert(0,'.')
from pe import *
d,b,s=load(sys.argv[1]); lo=int(sys.argv[2],16); hi=int(sys.argv[3],16)
n,va,vsz,raw,rsz=s[0]; text=d[raw:raw+rsz]
res={}
for i in range(len(text)-4):
    v=struct.unpack_from('<I',text,i)[0]
    if lo<=v<hi: res.setdefault(v,[]).append(va+i)
for k in sorted(res): print(hex(k), [hex(x) for x in res[k][:8]], len(res[k]))
