import struct
f = open(r"\\.\F:", "rb", buffering=0)
boot = f.read(512)
u16 = lambda b,o: struct.unpack_from("<H", b, o)[0]
u32 = lambda b,o: struct.unpack_from("<I", b, o)[0]
bps = u16(boot,11); spc=boot[13]; res=u16(boot,14); nf=boot[16]; re_=u16(boot,17); spf=u16(boot,22); total=u16(boot,19) or u32(boot,32)
root_off=(res+nf*spf)*bps
rootSec=(re_*32+bps-1)//bps
data_off=root_off+rootSec*bps
print("bps",bps,"spc",spc,"root_off",root_off,"data_off",data_off, "total", total)
f.seek(root_off); root=f.read(re_*32)
for i in range(0,len(root),32):
    e=root[i:i+32]
    if e[0]==0: break
    if e[11]&0x0F==0x0F:
        print("LFN?", e[:32])
        continue
    short=(e[0:8].decode("ascii","replace").rstrip())
    ext=e[8:11].decode("ascii","replace").rstrip()
    name=(short+"."+ext) if ext else short
    print(repr(name), hex(e[11]), "cluster", u16(e,26), "size", u32(e,28))
    if name.upper()=="APPS":
        apps_cluster=u16(e,26)
        apps_size=u32(e,28)
        print("Apps cluster", apps_cluster)
        # read fat
        fat_off=res*bps
        f.seek(fat_off); fat=f.read(spf*bps)
        # chain
        cur=apps_cluster; cls=[]
        while 2 <= cur < 0xFFF8:
            cls.append(cur)
            nxt=u16(fat, cur*2)
            cur=nxt
        print("Apps chain", cls[:10])
        clus_bytes=spc*bps
        for c in cls:
            f.seek(data_off+(c-2)*clus_bytes)
            data=f.read(clus_bytes)
            for j in range(0,len(data),32):
                ee=data[j:j+32]
                if ee[0]==0: break
                if ee[11]&0x0F==0x0F:
                    continue
                s=(ee[0:8].decode("ascii","replace").rstrip())
                x=ee[8:11].decode("ascii","replace").rstrip()
                nm=(s+"."+x) if x else s
                print(" Apps file", repr(nm), "cluster", u16(ee,26), "size", u32(ee,28), "attr", hex(ee[11]))
                # extract first bytes
                if nm.upper().startswith("INSTAL"):
                    f2size=u32(ee,28)
                    fcluster=u16(ee,26)
                    # chain file
                    cur2=fcluster; cls2=[]
                    while 2 <= cur2 < 0xFFF8:
                        cls2.append(cur2)
                        cur2=u16(fat, cur2*2)
                    print(" installer chain len", len(cls2), cls2[:5])
                    # read first 64
                    f.seek(data_off+(cls2[0]-2)*clus_bytes)
                    head=f.read(64)
                    print(" head", head[:32])
