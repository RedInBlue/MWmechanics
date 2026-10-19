import sys, struct; sys.path.insert(0,'.')
from pe import *
d,b,secs=load(sys.argv[1])
for s in sys.argv[2:]:
    o=d.find(s.encode()+b'\0'); va=off2va(secs,o)
    refs=[]; i=d.find(struct.pack('<I',va))
    while i!=-1:
        r=off2va(secs,i)
        if r and r<0x9c0000: refs.append(hex(r))
        i=d.find(struct.pack('<I',va),i+1)
    print(s, hex(va), refs)
