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
    while 2 <= cur < 0xFFF8:
        out.append(cur)
        nxt=u16(fat, cur*2)
        if nxt in out: break
        cur=nxt
    return out
def read_chain(first, size):
    cls=chain(first)
    buf=b""
    for c in cls:
        f.seek(data_off+(c-2)*clus_bytes)
        buf+=f.read(clus_bytes)
        if len(buf)>=size: break
    return buf[:size]
f.seek(root_off); root=f.read(re_*32)
apps_cluster=7
cls_apps=chain(apps_cluster)
files=[]
for c in cls_apps:
    f.seek(data_off+(c-2)*clus_bytes)
    data=f.read(clus_bytes)
    for j in range(0,len(data),32):
        ee=data[j:j+32]
        if ee[0]==0: break
        if ee[11]&0x0F==0x0F: continue
        s=ee[0:8].decode("ascii","replace").rstrip()
        x=ee[8:11].decode("ascii","replace").rstrip()
        nm=(s+"."+x) if x else s
        if nm in (".",".."): continue
        sz=u32(ee,28); cl=u16(ee,26)
        files.append((nm,cl,sz))
# build host hash map
host_map={}
for p in list(pathlib.Path("out/apps").glob("*.qeapp"))+[pathlib.Path("out/installer.qeapp")]:
    h=hashlib.sha256(p.read_bytes()).hexdigest()
    host_map[h]=p.name
print(f"{'device 8.3':<14} {'size':>8} {'device sha256[:16]':<18} {'host match':<16}")
ok=0; total=0
for nm,cl,sz in sorted(files):
    buf=read_chain(cl,sz)
    h=hashlib.sha256(buf).hexdigest()
    host=host_map.get(h, "MISMATCH")
    total+=1
    if host!="MISMATCH": ok+=1
    print(f"{nm:<14} {sz:>8} {h[:16]:<18} {host:<16} {'.'+str(sz)+'B'}")
print(f"verified {ok}/{total}")
# specific required
for need in ["settings.qeapp","recovery.qeapp"]:
    hp=pathlib.Path("out/apps")/need
    h=hashlib.sha256(hp.read_bytes()).hexdigest()
    print(need, h, "in device?", h in host_map, "device has", need.upper()[:6])
