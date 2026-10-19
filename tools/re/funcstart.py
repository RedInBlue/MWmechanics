import sys, struct; sys.path.insert(0,'.')
from pe import *
d,b,secs=load(sys.argv[1])
for a in sys.argv[2:]:
    a=int(a,16); o=va2off(secs,a)
    while not (d[o-1]==0xCC and o%16==0): o-=1
    start=off2va(secs,o)
    # refs to start as pointer (vtables) or call targets
    refs=[]; i=d.find(struct.pack('<I',start))
    while i!=-1: refs.append(hex(off2va(secs,i))); i=d.find(struct.pack('<I',start),i+1)
    print(hex(a),'func start',hex(start),'ptr refs',refs[:8])
