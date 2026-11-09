import time, subprocess, sys, pathlib
def run_batch(cmds, outdir, wait=10):
    # write QEAF.CMD
    open("F:/QEAF.CMD","w",newline="\r\n").write("\n".join(cmds)+"\n")
    subprocess.run(["powershell","-Command","Write-VolumeCache -DriveLetter F"], check=False)
    time.sleep(wait)
    subprocess.run([sys.executable,"scripts/read_rescue_raw.py","--drive","F:","--out",outdir,"--files","RESPONSE.TXT","HOMOLOG.TXT","SCREEN.RAW"], check=False)
    resp = pathlib.Path(outdir,"RESPONSE.TXT").read_text(encoding="utf-8", errors="replace")
    print(resp)
    return resp
print("=== TEST 1 status/help/ls ===")
run_batch(["status","ls","help","echo T1"], "out/report-auto-t1")
print("=== TEST 2 wifi ===")
run_batch(["wifi","wifi net","wifi fw","echo T2"], "out/report-auto-t2")
print("=== TEST 3 rotate/lang ===")
run_batch(["rotate 90","rotate 0","lang vi","lang en","echo T3"], "out/report-auto-t3")
print("=== TEST 4 usb/snap/kmsg ===")
run_batch(["usb charge","snap","kmsg","echo T4"], "out/report-auto-t4")
