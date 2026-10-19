import sys; sys.path.insert(0,'.')
from pe import *
d,b,secs=load(sys.argv[1])
for a in sys.argv[2:]:
    o=va2off(secs,int(a,16)); print(a, d[o:d.find(b'\0',o)].decode(errors='replace'))
