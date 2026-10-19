import sys, struct; sys.path.insert(0,'.')
from pe import *
d,b,secs=load('nfsc.exe'); m,mb,msecs=load('mw_speed.exe')
def f(dd,ss,a): return struct.unpack_from('<f',dd,va2off(ss,a))[0]
# MW EyeAccelMax.x per POV entry, keyed by (EyeAccelMin.x, LookAccel.x) of entry[0]
mwmap={}
for i in range(7):
    n,mn,mx,inv,ptr=struct.unpack_from('<IfffI',m,va2off(msecs,0x8ee040+i*0x14))
    key=(round(f(m,msecs,ptr+0x50),3), round(f(m,msecs,ptr+0x70),3), round(f(m,msecs,ptr),3))
    mwmap.setdefault(key,(i,[f(m,msecs,ptr+j*0xb0+0x60) for j in range(n)]))
seen=set()
for i in range(20):
    n,mn,mx,inv,ptr=struct.unpack_from('<IfffI',d,va2off(secs,0xa579b8+i*0x14))
    if f(d,secs,ptr)==0 or ptr in seen: continue
    seen.add(ptr)
    key=(round(f(d,secs,ptr+0x50),3), round(f(d,secs,ptr+0x70),3), round(f(d,secs,ptr),3))
    mi,vals=mwmap.get(key,(None,None))
    print('set %d pov %d data %s -> MW pov %s : %s' % (i//5, i%5, hex(ptr), mi, ', '.join('[%s]=%g'%(hex(ptr+j*0xb0+0x60),vals[j]) for j in range(n)) if vals else '?'))
print('speedhug exact', struct.unpack_from('<10f',d,va2off(secs,0xa57990)), struct.unpack_from('<10f',m,va2off(msecs,0x8ee0cc)))
