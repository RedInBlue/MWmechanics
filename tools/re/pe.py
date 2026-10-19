import sys, re, struct
def load(path):
    d = open(path,'rb').read()
    pe = struct.unpack_from('<I', d, 0x3c)[0]
    nsec = struct.unpack_from('<H', d, pe+6)[0]
    optsz = struct.unpack_from('<H', d, pe+20)[0]
    base = struct.unpack_from('<I', d, pe+24+28)[0]
    secs=[]
    off = pe+24+optsz
    for i in range(nsec):
        name = d[off:off+8].rstrip(b'\0').decode()
        vsz, va, rawsz, raw = struct.unpack_from('<IIII', d, off+8)
        secs.append((name, base+va, vsz, raw, rawsz))
        off += 40
    return d, base, secs
def off2va(secs, o):
    for n,va,vsz,raw,rsz in secs:
        if raw <= o < raw+rsz: return va + (o-raw)
def va2off(secs, v):
    for n,va,vsz,raw,rsz in secs:
        if va <= v < va+rsz: return raw + (v-va)
def pat(s):
    return re.compile(b''.join(b'.' if t=='?' else re.escape(bytes([int(t,16)])) for t in s.split()), re.S)
