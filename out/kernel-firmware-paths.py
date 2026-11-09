"""What firmware paths does the kernel we actually ship search?

The ROM's dmesg brings the ADSP up at 3.1 s, before any Android userspace exists,
so the image came from the kernel's own firmware search list. Our boot reuses that
kernel byte for byte but mounts vendor at /mnt/vendor and modem at /mnt/modem, so
if the built-in list is the Android one ("vendor/firmware", "firmware/image",
"system/etc/firmware"), the very same request fails here for a reason that has
nothing to do with radios: the path is not there.

Symbol names and proc paths are the wrong things to look for; these are rodata
literals, so a hit means the code that uses them is compiled in. ASCII output, so
the report survives a cp1252 console.
"""
import struct
import sys
import zlib

IMG = sys.argv[1] if len(sys.argv) > 1 else "out/reference/boot.img"

MARKERS = [
    # drivers/base/firmware_loader/main.c: the search list, in order, as literals.
    ("fwpath", b"vendor/firmware"),
    ("fwpath", b"system/etc/firmware"),
    ("fwpath", b"vendor/etc/firmware"),
    ("fwpath", b"firmware"),
    ("fwpath", b"firmware/image"),
    ("fwpath", b"/lib/firmware"),
    ("fwpath", b"/lib/firmware/updates"),
    ("fwpath", b"modem"),
    # the WCN3990/icnss image names the glue asks for
    ("wcn", b"mdsp"),
    ("wcn", b"WLAN.PFP_FW"),
    ("wcn", b"wlanmdsp"),
    ("wcn", b"qca_cld3"),
    ("wcn", b"wcn3990"),
    ("wcn", b"wcn36xx"),
    ("wcn", b"qcom,wcn-cpuss"),
    ("wcn", b"bdwlan"),
    ("wcn", b"regdb.bin"),
    ("wcn", b"wlan_mac.bin"),
    ("wcn", b"WCNSS_qcom_cfg.ini"),
    # the ADSP loader's own words
    ("adsp", b"adsp.mdt"),
    ("adsp", b"msm-adsp-loader"),
    ("adsp", b"qcom,adsp-loader"),
    ("adsp", b"subsys-pil-tz"),
    ("adsp", b"pil-tz"),
    ("adsp", b"Power/Clock ready interrupt received"),
    ("adsp", b"Brought out of reset"),
]


def inflate(blob):
    magic = blob[:4]
    if magic[:2] == b"\x1f\x8b":
        d = zlib.decompressobj(16 + zlib.MAX_WBITS)
        raw = d.decompress(blob) + d.flush()
        return raw, "gzip"
    if magic == b"\x04\"M\x18":
        import lz4.frame
        return lz4.frame.decompress(blob), "lz4"
    return blob, "stored"


data = open(IMG, "rb").read()
hdr = data[:4096]
u32 = lambda o: struct.unpack("<I", hdr[o:o + 4])[0]
page = u32(36) or 4096
ksize = u32(8)
raw, how = inflate(data[page:page + ksize])
print("%s: kernel %d bytes, %s -> vmlinux %d bytes" % (IMG, ksize, how, len(raw)))

for group, needle in MARKERS:
    n = raw.count(needle)
    print("%-6s %-40s %s %d" % (group, needle.decode("ascii", "replace"),
                                "HIT " if n else "absent", n))
