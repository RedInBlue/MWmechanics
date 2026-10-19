import sys, struct, re; sys.path.insert(0,'.')
from pe import *
d,b,secs=load(sys.argv[1]); off=int(sys.argv[2],16)
n,va,vsz,raw,rsz=secs[0]; text=d[raw:raw+rsz]; o=struct.pack('<I',off)
# fstp/fst dword [reg+disp32] (D9 98-9F / D9 90-97), mov [reg+disp32],reg (89 80-BF), mov [reg+disp32],imm (C7 80-87)
pats={'fstp':rb'\xD9[\x98-\x9F]'+re.escape(o),'fst':rb'\xD9[\x90-\x97]'+re.escape(o),'mov r':rb'\x89[\x80-\xBF]'+re.escape(o),'mov imm':rb'\xC7[\x80-\x87]'+re.escape(o)}
for k,p in pats.items():
    for m in re.finditer(p,text,re.S): print(k, hex(va+m.start()))
