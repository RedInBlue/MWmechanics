import sys, struct; sys.path.insert(0,'.')
from pe import *
d,base,secs = load(sys.argv[1])
for spec in sys.argv[2:]:
    a,n = spec.split(':'); a=int(a,16); n=int(n)
    o = va2off(secs,a)
    vals = struct.unpack_from('<%df'%n, d, o)
    print(hex(a), ' '.join('%.4g'%v for v in vals))
