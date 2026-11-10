---
name: umos-gates
description: Thang gates của VQEAF_OS — chạy đúng lệnh, hiểu mỗi cổng chứng minh gì và KHÔNG chứng minh gì, kèm setup build trên Windows (msys make, ZIG_GLOBAL_CACHE_DIR) và luật trung thực của báo cáo. Dùng trước khi claim bất kỳ thứ gì đã build / đã xanh / đã sửa.
---

# Thang gate của VQEAF_OS

## Setup bắt buộc trên host này

```bash
export PATH="/c/msys64/usr/bin:$PATH"      # make KHÔNG có trên PATH mặc định
```

Mọi target host-gate tự pin `ZIG_GLOBAL_CACHE_DIR=$(OUT)/zig-global` trong
chính Makefile — **đừng bỏ nó**: zig spawn bởi `make` không tới được Windows
AppData và chết `AppDataDirUnavailable` trước khi đọc một file nguồn; shell
chạy ngoài `make` thì tới được. Vì lý do đối xứng, `make` không nhận env var từ
caller, nên mọi thứ cần env phải pin trong Makefile.

`make check` build object thật với `-Wall -Wextra` (zig cc không có
`-fsyntax-only`).

## Chạy cả thang

Chỉ những target dưới đây tồn tại trong `Makefile` của cây này:

```bash
make check              # 13 objects, biên dịch sạch
make guard-test         # ảnh p77 + GUI không chữ Android + bản vá chữ
make bootui-render      # fastboot/recovery/confirm trên framebuffer thật
make wifi-check         # seam nl80211 + hai màn Wi-Fi
make bt-check           # seam HCI + settings hub
make power-check        # power_supply + dòng sạc trên màn khóa
make motion-check       # nền động zoom + blur + dim
make storage-check      # volume QEAFRESCUE: 5 thư mục, đọc lại hai lần
```

`make` **không** có `umos-verify`, `touch-check`, `backlight-check`,
`local-time-check`, `dirty-flush-check` trong cây này; `scripts/verify_umos.py`,
`build/autotest_selftest.py` và `build/autotest.py` cũng không tồn tại — đừng
gọi chúng. Bản đầy đủ của những gate đó chỉ còn trong `out/tmp/oldsrc/`.

## Mỗi cổng chứng minh gì, và cái nó KHÔNG chứng minh

| Cổng | Số đo gần nhất (artifact) | Chứng minh | Không chứng minh |
|---|---|---|---|
| `make check` | 13 objects — `out/t/gate-m47-check.log` | mọi `.c` của thiết bị biên dịch với `-nostdinc -ffreestanding -Wall -Wextra` | chạy đúng, hay boot được |
| `make guard-test` | 84 = 35 + 1 + 48 — `out/t/gate-validate-bootimg-m48.log`, `out/t/gate-gui-no-android-m48.log`, `out/t/gate-strip-android-m48.log` | ảnh p77 hợp lệ về cấu trúc, không còn chữ "Android" trong nguồn runtime, và bản vá chữ chỉ đổi đúng 63 byte trên 5 entry cpio | rằng ảnh chạy được trên kính — phải smoke-test bằng boot thật |
| `make bootui-render` | 49 checks — `out/t/gate-m47-bootui.log` | fastboot/recovery/confirm vẽ đúng trên RGB565 1080x2340 | panel thật hiện đúng màu |
| `make wifi-check` | 82 checks — README | parser nl80211 đọc blob đúng wire, rfkill ghi thật, hai màn Wi-Fi | rằng card Wi-Fi thật bắt được AP |
| `make bt-check` | 72 checks — `out/t/gate-m48-bt.log:105` | seam HCI + settings hub, khung "off" là control âm | rằng Bluetooth thật bắt được thiết bị |
| `make power-check` | 16 checks — README | `power_supply` đọc qua `core/sysio.c`, dòng "Charging 84%" | pin thật đang sạc |
| `make motion-check` | 44 checks — README | zoom+blur+dim chạy như một ramp thật trên panel 1080x2340 | cảm giác mượt trên kính |
| `make storage-check` | 31 checks (C) + 31 (Python) — `out/t/gate-storage.log` | `fat16.c` dựng `VQEAF,UserData,Install` + nested qua `fat16_mkdir_in`, hai reader độc lập đọc lại | rằng Windows mount hiện đúng tên dài (đo riêng) |

Bài học `dirty-flush-check` vẫn giữ dù gate đó không còn trong cây: host BSP
present cả tấm, nên không cổng host nào thấy được một flush rỗng — mà trên điện
thoại flush rỗng là kính đen dù mọi screenshot thề nó đã vẽ.

## Ba luật khi viết một gate mới

1. **Gate phải tự chứng minh nó quan sát.** Sample rỗng, một batch chỉ một bucket,
   A/B một phía = dụng cụ hỏng chứ không phải kết quả. Luôn có **negative control
   trong cùng lượt chạy**; một chuẩn so sánh không tồn tại thì gate xanh vô nghĩa.
   Đếm số màu chỉ nói *có mực*, không nói *bố cục đúng áp phích* → ảnh crop phải
   được người mở ra nhìn.
2. **Gate phải mang vật lý của thiết bị.** Host gate và kính disagree thường vì
   gate thiếu trục thời gian (hẹn idle 60 s, round-trip host ~1 phút). Mang cả
   biến thể nhanh + chậm kèm wait, và nói rõ bằng chứng thiết bị đến từ uinput,
   từ digitizer hay từ ngón thật.
3. **Revert để chứng minh gate đỏ.** Sau khi viết gate cho một bug thật, chạy nó
   trên bản code cũ: nếu vẫn xanh thì gate không bắt được gì. (`dirty-flush-check`
   đỏ 9 check trên bản trước khi sửa.)

## Luật trung thực khi báo cáo

- **Không claim đã chạy một gate; không claim ánh sáng, điểm ảnh, chạm, mở khóa
  hay đã sửa** nếu không có bằng chứng tương ứng.
- Mọi con số phải trace tới artifact, kèm `đường dẫn:dòng`. Một con số thuộc về
  input mà nó đo — không được tái dùng số của boot cũ cho boot mới.
- Mất kênh cứu hộ (`ChannelGone`) nghĩa là các bước còn lại **chưa đo**, không
  phải đỏ.
- Kết luận "không thấy gì" khi connector `Off` là sai loại: bo đó
  `disp_flush()` return sớm khi panel blank, nên ảnh chụp kính tối không trả lời
  về khả năng nhìn.
- Host green suite **không** nghĩa là bản đóng gói/chạy được: phải smoke-test
  chính binary trên thiết bị thật.

## Ảnh và artifact nằm đâu

| Đường dẫn | Nội dung |
|---|---|
| `out/t/*.log` | stdout của từng lượt gate, mang suffix boot (`-m37`, `-m38c`) |
| `out/autotest/at*/map-8x.png` | ảnh dump từ framebuffer, bản 1/15 để nhìn |
| `out/report/<stem>RESPONSE.TXT` | capture thô của một batch rescue |
| `out/report-flash-m48/` | RESPONSE/QEAFBOOT của lượt nạp m48 (bằng chứng flash + verify) |
| `out/m48-final/`, `out/m48-themes/`, `out/m48-sd/` | RESPONSE/QEAFBOOT của boot m48: status, `ls /Themes`, `ls /sd` |
| `out/boot-qeaf-violet-m48-noandroid.img` | **ảnh OS hiện tại** (23 851 008 B, sha256 `e2ff7df1…`), bản duy nhất được nạp |
| `out/boot-qeaf-violet-m48.img` | nguồn trước khi vá chữ (23 838 720 B, sha256 `30ed191d…`); gate so sánh với nó |
| `out/qeaf-initramfs.cpio.gz` | initramfs dựng từ `out/stage` (`python scripts/mk_initramfs.py`) |
| `out/scratch/` | log thô rig giữ giữa các lượt |

Suffix `-mN` **là** danh tính boot: một con số measurement chỉ được trích từ
artifact cùng boot. Docs dẫn số thì dẫn cả file và số dòng.


## Violet Homo gates (QeafdropOs, current)

Current image is `out/boot-qeafdropos-violet.img` (homo-init PID1). Host gates:

```powershell
$env:PATH="C:\msys64\usr\bin;$env:PATH"
make violet-core-check      # panel_scale_test (540x1170 no bars, 1170x540, stride 1088) + validate_bootimg + audit_rescue_channel + identity
make rescue-channel-check   # fs/rescue.c + p77 slot, 39 checks (incl. wifi sub-verbs text)
make storage-check          # 31 checks, VQEAF/UserData/Install + nested fat16_mkdir_in + Python reader
make radio-firmware-check   # WCN3990 staging report + state text (2 Python cases + host probe)
make check                  # legacy compile, 21 objects
```

Tra cứu 2026-11-10: hakios-xnu-kernel (XNU POCO X5 Pro, không có violet), hakios-metal (gộp vào kernel, Adreno 642L), hakios-uefi (Mu-Silicium, fw phải lấy từ xbl máy mình), luma (Fedora/GTK, không có violet), dipper-xnu (SDM845+WCN3990 Wi-Fi sống trên XNU khi có fw modem, gần violet nhất nhưng là XNU không phải Linux), omarchy-mobile (Arch mainline v7.3-rc6, fw từ factory image, gần nguyên tắc violet nhưng kernel 4.14 không phải mainline).

Rescue verbs (fs/rescue.c, QeafdropOs): `help status wifi sync usb rotate lang echo ls kmsg snap reboot flash`. `wifi` alone = /sys/class/net + dmesg wlan; `wifi net|dmesg|sdio|mod|fw` = ip-link / deep dmesg / mmc-sdio / module / 6 firmware dirs. On violet measured: module wlan+cfg80211 loaded, `wlan0` absent, firmware dirs absent, dmesg `read firmware checksum failed`.
