import sys, struct; sys.path.insert(0,'.')
from pe import *
names=['EyeDur','LookDur','FovDur','UpDur','UpAccel','UpAccelMin','UpAccelMax','EyeAccel','EyeAccelMin','EyeAccelMax','LookAccel','LookAccelMin','LookAccelMax','ForwardDur']
def pov(d,secs,a):
    o=va2off(secs,a); f=struct.unpack_from('<44f',d,o)
    out=['%s=%.3g'%(names[i],f[i]) for i in range(4)]
    for k in range(10):
        v=f[4+k*4:4+k*4+3]; out.append('%s=(%s)'%(names[4+k],','.join('%.3g'%x for x in v)))
    return ' '.join(out)
path, base, count = sys.argv[1], int(sys.argv[2],16), int(sys.argv[3])
d,b,secs=load(path)
for i in range(count):
    a=base+i*0x14
    n,mn,mx,inv,ptr=struct.unpack_from('<IfffI',d,va2off(secs,a))
    print('table[%d] @%s num=%d range=%.3g..%.3g data=%s'%(i,hex(a),n,mn,mx,hex(ptr)))
    if n>4 or va2off(secs,ptr) is None: print('   (bad)'); continue
    for j in range(n):
        print('   [%d] '%j + pov(d,secs,ptr+j*0xb0))
