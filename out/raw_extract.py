import struct, hashlib, pathlib
f = open(r"\\.\F:", "rb", buffering=0)
boot = f.read(512)
u16 = lambda b,o: struct.unpack_from("<H", b, o)[0]
u32 = lambda b,o: struct.unpack_from("<I", b, o)[0]
bps = u16(boot,11); spc=boot[13]; res=u16(boot,14); nf=boot[16]; re_=u16(boot,17); spf=u16(boot,22)
root_off=(res+nf*spf)*bps
rootSec=(re_*32+bps-1)//bps
data_off=root_off+rootSec*bps
clus_bytes=spc*bps
fat_off=res*bps
f.seek(fat_off); fat=f.read(spf*bps)
def chain(first):
    cur=first; out=[]
    while 2 <= cur < 0xFFF8 and len(out)<200:
        out.append(cur)
        cur=u16(fat, cur*2)
        if cur in out: break
    return out
# find INSTAL cluster 86
inst_cluster=86
inst_size=263606
cls=chain(inst_cluster)
print("chain", len(cls), cls[:3], cls[-3:])
buf=b""
for c in cls:
    f.seek(data_off+(c-2)*clus_bytes)
    buf+=f.read(clus_bytes)
buf=buf[:inst_size]
print("extracted", len(buf))
pathlib.Path("out/device-installer.raw").write_bytes(buf)
h1=hashlib.sha256(buf).hexdigest()
host=pathlib.Path("out/installer.qeapp").read_bytes()
h2=hashlib.sha256(host).hexdigest()
print("device sha256", h1)
print("host sha256  ", h2)
print("match", buf==host)
print("host len", len(host), "dev len", len(buf))
print("head", buf[:16])
# also check if this is QEAPP/2 format?
# header
print("magic", buf[:8])
# try list qeapp structure
if buf[:6]==b"QEAPP2":
    print("QEAPP/2 format")
else:
    import zipfile, io
    try:
        z=zipfile.ZipFile(io.BytesIO(buf))
        print("ZIP", z.namelist()[:10])
    except Exception as e:
        print("not zip", e)
