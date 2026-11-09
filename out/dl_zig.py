import urllib.request, pathlib, zipfile, shutil
url = "https://ziglang.org/download/0.13.0/zig-windows-x86_64-0.13.0.zip"
out = pathlib.Path("out/zig.zip")
print("downloading", url)
req = urllib.request.Request(url, headers={"User-Agent": "Mozilla/5.0"})
data = urllib.request.urlopen(req, timeout=120).read()
out.write_bytes(data)
print("downloaded", len(data))
dest = pathlib.Path("out/toolchain")
dest.mkdir(parents=True, exist_ok=True)
with zipfile.ZipFile(out) as z:
    z.extractall(dest)
print("extracted")
print(list((dest).rglob("zig.exe"))[:2])
out.unlink()
