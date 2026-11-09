import struct, zlib, re
data=open('out/reference/stock-violet/boot.img','rb').read()
hdr=data[:4096]
u32=lambda o: struct.unpack('<I', hdr[o:o+4])[0]
page=u32(36) or 4096
ksize=u32(8)
import zlib
d = zlib.decompressobj(16 + zlib.MAX_WBITS)
raw = d.decompress(data[page:page+ksize]) + d.flush()
print("vmlinux", len(raw))
for m in re.finditer(rb'[ -~]{5,}', raw):
    s=m.group().decode()
    ls=s.lower()
    if 'firmware' in ls:
        print(s)
