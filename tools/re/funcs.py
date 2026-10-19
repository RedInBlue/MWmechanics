import sys; sys.path.insert(0,'.')
from pe import *
d,b,s=load(sys.argv[1]); a0=int(sys.argv[2],16); a1=int(sys.argv[3],16)
o=va2off(s,a0); e=va2off(s,a1); starts=[]
i=o
while i<e:
    if i%16==0 and d[i-1]==0xCC and d[i]!=0xCC: starts.append(i)
    i+=1
for k,st in enumerate(starts):
    en=starts[k+1] if k+1<len(starts) else e
    j=en
    while d[j-1]==0xCC: j-=1
    print(hex(off2va(s,st)), hex(j-st))
