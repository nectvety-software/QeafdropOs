---
name: rescue-channel
description: Vận hành kênh cứu hộ QEAFRESCUE (LUN FAT16 trên violet) qua scripts/send_rescue_cmd.py, scripts/read_rescue_raw.py và scripts/prepare_rescue_flash.py — danh sách verb thật, quy tắc tên 8.3, ràng buộc batch, và những lệnh cấm. Dùng khi cần gửi lệnh, đọc RESPONSE.TXT, hoặc nạp ảnh vào p77.
---

# Kênh cứu hộ QEAFRESCUE

## Violet Homo build (current - QeafdropOs)

 Verbs thật trong `fs/rescue.c` của build QeafdropOs hiện tại:

```
help status wifi sync usb rotate lang echo ls kmsg snap reboot flash
```

- `wifi` : `/sys/class/net` + `wlan0 present` + dmesg wlan
- `wifi net` : `ip link` dạng operstate+addr mỗi netdev
- `wifi dmesg` : lọc sâu wlan/WCN/cfg/mac/wifi/firmware/BSS/SDIO
- `wifi sdio` : `/sys/bus/mmc/devices|drivers`
- `wifi mod` : `/sys/module` (wlan/cfg/mac) + `/lib/modules`
- `wifi fw` : 6 thư mục firmware — trên violet đo `wlan0 present: no`, firmware `absent` (cần modem p82/persist p74; xem PROMPT.md Giới hạn: hakios-xnu-kernel/hakios-metal/hakios-uefi không có Linux WCN3990 cho violet, dipper-xnu chứng minh WCN3990 sống trên XNU khi có fw modem, omarchy-mobile + luma là Linux mainline/Fedora không có profile violet)
- `snap` : scanout thật `SCREEN.RAW` 240x520 RGB565
- `rotate 0|90|180|270|auto`, `lang vi|en|zh`, `flash recovery BOOTIMG.BIN confirm` (chỉ p77)

LUN RAM 32 MiB (`FAT16_VOLUME_SIZE 32*1024*1024`): root có `VQEAF, UserData, Install` + nested (xem PROMPT.md). Mất sau reboot. `.qeapp` bền qua reboot nằm ở initramfs `/apps/*.qeapp`, không phải LUN.

Tài liệu dưới đây mô tả lineage UMOS m48 cũ (init/rescue.c 47 verb, build/send-cmd.ps1) và chỉ còn đúng cho cây cũ `out/tmp/oldsrc/`; trên cây QeafdropOs hiện tại dùng `scripts/prepare_rescue_flash.py` + `scripts/read_rescue_raw.py` như đã dùng trong các lượt nạp p77 gần đây.

Đây là con đường DUY NHẤT để nói chuyện với điện thoại khi UMOS đang giữ USB.
Nó là một LUN SCSI do `init/lun.c` phục vụ, hiện ra ở Windows như ổ `F:` với
nhãn `QEAFRESCUE`. Hộp thư: `QEAF.CMD` (host -> máy, mỗi dòng một lệnh),
`RESPONSE.TXT` (máy -> host).

## ba điều cấm tuyệt đối

1. **KHÔNG bao giờ eject volume cứu hộ.** `-Eject` (bản cũ của `build/send-cmd.ps1`)
   giết medium của LUN và làm chết cả kênh giữa chừng một lượt đo (khuyết tật
   #65, #120). `scripts/send_rescue_cmd.py` không có cờ đó; `Write-VolumeCache`
   là đủ. Đo ngày 2026-10-26.
2. **KHÔNG đọc `/proc/tp_lockdown_info` hay bất kỳ node `/proc/tp_*` nào.**
   Một lần kiểm kê nodes từng làm chết hẳn kênh cứu hộ.
3. **KHÔNG ghi bất kỳ partition nào ngoài p77 (recovery).** p76 là boot slot
   của Android; backup recovery gốc nằm trên PC và không được xoá.

## Tên file phải là 8.3

Đầu đọc phía máy đi duyệt root directory entries và **bỏ qua mảnh tên dài**,
nên `boot image.bin` vô hình với nó dù Explorer vẫn hiện. `scripts/send_rescue_cmd.py`
và `scripts/prepare_rescue_flash.py` từ chối tên không hợp lệ thay vì để máy im
lặng bỏ qua file. Chỉ chữ và số, phần mở rộng 0–3 ký tự, tên gốc ≤ 8.

## Một shot = một batch, không phải một vòng lệnh lẻ

Máy chạy **cả file `QEAF.CMD` trong một lượt** và ghi **một** `RESPONSE.TXT`.
Đó là lý do một ảnh chụp là một batch. Hệ quả thực đo được:

- **Mọi phép đo trên kính phải đánh thức kính TRONG CÙNG BATCH với phép đọc.**
  Round-trip host sang host dài hơn hẹn giờ idle 60 s của shell, nên batch thứ
  hai chụp một tấm kính đã tắt lần hai. Xem `shot_batch(cmds, wake=...)`
  trong [build/autotest.py](file:///D:/QeafivelsOfficial/QeafdropOs/out/tmp/oldsrc/build/autotest.py)
  (bản đầy đủ; cây này chỉ còn `scripts/send_rescue_cmd.py`).
- `scripts/send_rescue_cmd.py --cmd 'a' --cmd 'b'` gửi **một batch**: mỗi `--cmd`
  là một dòng trong `QEAF.CMD`. Đừng gọi nhiều lần cho các lệnh cần chung một
  lượt — mỗi lần gọi là một batch mới và một `RESPONSE.TXT` mới.
- Mỗi batch kết thúc bằng một nonce `tsim status n<N>`
  (`with_nonce()`), vì cổng completeness đếm các echo `### <line>` và hai batch
  giống hệt nhau không phân biệt được — một batch chưa trả lời vẫn mang header
  của batch trước và đọc thành "xong" khi máy chưa hề bắt đầu.

## Đọc lại: phải đọc sector thô

`read_back()` dùng `\\.\F:`. Góc nhìn `F:` đã mount **cũ trên driver này** —
chỉ raw sector nói thật. `build/read-report-raw.ps1` và `build/send-cmd.ps1`
**đã mất** sau lần dựng lại cây; thay thế read-only, chỉ stdlib:

```
python scripts\read_rescue_raw.py --drive F: --out out\report-raw
```

Script mở `\\.\F:`, parse BPB FAT16 + root directory, đi theo chuỗi cluster
và ghi bản copy đúng kích thước. Nó **không ghi và không flush volume** (flush
từ mounted view có thể đẩy directory entry cũ của host xuống máy). Đo thực tế
20:42–20:46: `QEAFBOOT.LOG` trên máy 23 411 → 34 840 B trong khi `F:` mounted
chỉ hiện 1 566 B. Script từ chối volume không có label `QEAFRESCUE`.

**Hỏng đã biết:** script đọc đời cũ (`read-report-raw.ps1`) có thể treo **sau
khi** đã ghi đầy đủ capture — shell không bao giờ thoát nên `ps()` timeout 900 s
trên một câu trả lời hoàn hảo (đo hai lần ngày 2026-11-03: `out/report/m37ack*`,
`out/report/atlit1*`). `scripts/read_rescue_raw.py` viết lại read-only và không
flush, nhưng luật vẫn giữ: khi gặp treo, **đừng gửi lại batch** (nó là write),
hãy parse artifact đã nằm trên đĩa. `build/probe_lit.py --from <file>` tồn tại
chỉ vì thế.

## Mất kênh khác với thất bại

`CHANNEL_LOST` = `no mounted volume labelled QEAFRESCUE` hoặc
`NO drive serving sector 0 with the QEAFRESCUE signature`. Gặp nó thì
`ChannelGone` dừng cả lượt: các shot còn lại là **chưa đo**, không phải đỏ.
Đo 2026-10-31: một lượt mất kênh ghi ~20 failure giống hệt nhau rồi in "6
shots, 114 checks, 25 failures" — một con số trông như kết luận về OS nhưng
chỉ là sợi cáp.

## Verb: 47 lệnh top-level, đúng theo bảng dispatch

Trích nguyên văn từ `strcmp(cmd, "...")` trong
[init/rescue.c](file:///D:/QeafivelsOfficial/QeafdropOs/out/tmp/oldsrc/init/rescue.c)
(bản nguồn đầy đủ; cây này chỉ giữ binary `out/stage/sbin/qeafinit`) — không
phải bản mô tả tự do, và **không** lẫn subcommand.
Chuỗi nào thuộc cha nào, theo đúng dòng dispatch: `tsim` nhận
`start|stop|status|key`; `menu` nhận `up|down|off|go`; `torch` nhận
`status|on|off`; `usb <what> <how>` với `what = status|adb` và
`how = on|off`.

```
apps autolock bootloader capture cat chmod clock coldplug dmesg echo exit
flash glide health help hold install klog lock ls menu mkdir mknod mountall
net poweroff ps reboot recovery reprobe resume rm rmdir screen snap start
status stop swipe sync tap theme torch tsim uninstall usb volume
```

Bảng `--- commands` mà chính volume in ra (`HELP.TXT`/`SUMMARY.TXT`, dựng bởi
`init/lun.c` trong `out/tmp/oldsrc/`) là bản rút gọn để người đọc theo, không
phải bản đầy đủ.

Dùng thường xuyên nhất: `tsim key 150` (đánh thức), `swipe X1 Y1 X2 Y2`,
`tap X Y`, `hold`, `snap` (dump ảnh), `cat PATH [OFFSET LEN]`, `ps`,
`status`, `klog`, `flash recovery <FILE83> confirm`, `reboot`.

**Cạm bẫy khi đọc kết quả:** verb `ps` in PID với tên rỗng (chỉ dòng
`[kthread]` mang chữ). `ps lines naming umos: NONE` **không** có nghĩa UMOS
chết — bằng chứng sống là marker seq, map serial và `dpms`.

## Đường nạp p77

`build/send-cmd.ps1` không còn trong cây sau lần dựng lại. Đường chuẩn bây giờ
là một script stdlib: nó validate ảnh, copy `BOOTIMG.BIN` (tên 8.3) lên volume,
đọc lại so SHA-256, rồi ghi đúng một dòng `QEAF.CMD`:

```powershell
# mặc định dry-run: in plan, không ghi gì
python scripts\prepare_rescue_flash.py --volume F:\
# chỉ khi plan đúng
python scripts\prepare_rescue_flash.py --volume F:\ --commit
```

`--image` mặc định là ảnh m48 hiện tại; truyền ảnh khác khi cần khôi phục một
bản đã lưu. Bằng chứng nạp thành công là hai dòng, không phải một:
`RESPONSE.TXT` trả `flash: written and verified byte for byte`, và
`QEAFBOOT.LOG` của boot đó in
`lun: verified - recovery and BOOTIMG.BIN agree byte for byte over N bytes`.

### BẮT BUỘC: validate ảnh ở host TRƯỚC khi chép BOOTIMG.BIN

Guard phía máy chỉ kiểm `ANDROID!` magic, đích `recovery`, chữ `confirm` và cỡ
partition (`HELP.TXT`). Nó **không** kiểm nội dung. Ngày 2026-11-04 17:36 một
ảnh 97280 byte có magic đúng nhưng "kernel" là ELF64 x86 (`page_size=1024`,
`ramdisk_size=0`, `name=myos`) đã được nạp và verify thành công
(`out/report2/RESPONSE.TXT`); boot `boot-recovery` kế tiếp là bootloop. Ảnh đó
được sinh bởi `build/pack_bootimg.py` từ `VQEAFOS/out/kernel.elf`.

```
python scripts\validate_bootimg.py <ảnh>       # phải in [PASS], exit 0
python scripts\prepare_rescue_flash.py --image <ảnh> --volume F:\ --commit
```

`validate_bootimg.py` đòi `page_size=4096`, kernel gzip >= 1 MiB tại offset
4096, ramdisk gzip không rỗng tại biên page tiếp theo, cặp địa chỉ
`0x00008000/0x01000000` (install) hoặc `0x80008000/0x81000000` (stock/TWRP),
và file <= 67108864 byte. `prepare_rescue_flash.py` từ chối mọi ảnh không PASS,
mặc định dry-run, và không bao giờ `-Eject`.

**Ảnh mặc định cần nạp:**
`out/boot-qeaf-violet-m48-noandroid.img` (23 851 008 byte, PASS,
sha256 `e2ff7df1c5e94bfe1b1e147722be970b53b1d387efc171233d4bb0143e3331c6`).

Đây là `m48`: kernel + DTB stock violet (`out/reference/stock-violet/boot.img`),
`out/stage` với 66 theme `.vqeaf` trong `/Themes`, và `sbin/qeafinit` bản
`m47-storage-noandroid` (360 704 B) dựng volume QEAFRESCUE với 5 thư mục
`Documents`, `Downloads`, `Music`, `Pictures`, `Videos`. Init này không mang
chuỗi "Android" nào.

`scripts/strip_android_from_image.py --all` thay 9 lần `Android` -> `System `
trên 5 entry cpio: `./bin/umos` (3), `./bin/qeaf-wifi` (3),
`./bin/qeaf-report` (1), `./etc/qeaf/fstab` (1), `./etc/qeaf/ueventd.cfg` (1)
— đúng 63 byte. Kernel blob giữ nguyên từng byte, cmdline/địa chỉ/page size
không đổi, 428 cpio entry còn nguyên. Token giao thức `ANDROID!` không bị sửa.
`tests/strip_android_image_test.py` (48 checks) chứng minh các bất biến này;
`make guard-test` chạy nó cùng `tests/validate_bootimg_test.py` (35) và
`tests/gui_no_android_test.py` (1) — 84 checks.

Ảnh nguồn trước khi vá: `out/boot-qeaf-violet-m48.img` (23 838 720 byte,
sha256 `30ed191dcc5c80e3aecda7bfb5da6a03450fd0dbe5e861a5f46d8d5c94bf36ff`).
Mọi ảnh `boot-qeaf-p77-install-m*` cũ đã bị xoá (43 file); đừng trích số đo
của chúng cho bản m48.

`prepare_rescue_flash.py` và `scripts/restore_p77.ps1` đều trỏ tới ảnh m48 này.
Vẫn chỉ ghi `p77/recovery`, không đụng `p76`.

**Không được** tự đóng gói `out/qeafinit.elf` hay bất kỳ ELF userspace nào vào
header `ANDROID!`: đó không phải kernel, dù magic có đúng.

## Paths phía máy

| Ký hiệu | Giá trị |
|---|---|
| `MARKER` | `/run/qeaf-tsim-map` |
| `MAP_CANDIDATES` | `/cache/umos/map.hex`, `/tmp/umos/map.hex` |
| `UMOS_LOG` | `/cache/umos.log` |
| `GLASS_STATE` | `/sys/class/drm/card0-DSI-1/dpms` |
| backlight | `/sys/class/backlight/{panel0-backlight,backlight}/brightness` |

Panel violet: **1080x2340**, dump theo scale 15 → 11232 điểm lấy mẫu.
