import sys, struct, re; sys.path.insert(0,'.')
from pe import *
d,b,secs=load(sys.argv[1]); lo,hi=int(sys.argv[2],16),int(sys.argv[3],16)
n,va,vsz,raw,rsz=secs[0]
# mov dword [reg+disp8], imm32 : C7 4x dd imm ; mov dword [reg], imm32: C7 0x imm
for m in re.finditer(rb'\xC7([\x40-\x47])(.)(....)|\xC7([\x00-\x07])(....)', d[raw:raw+rsz], re.S):
    if m.group(1):
        imm=struct.unpack('<I',m.group(3))[0]; disp=m.group(2)[0]
    else:
        imm=struct.unpack('<I',m.group(5))[0]; disp=0
    if lo<=imm<hi: print(hex(va+m.start()), 'this+%#x ='%disp, hex(imm))
