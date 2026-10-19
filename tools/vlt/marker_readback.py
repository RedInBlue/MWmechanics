"""Read attribute values out of a compiled NFS database (attributes.bin) with the "marker" trick.

Attribulator can apply ModScripts to compiled databases but (in this setup) can't export them.
So: apply a no-op script and a marker script (each field set to a unique value 1000+i) to copies
of the same GLOBAL folder, find each marker in the marked attributes.bin, and read the float at the
same offset in the no-op output. Only valid when both outputs have the same size (no fields added).

Usage:
  Attribulator.CLI apply-script-bin -i GLOBAL -o out_noop -p CARBON -s noop.nfsms
  Attribulator.CLI apply-script-bin -i GLOBAL -o out_mark -p CARBON -s mark.nfsms
  python3 marker_readback.py out_noop/main/attributes.bin out_mark/main/attributes.bin labels.txt [first_marker]

labels.txt: one label per line, in the same order as the markers (marker = first_marker + line index,
first_marker defaults to 1001). Extra hits whose no-op value equals the marker are coincidences.
"""
import struct
import sys

noop_path, mark_path, labels_path = sys.argv[1:4]
first = int(sys.argv[4]) if len(sys.argv) > 4 else 1001

R = open(noop_path, "rb").read()
M = open(mark_path, "rb").read()
labels = open(labels_path).read().split()
print("sizes", len(R), len(M), "(must match)")

for i, label in enumerate(labels):
    marker = float(first + i)
    pat = struct.pack("<f", marker)
    hits, j = [], M.find(pat)
    while j != -1:
        hits.append(j)
        j = M.find(pat, j + 1)
    vals = [struct.unpack_from("<f", R, h)[0] for h in hits] if len(R) == len(M) else "size differs"
    real = [v for v in vals if v != marker] if isinstance(vals, list) else vals
    print(f"{label:24s} offsets={[hex(h) for h in hits]} value={real}")
