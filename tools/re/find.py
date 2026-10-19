import sys; sys.path.insert(0,'.')
from pe import *
for path in sys.argv[2:]:
    d,base,secs = load(path)
    print(path, hex(base), [(n,hex(va),hex(vsz)) for n,va,vsz,_,_ in secs])
    for m in pat(sys.argv[1]).finditer(d):
        print('  match va', hex(off2va(secs,m.start())))
