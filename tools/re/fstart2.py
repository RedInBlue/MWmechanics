import sys; sys.path.insert(0,'.')
from pe import *
d,b,s=load(sys.argv[1])
for a in sys.argv[2:]:
    o=va2off(s,int(a,16))
    while not (o%16==0 and d[o-1]==0xCC and d[o-2]==0xCC): o-=1
    e=va2off(s,int(a,16))
    while not (d[e]==0xCC and d[e+1]==0xCC): e+=1
    print(a,'func',hex(off2va(s,o)),'..',hex(off2va(s,e)),'size',hex(e-o))
