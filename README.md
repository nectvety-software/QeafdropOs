# QeafdropOs

Nguồn UMOS/violet cho Xiaomi Redmi Note 7 Pro: `qeafinit.elf` là ELF userspace
aarch64 chạy trên nhân cứu hộ, không phải nhân Linux.

## Trạng thái

Cây này chứa toàn bộ nguồn UMOS (`core/`, `hal/`, `bsp/violet/`, `gui/`,
`apps/`, `fs/`, `init/`), các cổng host trong `tests/`, **và lõi OS mới nhất
trong `homo/`** — bản Homo OS/LVGL 9.4 port sang violet, có driver DRM/KMS và
USB gadget thật (xem mục riêng bên dưới).

`out/qeafinit.elf` (cây UMOS gốc) chỉ là compile + link: tự nó không dựng được
panel hay LUN trên máy thật. Ảnh p77 hiện tại phải được đóng gói từ
`homo/build/violet/homo-init` qua `homo/scripts/package_violet.py`.

## Cấu trúc nguồn chuẩn

QeafdropOs dùng **một cây nguồn chuẩn**: `homo/` là hệ điều hành đang được
build; `core/` và `fs/` ở root là các module dùng chung mà image shipping liên kết.
Các module nguồn thật trước đây được nối bằng junction từ `VQEAF_OS/` đã
được chuyển thành thư mục vật lý trong `homo/framework/`, `homo/services/`,
`homo/platform/violet/` và `homo/scripts/`. Cây `VQEAF_OS/` cũ không còn là
source root; phần trùng lặp được lưu ngoài repo tại
`D:\QeafivelsOfficial\QeafdropOs-archive\VQEAF_OS-legacy-duplicates`. Installer
cũ được giữ tại `homo/legacy/installer/`, không tham gia build. Sơ đồ module và
lệnh build chuẩn xem tại [`ARCHITECTURE.md`](ARCHITECTURE.md).

## Bản danh tính OS — QeafdropOs không phải Android

QeafdropOs (`QEAF_OS_STRING = "QeafdropOs 0.1.0"`, nguồn duy nhất
`qeaf_version.h`) là **một hệ điều hành hoàn toàn riêng biệt**, không phải
Android, không phải bản ROM/port của Android và không chạy trên nhân Linux của
Android.

- **Hệ thống riêng:** PID 1 là `homo-init` (ELF aarch64-musl tĩnh), tự mount
  hệ thống tệp, điều khiển backlight, khởi động panel qua DRM/KMS, chạy vòng
  lặp LVGL và kênh cứu hộ `QEAF.CMD`. Không có `init.rc`, `zygote`,
  `system_server` hay thành phần Android nào.
- **Định dạng theme riêng:** các gói `.vqeaf` (XML palette, phân tích và
  trước xem bằng `app/core/themes_os.py`), lưu trong `/Themes` trên volume
  QEAFRESCUE.
- **Định dạng ứng dụng riêng:** ứng dụng đóng gói thành tệp `.qeapp` (trên FAT16
  8.3 hiển thị như `MYNOTE.QEA`); VQEAF Tool cài và gỡ qua trang Tools. Đây
  không phải APK và không dùng runtime Android.
- **Token `ANDROID!`:** chỉ là magic byte trong header ảnh boot p77, giữ nguyên
  để công cụ nhận diện cấu trúc ảnh. Nó không làm hệ điều hành này thành Android,
  cùng cách chữ `android` (viết thường) trong target-triple của trình biên dịch
  chỉ là mã nhận diện nền tảng, không phải nhãn thương hiệu.

## Ảnh OS hiện tại

Trong `out/` còn **mười** ảnh boot OS: `m48`/`m49`/`m49b`/`m50` hai bản mỗi
loại, `boot-qeaf-violet-known-good.img` và `boot-qeafdropos-violet.img` (dòng
Homo hiện tại). Toàn bộ ảnh `boot-qeaf-p77-install-m*` cũ đã bị xoá (43 file,
~830 MB) để không ai nạp nhầm một bản đã chết. Các `.img` còn lại là artifact
test/splash, không phải OS.

| Ảnh | Vai trò | Kích thước | sha256 |
|---|---|---|---|
| `out/boot-qeafdropos-violet.img` | **LÕI OS MỚI NHẤT (đang chạy, 2026-11-10)**: Homo OS/LVGL, `homo-init` PID 1, DRM/KMS `SETCRTC` + configfs `f_mass_storage`, kênh `QEAF.CMD` (`fs/rescue.c`). Shell dọc `540x1170` và ngang `1170x540` full-screen không viền (2x panel), app ngang `1170x540` full ngang, Cài đặt chạy canvas shell responsive. Home: chạm mở, nhấn giữ lâu hiện X gỡ/kéo thả, Home bar mở đa nhiệm. Cài đặt: light 1 cột, cỡ chữ lớn, Ngôn ngữ 8 hàng, Wi-Fi WPA2/WPA3-SAE; verb `wifi net/dmesg/sdio/mod/fw` probe driver/firmware (đo thật: module `wlan`+`cfg80211` có, `wlan0` absent, firmware dirs absent). Đồng hồ `%H:%M` mỗi giây. SDCARD `VQEAF/UserData/Install` qua `fat16_mkdir_in`, 12 `.qeapp` mới đúng chuẩn ở `/apps` initramfs qua reboot. Gate: `violet-core-check` (540x1170 no bars + stride 1088 + validate + audit), `rescue-channel-check` 39/0, `storage-check` 31/0, `make check` 21. | 28 590 080 B | `c7edf67701aa00274103e247962f2039ae8e1cad2b15fc8e03805ef26b3ee3cf` |
| `out/boot-qeaf-violet-m50-noandroid.img` | bản C lineage 2026-11-08, cũng có panel DRM + gadget, audit `[PASS]` — phương án dự phòng nếu lõi Homo chưa ổn trên phần cứng | 23 990 272 B | `0dc5b54f35f1526dfbc9633b0f6a64087e2cc0424688fc1df6fc8b9502f58566` |
| `out/boot-qeaf-violet-m49b-noandroid.img` | Terminal + Trình khởi động + fix `explain gesture/nav` — validate `[PASS]`, **chưa nạp** | 24 170 496 B | `8e1a3aecf9af699caf878f5c85ba114304634631f168ffb08b937406cff995cb` |
| `out/boot-qeaf-violet-m49b.img` | nguồn trước khi vá chữ của m49b | 24 154 112 B | `cde607b7f81c166bad899d9bdead01f481263f9ca6ae447a064e9dce1563aa70` |
| `out/boot-qeaf-violet-m49-noandroid.img` | **đã nạp 2026-11-07** (2 bằng chứng, `out/report2/`) nhưng dựng trước fix recents — **đừng nạp lại bản này** | 24 170 496 B | `bf7093763b7d163d65853d3341c1c7daf28fe567f0fd41fd5815dc4316d1c245` |
| `out/boot-qeaf-violet-m49.img` | nguồn trước khi vá chữ của m49 | 24 154 112 B | `0ee9760d7ea9ba7e29da69620d685688d02af16f4592ad22540926ab5718381e` |
| `out/boot-qeaf-violet-m48-noandroid.img` | **đã nạp p77 2026-11-08** nhưng **không đưa được LUN lên** — xem "Sự cố 2026-11-08" | 23 961 600 B | `715bc54a471ea02919d1941736bcc896e6b089734f5f78cd3763eaeb89c810a1` |
| `out/boot-qeaf-violet-m48.img` | nguồn trước khi vá chữ; gate so sánh với nó | 23 945 216 B | `60a5237a174192dde398e852ca8fb30783522e5c14cbf6b7c09ec0426e1e44bf` |

`m48` = kernel + DTB thật của violet (`out/reference/stock-violet/boot.img`),
`out/stage` (66 theme `.vqeaf` trong `/Themes`) và `sbin/qeafinit` bản
rebuild 2026-11-06 (1 399 288 B): init dựng volume QEAFRESCUE với **năm**
thư mục `Documents`, `Downloads`, `Music`, `Pictures`, `Videos` và không mang
chuỗi "Android" nào. `strip_android_from_image.py --all` thay 9 lần `Android`
bằng `System ` (63 byte, 5 entry cpio); token giao thức `ANDROID!` giữ nguyên.
Target-triple `android` (lowercase) trong `.rodata` của Zig compiler được giữ
nguyên — đây là platform identifier, không phải brand label.

Bằng chứng bản m48 chạy được, cùng một boot:

- `out/report-flash-m48/RESPONSE.TXT:2` — `flash: written and verified byte for
  byte`
- `out/report-flash-m48/QEAFBOOT.LOG:736` — `lun: verified - recovery and
  BOOTIMG.BIN agree byte for byte over 23851008 bytes`
- `out/m48-final/QEAFBOOT.LOG:3` — `VQEAF OS 0.1.0 starting (pid 1)`
- `out/m48-final/QEAFBOOT.LOG:16` — `lun: volume built ... (5 storage folders)`
- `out/m48-themes/RESPONSE.TXT` — `ls /Themes` trả đủ 66 file `.vqeaf`
- `out/m48-sd/RESPONSE.TXT` — `ls /sd` = `Themes`, `System`, `Music`

`m49`/`m49b` = m48 + cửa sổ `Terminal` (11 secret code ánh xạ vào thao tác
thật) + cửa sổ `Trình khởi động`. Khác biệt duy nhất giữa hai bản: m49b mang
fix `explain gesture/nav` (m49 gọi `wm_recents_open()` sai nghĩa query nên mở
Recents và nuốt mọi lệnh sau — tái lập 5/5 trên host, `out/scratch/m49-d2.log`).

Bằng chứng bản m49 đã ghi vào p77, cùng một lượt:

- `out/report2/RESPONSE.TXT:2` — `flash: written and verified byte for byte`
- `out/report2/QEAFBOOT.LOG` — `lun: verified - recovery and BOOTIMG.BIN agree
  byte for byte over 24170496 bytes`

Sau khi ghi, `reboot` đã được gửi qua `QEAF.CMD`; kênh cứu hộ chưa quay lại
(máy chỉ còn là `Generic Mass-Storage` 0 sector — trạng thái #73/#65/#120), nên
**m49b chưa được nạp** và chưa có bằng chứng Terminal/Trình khởi động trên kính.

### Bố cục dọc theo tỉ lệ iOS (2026-11-09)

Mốc tham chiếu là ba ảnh iPhone chủ máy đưa (Home có widget, iOS 18/iOS 26 home,
và Control Center iOS 26). Lưới Home dọc lấy tỉ lệ của màn 390x844 pt rồi nhân
theo canvas 540x1170; `homo/framework/ui/ios_metrics.c` chỉ chứa phân số và
không phụ thuộc LVGL, nên `make ios-metrics-check` đo đúng những số mà desktop
đọc lúc bố cục (`out/t/ios-metrics-qeafdropos.log`, 41 check):

| Thành phần | Công thức (mốc 390x844) | Trên canvas 540x1170 |
|---|---|---|
| Số cột | 4 | 4 |
| Cạnh icon | 60/390 | 83 px |
| Khoảng cách cột | 32/390 | 44 px |
| Lề trái/phải | (W - 4*icon - 3*gap)/2 | 38 px |
| Tâm cột 0 / bước cột | — | 79 px / 127 px |
| Cỡ nhãn / khoảng nhãn | 11/390, 4/390 | 15 px, 6 px |
| Bề rộng nhãn | icon + 30 | 125 px |
| Đỉnh hàng 1 / bước hàng | 79/844, 107/844 | 110 px, 148 px |
| Số hàng trước dock | 6 | 6 |
| Dock | 82/844, inset 11, bo 44 | cao 114, đỉnh 1028 |
| Home bar | 134/390 x 5/844 | 186 x 7, y 1152 |
| Status bar | 33/844, chữ 16/390 | cao 46, chữ 22, pad 14/28 |

Landscape giữ nguyên lưới cũ 800x480 (6 cột, icon 64, gap 69, lề 35, dock đỉnh
386, home bar y470) — gate khoá cả hai nhánh, cộng check "icon lớn dần theo bề
ngang" và "phân số giữ trong 1/1000" để một phân số không lặng lẽ thành hằng số.

Nhãn Home không bị cắt: `lv_tiny_ttf_set_size` đặt `line_height =
scale*(ascent - descent + line_gap)` và `base_line = scale*(line_gap - descent)`;
`lv_draw_label.c:612` vẽ glyph tại `y + (line_height - base_line) - box_h -
ofs_y`, nên ở 15 px Liberation Sans baseline rơi ~13 px và descender hết ~16 px
trong hộp 17 px. Nhãn còn auto-height, nên cột nhãn cao 83 + 6 + 17 = 106 px <
148 px bước hàng. Vệt "bị cắt" trong ảnh dump 240x520 chỉ là phép lấy mẫu
nearest-neighbour 4.5:1 — font 15 px chỉ còn ~5 pixel thô.

Control Center dọc: backdrop là snapshot Home thu nhỏ 1/5, ba lượt Gaussian
{1,4,6,4,1} ngang+dọc, tint ~0.75 có dither 4x4; sheet `0x252a32` đục. iOS
không làm mờ status bar, nên từ bản này `capture_background()` ẩn status bar của
Home đúng lúc `lv_snapshot_take_to_draw_buf()` rồi bật lại ngay
(`hachimi_desktop_set_status_bar_visible()`,
`homo/framework/ui/control_center.c`), để backdrop không còn một dòng "09:41"
nhòe nằm ngay trên hàng giờ thật của sheet.

**Chưa có nhân chứng trên kính cho Control Center**: shell cứu hộ của cây này
(`fs/rescue.c`) chỉ có `help status sync usb rotate lang echo ls kmsg snap
reboot flash` — không có verb chèn input, nên không mở CC từ xa được. Ảnh
`out/report-raw/cc-top.png` / `cc-bottom.png` là bản **trước** khi sửa; bản đã
nạp chưa được chụp lại, và việc mở CC cần một ngón thật trên kính.

Bằng chứng cùng boot (`out/report-raw/`, chụp 09:52):

- `HOMOLOG.TXT:2` — `prefs: language 0, orientation portrait, auto 0 (p77 slot)`
- `HOMOLOG.TXT:11` — `drm: connector 28 connected, 1080x2340, encoder 27 crtc 107`
- `HOMOLOG.TXT:15` — `drm: verified - crtc 107 scans our fb 188 at 1080x2340`
- `HOMOLOG.TXT:20` — `ui: QeafdropOs desktop created at 540x1170`
- `RESPONSE.TXT:6` — `snap: wrote SCREEN.RAW (249600 bytes, RGB565 240x520)`
- `home-m50.png3x.png` — Home dọc: status bar 09:52, 4 cột, 13 app, nhãn tiếng
  Việt có dấu, dock + home bar.

### Lượt m50 (2026-11-08 15:06): ảnh mới đã vào p77, chưa lên USB

- **Sửa pipeline build**: `build/build.ps1` không biên dịch/link
  `bsp/violet/panel_drm.c` và `bsp/violet/usb_gadget_bind.c`, nên mọi ELF do
  nó tạo ra đều thiếu driver panel/gadget. Đã thêm cả hai. `libc_stubs.c` cũng
  thiếu `open/read/close/reboot/syscall` mà `fs/rescue.c` dùng cho verb
  `kmsg`/`reboot`; đã thêm shim `svc #0` cho aarch64 (`openat` 56, `close` 57,
  `read` 63, `reboot` 142).
- **Ảnh**: `out/boot-qeaf-violet-m50-noandroid.img`, 23 990 272 B, sha256
  `0dc5b54f35f1526dfbc9633b0f6a64087e2cc0424688fc1df6fc8b9502f58566`.
  `validate_bootimg.py` PASS; audit PASS (`init_panel` 4, `init_gadget` 6,
  `gadget_configfs` 21, `gadget_udc` 3); cmdline giữ
  `force_initramfs prefer_initramfs`.
- **Nạp**: `restore_p77.ps1 -Image ... -Execute -ConfirmPartitionWrite` →
  `Sending 'recovery' (23428 KB) OKAY`, `Writing 'recovery' OKAY`,
  `Rebooting into recovery OKAY`; log
  `out/logs/restore-p77-20261108-150650.log`.
- **Đọc lại từng byte**: qua `adb root`, `sha256` của đúng 23 990 272 byte đầu
  `/dev/block/by-name/recovery` = `0dc5b54f…`, khớp tuyệt đối với file host.
- **Kết quả boot**: `fastboot reboot recovery` lại khởi động `boot` (cmdline
  `qeaf.nopanel … skip_initramfs`), nên lần đầu chưa nạp p77. Sau
  `adb reboot recovery`, máy nạp p77 nhưng sau 2 phút **không có adb, không có
  QEAFRESCUE, không có node USB** (`Get-Disk` chỉ còn "Generic Mass-Storage /
  No Media"). Vì vậy **chưa xác nhận được giao diện trên kính**; cần đưa máy
  về fastboot bằng tay để xem log hoặc khôi phục.

### Lượt m51 (2026-11-09): responsive 9:16 / 16:9, đồng hồ, ngôn ngữ

- **Hai canvas responsive**: dọc 360x780 (K=3 trên 1080x2340) và ngang 780x360
  (K=3 trên 2340x1080). Home xếp 5x5 + dock ở dọc, 9x3 + dock phải ở ngang;
  lock đưa đồng hồ sang trái và thông báo sang phải; hub Settings xếp 2x3.
  Các màn hình cũ (boot menu, Wi-Fi/Bluetooth, Features, USB sheet) giữ nguyên
  thiết kế 360x780 qua `bui_begin_fit()`/`bui_view_fit()`; bàn phím tự căn
  giữa bằng `bui_view_legacy()` để không cộng origin hai lần.
- **Đồng hồ**: `core/clock.c` đọc `/sys/class/rtc/rtc0/since_epoch`, cache 1 s.
  Không có RTC thì lùi về uptime và nhãn đổi thành `UP SINCE BOOT`, nên không
  giả vờ đó là giờ tường. Statusbar home/lock dùng chung nguồn này.
- **Ngôn ngữ**: hàng thứ 6 trong Settings mở sheet Language 8 mục
  (VI/EN/JA/ZH/ES/DE/KO + Automatic). EN/VI/ES/DE có chuỗi thật; JA/ZH/KO chọn
  được nhưng mặt chữ 5x7 chỉ có ASCII nên chuỗi UI vẫn là tiếng Anh
  (`i18n_effective()` trả EN, đã đo trong gate).
- **Lưu cấu hình**: `core/prefs.c` ghi `/etc/qeaf.conf` dạng `key=value`
  (`language`, `orientation`, `auto_rotate`), chỉ ghi khi một giá trị thực sự
  đổi; `init` nạp trước khi mở shell và áp dụng cả ngôn ngữ lẫn độ xoay.
  Khi auto-rotate bật, hướng do cảm biến tạo ra **không** bị ghi đè vào file.
- **Bản bền vững trên p77**: vì `/` là initramfs trong RAM, mỗi lần đổi còn
  ghi một record `QEAFPRF1` (version, độ dài, CRC32, cùng nội dung key=value)
  vào slot 64 KiB ở **cuối p77** (`64 MiB - 64 KiB`), rồi đọc lại so từng byte.
  Boot đọc slot trước; CRC sai hoặc slot trắng thì rơi về `/etc/qeaf.conf`.
  Ảnh p77 chỉ ghi `size` byte từ offset 0, nên slot sống qua reboot và qua lần
  nạp ảnh thường (~24–28 MiB); chỉ ảnh lớn chạm tới đuôi mới xoá nó.
- **Homo dùng cùng store**: `homo-init` nạp slot sau khi `devnodes` tạo
  `/dev/mmcblk0p77` và **trước khi bind panel**, nên độ xoay đã lưu được áp
  ngay từ frame đầu; mỗi vòng lặp đồng bộ lại khi `QEAF.CMD rotate` đổi hướng.
- **Giao diện VI/EN**: `homo/framework/ui/i18n.c` giữ catalog chuỗi hệ thống
  và một hook trong `lv_label`, nên mọi label mới được dịch mà không phải sửa
  từng app. Settings có trang Language; chọn ngôn ngữ sẽ dựng lại desktop ngay
  và `prefs_sync` ghi lựa chọn xuống p77. Desktop shell, Settings, Control
  Center và tên ứng dụng đã có bản dịch; nội dung chưa có trong catalog vẫn
  fallback về tiếng Trung.
- **Gate host**: `make prefs-test` 17/17, `prefs-boot-test` 4 + 7
  (hai tiến trình: ghi slot rồi xoá `/etc` và nạp lại), `language-check` 31/31,
  `keyboard-check` 36/36,
  `bt-check` 72/72, `power-check` 16/16, `motion-check` 44/44,
  `bootui-render` 54/54, `features-check` 70/70, `guard-test` PASS;
  `build/build.ps1` biên dịch + link `out/qeafinit.elf` thành công.
- **Chưa lên kính**: điện thoại vẫn offline (không adb/fastboot/USB node), nên
  ảnh p77 **chưa được đóng gói lại và chưa nạp**; giao diện ngang/dọc mới chỉ
  được xác nhận bằng framebuffer host, không phải trên panel.

## Lõi OS 2026-11-09: Homo OS / LVGL trên violet

`homo/` là **dữ liệu mới nhất**: một desktop LVGL 9.4 hoàn chỉnh (desktop,
dock, control centre, 14 app: Files, Books, Calculator, Clock, Calendar,
Weather, Maps, Music, Messages, Mail, Browser, LED, Settings...) được port sang
chính con violet này. Chi tiết: `homo/ARCHITECTURE.md` và
`homo/platform/violet/README.md`.

Đây là phần sửa tận gốc hai lỗi đã làm màn hình trống và mất LUN ngày 8/11:

| Thiếu trước đó | Có trong lõi mới |
|---|---|
| `g_display` NULL → `gfx_init()` không chạy | `homo/platform/violet/drm_kms.c`: `GETRESOURCES` / `GETCONNECTOR` / `CREATE_DUMB` / `ADDFB` / `SETCRTC` trực tiếp, vì kernel **không có fbdev** |
| `usb_gadget_add_lun()` chỉ ghi log | `homo/platform/violet/usb_lun.c`: tạo `/run/qeaf-lun.img` 32 MiB `MAP_SHARED`, format FAT16, dựng `/config/usb_gadget/homo`, bind UDC — kernel `f_mass_storage` phục vụ thật |
| không có `/dev` (không devtmpfs) | `devnodes.c` tạo node từ `/sys/class/*/dev` |
| `hal_rtc()->now_ms()` NULL | `main_violet.c` là PID 1, tự chủ mount / backlight / LUN / DRM / vòng lặp LVGL |
| xoay màn hình chỉ là transform ảo | `panel_scale.c`: **hai canvas** — shell dọc 540x1170 (2.0x, lấp kín 1080x2340) và app 800x480 (2.25x → 1800x1080, bar 270 px trên mặt 2340x1080 khi quay 90° CW), **cảm ứng dùng phép nghịch đảo cùng bảng số nguyên Q16** |

`homo-init` là ELF aarch64-musl tĩnh 7,9 MB, biên dịch thẳng cả
`fs/fat16.c`, `fs/storage.c`, `fs/rescue.c`, `core/sysio.c`, `core/log.c` của
cây này, nên kênh `QEAF.CMD` / `RESPONSE.TXT` / `DEVINFO.TXT` vẫn là kênh đã
test. **Một nguồn identity duy nhất**: `qeaf_version.h` →
`QEAF_OS_STRING = "QeafdropOs 0.1.0"`, có mặt trong `homo-init`, banner cứu
hộ, `HELP.TXT` và verb `status`.

### `make violet-core-check`

Gate mới gom bốn bằng chứng, không tin vào tên file:

1. biên dịch + chạy `homo/tests/panel_scale_test.c` — đúng 4 góc xoay, 90
   tile/khung, mọi pixel letterbox đen, touch round-trip sai ≤1 px, và stride
   1088 px của DRM pitch cho ra đúng khung, không tô vào 8 px padding;
2. `validate_bootimg.py` trên `out/boot-qeafdropos-violet.img`;
3. `audit_rescue_channel.py` (đã siết) — `init_panel` / `init_gadget` phải
   nằm **trong chính ELF mà `/init` trỏ tới**;
4. `homo-init` phải mang chuỗi `QeafdropOs 0.1.0`.

### Chưa kiểm chứng trên phần cứng

Theo đúng `homo/platform/violet/README.md`: chưa thử `CREATE_DUMB`/`ADDFB`/
`SETCRTC` trên CRTC DSI của msm-4.14, tên node touch và trục raw, tên
backlight, việc dwc3 UDC đăng ký không có Android gadget HAL, và frame rate
(khung đầu ~1,9 M pixel trên một A53). Các gap đã biết: Wi-Fi/BT driver không
nằm trong build này, ALSA `dlopen` thiếu `libasound.so.2`, tiles Maps bị bỏ vì
p77 chỉ 64 MiB, không có suspend/thermal policy, **không có shell** (cứu hộ là
ổ USB + `QEAF.CMD`). Nói cách khác: image sẵn sàng để nạp, nhưng lần boot đầu
vẫn là một phép thử.

## Cổng kiểm tra (host)

Số liệu dưới đây đo ngày 2026-11-06 trên chính cây này; mỗi con số thuộc về
lượt chạy của nó, không tái dùng cho boot khác.

| Lệnh | Chứng minh | Số đo |
|---|---|---|
| `make check` | mọi `.c` của thiết bị biên dịch sạch với `-nostdinc -ffreestanding` | 13 objects |
| `make wifi-check` | seam Wi-Fi thật: parser nl80211 đọc blob đúng định dạng wire, rfkill ghi thật, kết nối bị từ chối thẳng, màn danh sách + chi tiết vẽ trên framebuffer 1080x2340; header có mũi tên quay lại `‹`, dòng mạng mở có chevron thay vì ổ khóa | 87 checks, 0 failed |
| `make bt-check` | seam HCI thật: inquiry seam của điện thoại, hai màn Bluetooth và settings hub, khung "off" là control âm trong cùng lượt chạy; mũi tên header `‹` có hit-test trên trang list | 72 checks, 0 failed |
| `make usb-check` | "Use USB for" 3 lựa chọn trên framebuffer thật + seam mode của gadget (charge mặc định, mode ngoài dải bị từ chối, rút cáp về charge) và nội dung `DEVINFO.TXT` | 37 checks, 0 failed |
| `make rescue-channel-check` | vòng lặp `QEAF.CMD` trên volume FAT16 thật: `HELP/SUMMARY/RESPONSE/QEAFBOOT/DEVINFO`, verb `status/echo/usb/sync/rotate`, từ chối `flash boot` và thiếu `confirm`, flash thật vào file fixture rồi đọc lại từng byte; cộng chính sách auto-rotate và identity `QeafdropOs 0.1.0` | 35 checks, 0 failed |
| `make keyboard-check` | keyboard Blueprint trên framebuffer thật: 34 phím, giấy kẻ ô, bản nạp chữ, Shift / `?123` / emoji / backspace / spacebar, intent xoay; và phép biến đổi xoay 0/90/180/270 với ánh xạ cảm ứng ngược | 36 checks, 0 failed |
| `make power-check` | seam `power_supply` đọc qua `core/sysio.c` và dòng "Charging 84%" trên màn hình khóa | 16 checks, 0 failed |
| `make bootui-render` | fastboot/recovery vẽ đúng; mục "Flash Recovery" bắt buộc đi qua hộp thoại xác nhận với CANCEL mặc định | 54 checks, 0 failed |
| `make motion-check` | nền động kiểu iOS 26: zoom + blur + dim chạy như một ramp thật trên panel 1080x2340, Home sắc nét, Lock/sheet phủ trên nền mờ, cửa sổ opaque thì nền nghỉ hẳn, màn boot đầu tiên đến thẳng trạng thái chốt | 44 checks, 0 failed |
| `make storage-check` | volume QEAFRESCUE thật sự có năm thư mục lưu trữ: `fs/fat16.c` dựng thư mục con + tên dài LFN + ghi cả hai FAT, rồi một reader C đi lại theo spec và `tests/check_rescue_volume.py` đọc lại bằng một cài đặt độc lập thứ hai; cộng thêm tạo / ghi đè `DEVINFO.TXT` tại chỗ | 85 checks (48 + 37), 0 failed |
| `make guard-test` | không còn chữ Android trong ảnh/ GUI, ảnh p77 hợp lệ, bản vá chữ chỉ đổi 63 byte, mọi lệnh ghi phân vùng đều phải có đồng ý, và audit kênh cứu hộ (kể cả driver trong đúng ELF `/init`) | 106 checks (56 + 1 + 49), 0 failed |
| `make violet-core-check` | lõi Homo/violet: `panel_scale_test` 4 góc xoay + `validate_bootimg` + audit nghiêm (`init_panel` / `init_gadget` trong `/init`) + identity `QeafdropOs 0.1.0` | 4 bước, 0 failed |

Ảnh render để người mở ra xem: `out/wifi_list_on.png`, `out/wifi_details.png`,
`out/bt_list_on.png`, `out/bt_details.png`, `out/settings_hub.png`,
`out/lock_charging.png`, `out/lock_unplugged.png`, `out/motion_home.png`,
`out/motion_lock.png`, `out/motion_sheet.png`, `out/motion_opaque.png`.

## Tính năng Wi-Fi và Bluetooth

Hai tính năng này chia sẻ cùng kiến trúc: một seam HAL thật trong `hal/include/`
(`wifi.h`, `bluetooth.h`), một driver `bsp/violet/` đọc hardware qua `core/sysio.c`
(`wcn3990.c` cho nl80211, `bluetooth_hci.c` cho HCI), và một bộ GUI
`apps/settings/` vẽ trên framebuffer 1080x2340 bằng `bootui` primitives.

### Seam radio thật, không bidiện danh sách

Cả hai driver đều có hai đường: **thật** và **dump**. Đường thật đi qua sysfs
(`/sys/class/ieee80211/phy0`, `/sys/class/rfkill/rfkill0/soft` cho Wi-Fi;
`/sys/class/bluetooth/hci0/address` cho BT). Khi board không có phy/hci, seam
trả `-1` và màn hình in "Radio not ready" — không bịa ra thiết bị ảo.

Đường dump cho host gate: `wcn_set_scan_dump()` và `bt_set_scan_dump()` nạp
một blob capture (nl80211 CMD_GET_SCAN cho Wi-Fi, fixed-record inquiry cho BT),
rồi cùng parser chạy trên điện thoại parse nó. Fixture `/sys` tree dựng bằng
file thường để `sysio_read_file()` đọc được.

Driver Wi-Fi `wcn3990.c` parse đúng định dạng wire nl80211: NLA nested,
SIGNAL_MBM là dBm×100, RSN AKM suite để phân WPA2/WPA3, freq→channel math,
sort theo RSSI giảm dần. Driver BT `bluetooth_hci.c` parse fixed-record 64 byte
(`QBT1` magic + count), sort connected lên đầu rồi theo RSSI.

### Màn GUI

Cả Wi-Fi và BT có hai trang: **list** và **details**. Settings hub
(`apps/settings/settings.c`) là cửa ngõ — hai hàng Wi-Fi và Bluetooth, mỗi hàng
có icon procedurally drawn và chevron `›`.

**Wi-Fi list** (`wifiui.c`):
- Header: mũi tên `‹` quay lại + tiêu đề "Wi-Fi" + menu kebab `⋮`
- Card toggle bật/tắt radio (gọi `wifi_on()`/`wifi_off()` → rfkill)
- Card mạng đang kết nối: icon WiFi arcs + SSID + "Connected" teal + chevron `›`
- Dòng mạng khả dụng: icon WiFi + SSID + auth label (WPA3/WPA2/Open) + signal bars
  + **ổ khóa** (mạng bảo vệ) hoặc **chevron** (mạng mở)
- Tap dòng → queue `WIFIUI_ACT_CONNECT`; tap card kết nối → mở details

**Wi-Fi details** (`wifiui.c`):
- Header: `‹` + "Network Details" + icon bút chì `(i)`
- Ba tile: Signal / Network usage / Security (icon + giá trị)
- Section Status: signal dBm, link Mbps, freq, channel, security — từ `wifi_link_info()`
- Section Advanced (fold/unfold): Privacy, Metered, IP settings, IPv4, Gateway,
  Subnet, DNS
- Ba action row: Manage saved networks, Scan settings, Network preferences
- Ba toggle: Turn on Wi-Fi automatically, Open network notification, Wi-Fi direct

**BT list** (`btui.c`):
- Header: `‹` + "Bluetooth" + `⋮`
- Card toggle + glyph Bluetooth lớn (bind-rune procedural)
- "Paired Devices": mỗi dòng có device-type icon (headset/watch/car/speaker/
  phone/keyboard), tên, sub-line ("Connected 78%" hoặc "Paired Headset"),
  signal indicator ở phải, nút `(i)` mở details
- "Available Devices": cùng layout, sub-line là type + codec
- Tap thiết bị unpaired → queue `BTUI_ACT_PAIR`; tap connected → details

**BT details** (`btui.c`):
- Header: `‹` + "Bluetooth Details" + `(i)`
- Ba tile: Signal / Network usage / Protected
- Section Status: signal dBm, connection type (LE/Classic), freq, codec, battery
- Section Advanced: Privacy, Spatial Mode, ANC, IP settings, MAC, Subnet, Gateway, DNS
- Ba action: Scan for devices, BT & Wi-Fi scanning, Pair new device
- Năm profile toggle: Phone calls, Media audio, Contact sharing, Input device,
  Share data — gọi `bt_set_profile()` xuống radio seam

### Kiểm chứng font tiếng Việt

Wi-Fi test có một bộ kiểm riêng cho composed glyphs: SSID `Quán Cà Phê_5G`
(U+00E1 acute, U+00E0 grave, U+00EA circumflex) phải vẽ đủ 12 ký tự có mực.
Font `font_5x7` có glyph composed cho U+00E0 (a grave), fallback cho U+00E1 và
U+00EA về base letter. Code point ngoài Latin-1 (U+1EBD) trả `NULL` — vẽ
nothing, không vẽ sai.

### Sửa chữ "Android" trong ảnh boot

`strip_android_from_image.py` thay `Android` → `System ` (7 byte, bằng nhau)
trong 5 entry cpio của ramdisk. Lần rebuild 2026-11-06 phát hiện: ELF mới do
Zig compile mang target-triple `android` (lowercase) trong `.rodata` — đây là
platform identifier của compiler, không phải brand label. Hàm `brand_in_gui()`
đã đổi từ case-insensitive sang **case-sensitive** để phân biệt: brand
`Android` (capital A) thì patch, platform `android` (lowercase) thì giữ
nguyên. Token giao thức `ANDROID!` ở boot header vẫn giữ nguyên.

## Đồng ý trước khi ghi phân vùng

**Quy tắc:** `flash recovery` ghi vào p77/recovery, nên mọi đường tới lệnh đó
đều phải có một bước đồng ý rõ ràng. Không có đường nào ghi phân vùng mà
không có confirm. p76/boot/userdata/misc không bao giờ là target.

| Đường | Bước đồng ý | Hành vi khi thiếu |
|---|---|---|
| GUI Fastboot Mode | mục **"Flash Recovery"** có `destructive = 1` → `bui_confirm()` với **CANCEL** là lựa chọn mặc định; tên hành động ghi rõ `(p77)` | một cú bấm Power nhầm không queue intent nào |
| GUI Recovery Mode | "Apply Update" (`BOOTIMG.BIN -> p77 (confirm)`) cùng cơ chế | như trên |
| `scripts/prepare_rescue_flash.py` | `--commit` chính là sự đồng ý; `QEAF.CMD` luôn là `flash recovery BOOTIMG.BIN confirm` | mặc định dry-run, không ghi file nào lên LUN |
| `scripts/send_rescue_cmd.py` | `--confirm-partition-write` (alias `--agree`) cho mọi verb `flash ...` | `REFUSED` trước khi mở `QEAF.CMD`, exit 2 |
| `scripts/restore_p77.ps1` | `-ConfirmPartitionWrite` **ngoài** `-Execute` | `REFUSED` ngay từ đầu, exit 2, không đụng tới thiết bị |

Hai lớp kiểm trên mọi lệnh flash ở phía host:
- `prepare_rescue_flash.cmd_line_is_safe()` — từ chối nếu thiếu token `confirm`
  hoặc target không phải `recovery`.
- `send_rescue_cmd.audit_commands()` — từ chối `flash boot`, `flash userdata`,
  `flash` không có target, và `flash recovery` thiếu `confirm`; dù đã có
  `--confirm-partition-write` cũng không cứu được target sai.

Ở phía thiết bị, rescue shell tự kiểm thêm: magic `ANDROID!`, từ `confirm`,
target `recovery`, và kích thước p77. Sau khi ghi nó trả hai bằng chứng độc
lập: `flash: written and verified byte for byte` trong `RESPONSE.TXT` và
`lun: verified - recovery and BOOTIMG.BIN agree byte for byte over N bytes`
trong `QEAFBOOT.LOG`. Đọc bằng `scripts/read_rescue_raw.py` (raw sector), không
đọc view đã mount vì view đó stale.

Lần nạp 2026-11-06 (ảnh 24 092 672 B, sha256 `4d5eeaa3dc3ed3df...`) có đủ
cả hai dòng: `out/report-flash-consent/RESPONSE.TXT` và
`out/report-flash-consent/QEAFBOOT.LOG:790`, trong cùng một sự kiện
`lun: running 35 bytes of commands from the volume` lúc uptime 2530 s.

p76/boot/userdata/misc không bị đụng, và không có lệnh reboot nào được gửi:
ảnh mới chỉ có hiệu lực ở lần khởi động tiếp theo.

## Cắm cáp PC: "Use USB for" 3 lựa chọn

Khi phát hiện cạnh lên của VBUS, `init/main.c` mở sheet `apps/usbui/usbui.c` phủ
lên màn hình đang hiện (không thay thế cửa sổ). Sheet có đúng ba lựa chọn:

| Lựa chọn | `enum usb_mode` | Dữ liệu |
|---|---|---|
| Charge this device | `USB_MODE_CHARGE` (mặc định) | chỉ VBUS, không endpoint dữ liệu |
| Transfer files (MTP) | `USB_MODE_MTP` | MTP + LUN điều khiển |
| Transfer photos (PTP) | `USB_MODE_PTP` | PTP + LUN điều khiển |

**LUN QEAFRESCUE là kênh điều khiển, không phải một trong ba lựa chọn** — nó
luôn còn trong mọi mode để VQEAF Tool không bị khoá ngoài và người dùng luôn
đổi ý được. Rút cáp tự động đưa mode về `charge`.

An toàn: không hàng nào được chọn sẵn, BACK và "Not now" chỉ dismiss, và chỉ
một cú chạm rõ ràng mới queue `USBUI_ACT_SET_MODE`. `init/main.c` là nơi duy
nhất gọi `usb_gadget_set_mode()` để áp intent. Gate `make usb-check` render cả
hai trạng thái ra `out/usb_sheet_charge.png` / `out/usb_sheet_ptp.png`.

## Đồng bộ với VQEAF Tool (`vqeaf-sync/1`)

Tool ở `D:\desktop-webapps\VQEAF-Tool`. Giao thức đồng bộ là file
**`DEVINFO.TXT`** (8.3, dòng `key=value`) do chính OS ghi vào volume FAT16 qua
`fs/fat16.c::fat16_write_file()`:

```
VQEAF-SYNC/1
os=VQEAF OS 0.1.0
device=violet
control_lun=QEAFRESCUE
usb_mode=charge
usb_cable=0
orientation=portrait
auto_rotate=0
storage_folders=5
sectors=65536
```

Chỉ công bố giá trị OS thật sự biết — không bịa serial, pin hay build hash.
`fat16_write_file()` ghi đè **tại chỗ** khi mode đổi (không sinh `DEVINF~1.TXT`
thứ hai); `make storage-check` đo cả hai chiều bằng một walker FAT độc lập.

Phía tool: `app/core/sync.py` đọc `DEVINFO.TXT` bằng **raw sector**, gộp với
log/summary cho bản OS cũ chưa có file này, rồi xuất một `DeviceState` duy
nhất. Backup kiểu 3uTools (`create_device_backup`) đóng gói `DEVICE.JSON`,
tất cả file cứu hộ, nội dung các thư mục mirror, kèm SHA-256 cho từng entry
trong `MANIFEST.JSON`; `BOOTIMG.BIN` 24 MB chỉ ghi hash + size chứ không nhân
đôi. `sync_verdict()` so sánh tree nguồn với thiết bị và báo drift.

Chạy headless (không cần PySide6):

```powershell
cd D:\desktop-webapps\VQEAF-Tool
py -3 -m app.core.sync --volume F: `
  --os-root D:\QeafivelsOfficial\QeafdropOs `
  --backup D:\QeafivelsOfficial\QeafdropOs\out\vqeaf-tool-test\device-backup.zip `
  --json  D:\QeafivelsOfficial\QeafdropOs\out\vqeaf-tool-test\sync-report.json `
  --log   D:\QeafivelsOfficial\QeafdropOs\out\vqeaf-tool-test\sync-test.log
```

### Ảnh p77 thiếu kênh cứu hộ — đã phát hiện và đã sửa

Test thiết bị thật ngày 2026-11-06 (`out/vqeaf-tool-test/sync-test.log`)
từng phát hiện ảnh `4d5eeaa3...` **đạt mọi kiểm tra cấu trúc** nhưng ramdisk
không chứa `QEAF.CMD`, `HELP.TXT`, `RESPONSE.TXT` hay `flash recovery`. Reboot
vào ảnh đó sẽ mất LUN.

**Đã sửa trong lượt tiếp theo**: `fs/rescue.c` là một kênh lệnh C thật được
biên dịch vào chính initramfs. Nó phát hành năm file của kênh, đọc
`QEAF.CMD`, trả `RESPONSE.TXT` (có echo nonce), xử lý `status/echo/usb/sync/ls`
và đặc biệt là `flash recovery BOOTIMG.BIN confirm`: kiểm tra magic
`ANDROID!` + cỡ p77, ghi phân vùng rồi **đọc lại từng byte** trước khi in
`flash: written and verified byte for byte`. `make rescue-channel-check` đo
toàn bộ đường này trên volume FAT16 thật với file fixture thay cho p77.

Ảnh mới `e80f6fbe...` (24 117 248 B) đã qua
`scripts/audit_rescue_channel.py` → **PASS**: 6 `QEAF.CMD`, 3 `HELP.TXT`,
4 `RESPONSE.TXT`, 5 `flash recovery`, 8 `DEVINFO.TXT`. Đã nạp p77 với đủ hai
bằng chứng trong `out/report-flash-final/`. OS đang chạy vẫn là bản cũ nên
`DEVINFO.TXT` chỉ xuất hiện sau lần reboot đầu tiên vào ảnh mới; **chưa reboot**
vì đây là thay đổi khởi động cần người dùng xác nhận.

Hai guard giữ lại để lỗi không lặp im lặng:

- `scripts/audit_rescue_channel.py` — giải nén ramdisk, đếm marker, exit 1 nếu
  thiếu. `prepare_rescue_flash.py` gọi nó và in cảnh báo đậm ngay trong plan.
- `app/core/sync.py::audit_staged_image()` trong VQEAF Tool — đọc
  `BOOTIMG.BIN` ngay trên LUN bằng raw FAT và trả exit 5 nếu ảnh không có
  kênh cứu hộ.

## Xoay màn hình (0° / 90° / 180° / 270° + auto)

Panel violet là **1080x2340 dọc, cố định**. `gui/gfx/gfx.c` vì thế không xoay
khung hình bằng buffer thứ hai: mọi lệnh vẽ vẫn dùng toạ độ **logical**, và
mỗi điểm ảnh được ánh xạ sang đúng một điểm vật lý theo phép xoay hiện tại.
Ở 90/270 mặt phẳng logical đổi thành **2340x1080**, nên layout ngang có đủ bề
rộng của panel.

| Góc | Toạ độ logical | Ánh xạ sang vật lý |
|---|---|---|
| 0° | 1080x2340 | `(x, y)` |
| 90° | 2340x1080 | `(y, 2339-x)` |
| 180° | 1080x2340 | `(1079-x, 2339-y)` |
| 270° | 2340x1080 | `(1079-y, x)` |

`gfx_plot565()` là cửa duy nhất để ghi pixel: `gfx_fill_rect`,
`gfx_blend_rect`, `font_draw_cp()` và cả `blit()` của bootui đều đi qua nó, nên
xoay panel không làm lệch chữ hay viền. `gfx_map_touch()` là ánh xạ ngược cho
cảm ứng; `bui_map_touch()` gộp luôn phép chia K, và **mọi màn hình**
(fastboot, recovery, wifi, BT, settings, usbui, keyboard) đã đổi sang dùng nó.
`bui_k()` giờ là `min(vw/360, vh/780)` thay vì chỉ `w/360`.

Chính sách nằm ở `hal/include/display.h` + `bsp/violet/display_orientation.c`:
một chủ sở hữu duy nhất gọi `gfx_set_rotation()`. `display_feed_sensor(gx,gy)`
nhận vector trọng lực (milli-g) và `display_poll()` áp deadband phẳng + tỉ lệ
1.30 giữa hai trục, nên máy nằm ngang không tự lật. Điều khiển:

- Từ kênh cứu hộ: `rotate 0|90|180|270|auto` trong `QEAF.CMD`.
- Từ giao diện: hàng nút `0 / 90 / 180 / 270 / AUTO` dưới bàn phím.
- `DEVINFO.TXT` công bố `orientation=` và `auto_rotate=` cho VQEAF Tool.

`make keyboard-check` đo từng góc bằng một điểm ảnh thật và kiểm tra ánh xạ
ngược trả đúng toạ độ ban đầu; `make rescue-channel-check` đo cả deadband.

## Bàn phím Blueprint

`apps/keyboard/keyboard.{c,h}` — mở từ **Settings → Keyboard** (hoặc
**Display rotation**). Lấy theo poster: giấy kẻ ô xanh nhạt, badge xanh dương,
một tấm **đen bo góc** đựng bốn hàng phím, phím chữ bạc có số mũ nhỏ `1..0`
trên hàng QWERTY, phím Shift / `?123` / emoji / Return màu than, và **hai phím
cam** là Backspace cùng thanh Space dài. Tất cả vẽ bằng số nguyên RGB565, không
asset, không float, không libm.

Không chỉ để nhìn: gõ được thật. Phím chữ ghi vào buffer, Shift đổi hoa/thường,
`?123` đổi lớp ký số, emoji chèn `:)`, Backspace xoá, Space / Return hoạt
động. Văn bản hiện ở vùng chữ lớn bên dưới tấm phím (đúng chỗ poster để câu
"orange is the new black"). Các nút xoay chỉ **queue intent**
`KEYBOARD_ACT_ROTATE`; `settings.c` mới là nơi gọi `display_set_orientation()`.

Layout được căn giữa trên mặt phẳng logical hiện tại (`bui_vwidth/height()`),
nên xoay ngang vẫn bấm đúng phím. Ảnh: `out/keyboard_portrait.png`,
`out/keyboard_landscape.png`.

> Hạn chế trung thực: ở góc 90/270 `K = 1`, tấm phím vẫn là design 360x780 được
> căn giữa trên mặt phẳng 2340x1080 nên trông nhỏ. Một layout ngang riêng (tận
> dụng bề rộng) là bước tiếp theo, chưa làm.

## Sự cố 2026-11-08: nạp xong nhưng LUN không lên

Lệnh của bạn: "nạp os vào thiết bị". Thiết bị đang ở **fastboot** (`ca1af25d`),
không còn `F:`, nên tôi dùng đúng quy trình có sẵn
`scripts/restore_p77.ps1 -Execute -ConfirmPartitionWrite`:

```
Sending 'recovery' (23400 KB)   OKAY [ 0.530s]
Writing 'recovery'              OKAY [ 0.122s]
Rebooting into recovery         OKAY
```

Log: `out/logs/restore-p77-20261108-082106.log`. **Ghi p77 thành công.**

Sau `reboot recovery`: không còn fastboot, không có adb, không có volume
QEAFRESCUE. `Get-Disk` chỉ thấy một "Generic Mass-Storage / No Media" (ổ đọc
thẻ), không có node USB nào của máy (VID 18D1 / 2717 / 05C6). Tôi đợi,
`Update-HostStorageCache`, dò lại 3 lần — vẫn không có LUN. **Vì vậy không có
bằng chứng đọc lại từng byte và không chạy được test VQEAF Tool sau boot.**

### Nguyên nhân (đã chứng minh bằng code, không phỏng đoán)

`bsp/violet/usb_gadget.c::usb_gadget_add_lun()` chỉ ghi log rồi `return 0` —
**không hề bind DWC3/ConfigFS**. `usb_gadget_init()` dựng volume FAT16 trong
RAM, nhưng không có ai đưa volume đó ra cổng USB. Bằng chứng:

- `out/qeafinit.elf` (747 560 B): `UDC` = 0, `configfs` = 0,
  `idVendor` = 0, `mass_storage` = 0; nhưng `QEAF.CMD` = 6,
  `DEVINFO.TXT` = 8.
- `grep -rn "configfs|UDC|idVendor|mass_storage" bsp/ hal/ fs/ init/ core/
  scripts/` → **rỗng**. `out/stage/bin/*` và `out/stage/etc/*` cũng không có.
- `out/tmp/oldsrc/init/lun.c` có 25 chỗ `idVendor`/`UDC`, và
  `out/m48-final/QEAFBOOT.LOG:14-16` ghi
  `lun: lun.0/file reads back '/run/qeaf-lun.img'` +
  `lun: binding UDC a600000.dwc3`. **Đó là init của cây cũ**, không phải ảnh
  vừa nạp.

Nói ngắn gọn: các ảnh `m48/m49/m49b` hiện tại đều có vòng lệnh cứu hộ nhưng
**thiếu phần cứng hoá kênh USB**. Audit cũ chỉ đếm marker `QEAF.CMD` nên đã
cho PASS nhầm — lỗi của tôi, giờ sửa.

### Guard mới

`scripts/audit_rescue_channel.py` và `app/core/sync.py::audit_staged_image()`
giờ yêu cầu thêm 4 marker `gadget_configfs / gadget_vendor /
gadget_function / gadget_udc`. Cả ba ảnh m48/m49/m49b đều bị từ chối đúng:

```
[WARN] ... missing gadget_configfs, gadget_vendor, gadget_function, gadget_udc
       the command loop exists but no USB mass-storage gadget is bound
```

`make guard-test` 52 checks, trong đó có assertion rằng chính ảnh production
bị REFUSE — không còn chỗ cho "có vẻ đủ".

### Màn hình trống: nguyên nhân thứ hai (2026-11-08, triệu chứng mới)

Bạn báo "trên thiết bị không hiện giao diện OS". Đó không phải lỗi theme hay
compositor — **PID 1 không có driver panel nào**:

- `bsp/violet/board_violet.c` đăng ký `g_display` với **toàn bộ callback NULL**
  (`init/get_geometry/fb/flush`). Bước 4 của `init/main.c` vì thế nhảy qua
  `gfx_init()`, `gfx_fb()` trả NULL, và **mọi lệnh vẽ đều là no-op**.
- `init/main.c` còn gọi `hal_rtc()->now_ms()` dù `g_rtc.now_ms` là NULL —
  nhảy tới địa chỉ 0 ngay vòng lặp đầu, giết PID 1. Đã sửa: kiểm tra cả con
  trỏ lẫn callback, rơi về `sysio_monotonic_ms()`. Thêm log
  `boot: FATAL display - ... the panel will stay blank` để lần sau không im lặn.
- Kernel violet **không có fbdev**: `out/tmp/oldsrc/init/panel_drm.c` ghi rõ
  `CONFIG_DRM_FBDEV_EMULATION is not set`, `/dev/fb0` và
  `/dev/graphics/fb0` không tồn tại, chỉ có `/dev/dri/card0`. Panel cần
  DRM/KMS `SETCRTC`, tức 1500 dòng driver trong cây cũ — **chưa port**.

Thủ phạm trực tiếp là tôi: `cp out/qeafinit.elf out/stage/sbin/qeafinit` đã
ghi đè init **1 562 904 B** của luồng build m49/m49b bằng bản skeleton
**747 560 B** của cây hiện tại. Bằng chứng so sánh `/init` trong cpio:

| Ảnh | `/init` -> | `sbin/qeafinit` | DRM panel | configfs/UDC |
|---|---|---|---|---|
| `m49b-noandroid.img` | `sbin/qeafinit` | 1 562 904 B | có | có |
| `m48-noandroid.img` (đã nạp) | `sbin/qeafinit` | 747 560 B | **không** | **không** |

### Guard mới (kiểm tra đúng binary PID 1)

`audit_rescue_channel.py` giờ parse newc cpio, resolve `/init` và chỉ đếm
`init_panel` (`SETCRTC` / `panel: first frame` / `/dev/fb0`) cùng
`init_gadget` (`UDC` / `configfs` / `lun.0/file`) **trong chính ELF đó**.
Marker nằm ở `bin/umos` không còn đánh lừa được: `make guard-test` có hẳn một
control "drivers in a NON-init binary do not fool the audit". Kết quả:

```
m48 / m49 / m49b  -> [WARN] BLANK SCREEN and no LUN  (đúng)
report-raw2/BOOTIMG.BIN -> [PASS] init_panel 4, init_gadget 14
backups/boot-qeaf-20261102-164808.img -> [PASS] init_panel 4, init_gadget 13
```

`app/core/sync.py::audit_staged_image()` phía VQEAF Tool cũng làm như vậy.

### Ảnh khôi phục đã chuẩn bị sẵn

`out/boot-qeaf-violet-known-good.img` — 18 714 624 B, sha256
`e58eaa6e17581d6bbc2329a59efc1b3617f254676920391ea19623fd54b3582f`, bản sao
của `out/backups/boot-qeaf-20261102-164808.img`. `validate_bootimg.py` PASS,
audit PASS, init có cả DRM panel lẫn configfs/UDC. Dry-run
`restore_p77.ps1` hiện báo `fastboot devices: (none)`.

**Cần bạn**: giữ **Volume Down + Power ~8 s** rồi cắm lại USB. Khi
`fastboot devices` hiện lại, chạy:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\restore_p77.ps1 `
  -Image "out\boot-qeaf-violet-known-good.img" -Execute -ConfirmPartitionWrite
```

Lệnh đó chỉ ghi p77 rồi `reboot recovery`; ảnh này sẽ dựng lại GUI và LUN.
Tôi không tự chạy vì hiện không có thiết bị nào trên USB.

### Việc cần làm tiếp

1. **Đưa máy về fastboot**: giữ **Volume Down + Power ~8 s** rồi cắm lại USB.
   Tôi không bấm được nút vật lý từ xa, và hiện máy không còn trên USB.
2. ~~Port ConfigFS/UDC~~ — **xong 2026-11-08**: `bsp/violet/usb_gadget_bind.c`
   (`qeaf_gadget_bind()` + `qeaf_gadget_start_detached()`), gọi ở bước 9b của
   `init/main.c`, primitives trong `core/sysio.{h,c}`. Volume giờ là file tmpfs
   `/run/qeaf-lun.img` mmap `MAP_SHARED`, nên byte host ghi vào `QEAF.CMD` là
   đúng byte PID 1 đọc. `make check` 17 objects PASS; object chứa đủ marker
   `/config/usb_gadget`, `idVendor`, `mass_storage`, `UDC`, `configfs`,
   `lun.0/file`. `usb-check` / `rescue-channel-check` (35 checks) / `guard-test`
   (49 checks) đều 0 failures.
3. ~~Port DRM panel~~ — **xong 2026-11-08**: `bsp/violet/panel_drm.c` (uapi tự
   khai vì `-nostdinc`, số ioctl suy ra từ `sizeof`), đăng ký vào `g_display`
   trong `board_violet.c`. Buffer 16 bpp RGB565 để khớp `uint16_t *` của
   `gui/gfx`; `get_geometry` trả pitch theo **byte** vì `gfx.c` chia hai.
   Timings chỉ lấy từ CRTC/connector, không tự đặt porch/clock. Panel hỏng thì
   `init()` trả 0 kèm một dòng log, vì `hal_init()` coi rc≠0 là chết PID 1.
   `make check` 18 objects PASS; object có `SETCRTC` 3, `panel: first frame` 1,
   `/dev/dri/card0` 3.
4. **Chưa** build lại ảnh và **chưa** nạp: chưa có lần boot nào chạy code này.
   Việc còn lại là dựng initramfs mới, cho audit thấy `init_panel` và
   `init_gadget` khác 0 trong `/init`, rồi nạp p77 khi máy về fastboot.

Bước 2 và 3 đã làm, nhưng chỉ ở mức **nguồn + biên dịch**: chưa có lần boot
nào chạy code này trên máy.

### p77 hiện giữ gì (2026-11-08 12:24)

Sau khi audit từ chối `m49b`, p77 đã được ghi bằng `out/boot-qeaf-violet-known-good.img`
(18 714 624 B, sha256 `e58eaa6e…`, audit **PASS**: `init_panel` 4,
`init_gadget` 13, `gadget_configfs` 26). Log:
`out/logs/restore-p77-20261108-122425.log` — `Sending 18276 KB OKAY`,
`Writing 'recovery' OKAY`, `Rebooting into recovery OKAY`.

Kết quả đo được sau boot, và nó **không** phải QEAF OS:

- Máy enumerate liên tục (khác `m49b`, thứ không có node nào):
  `USB\VID_18D1&PID_4EE7\CA1AF25D`, và `adb devices` → `ca1af25d device`.
- `getprop` trả `ro.product.device=violet`, `ro.build.version.release=13`,
  `aosp_violet-userdebug 13 TQ3A.230901.001.B1 release-keys`; kernel
  `4.14.215-azure-Pixel+`.
- **Không có** `/sbin/qeafinit`, **không có** `/run/qeaf-lun.img`, không có
  volume `QEAFRESCUE`. Tức là ảnh này chạy recovery Android 13 có adb, không
  chạy PID 1 của VQEAF.
- Phần cứng mà bước 2/3 dựa vào thì đã xác nhận bằng chính kernel đang chạy:
  `/dev/dri/card0` (226,0) và `/sys/class/udc` = `a600000.dwc3`.

### Nguyên nhân gốc: `skip_initramfs` trong cmdline (2026-11-08)

Đọc `/proc/cmdline` bằng adb root đã chỉ ra tại sao **không ảnh nào** tới giờ
hiện OS trên kính:

| Ảnh | Cờ initramfs trong cmdline | `/init` thực sự chạy |
|---|---|---|
| `m49b` | `force_initramfs prefer_initramfs` | `sbin/qeafinit` (1 562 904 B) — **không có** configfs/panel |
| `known-good` | `skip_initramfs … init=/init` | `/init -> /system/bin/init` (recovery Android 13) |

`skip_initramfs` bảo kernel bỏ qua ramdisk của ảnh, nên ảnh known-good dù
PASS audit vẫn chạy `system/bin/init` của Android — đó là lý do `adb` sống mà
không có `/sbin/qeafinit`, `/run/qeaf-lun.img` hay volume `QEAFRESCUE`.
Ngược lại `m49b` có `force_initramfs` nên init của nó **đã chạy**, chỉ là init
đó thiếu cả driver gadget lẫn panel — đúng hai thứ vừa port.

Phần cứng mà hai driver dựa vào đã xác nhận bằng kernel đang chạy:
`/dev/dri/card0` (226,0), `/sys/class/udc` = `a600000.dwc3`, và
`/config/usb_gadget/g1/functions/mass_storage.0` tồn tại.

Nghĩa là để thấy OS trên kính, còn thiếu **một ảnh mà `/init` thực sự là
qeafinit của cây này**: `out/qeafinit.elf` hiện là ELF freestanding không có
`_start`/crt, nên không thể đem thay `/init` của một ramdisk Android, và cmdline
phải giữ `force_initramfs prefer_initramfs` (không được `skip_initramfs`).

## Tính năng toàn diện (bản 2026-11-08)

Xem trang **Settings → All features** trên kính; mọi badge đều đọc getter thật,
không hardcode. `make features-check` = 70 checks, ảnh
`out/features_page.png` và `out/features_split.png` (đúng 2 màu, chứng minh
clip pane không leak).

| Nhóm | Đã làm thật | Giới hạn trung thực |
|---|---|---|
| UI & tuỳ biến | `gui/gfx/theme.c`: dark OLED true-black / light, **Material You** lấy accent từ pixel thật của wallpaper (`wallpaper_sample_palette`), 3 icon pack, font scale; `gui/widgets/widget_grid.c`: widget 4x6 ô, resize/move có từ chối chồng lấn; registry launcher bên thứ ba | Chọn launcher là **registry seam**, OS không nạp binary ngoài |
| Đa nhiệm | `gui/window_manager.c`: split 2 pane + floating window có z-order/drag; `gfx_set_clip()` là cửa duy nhất ghi pixel nên pane này không vẽ sang pane kia | Layout bootui vẽ full-screen, được clip; chưa có offscreen surface riêng từng app |
| Hiệu năng | `core/platform.c`: usage counter, freeze app ít dùng / wake nhiều khi pin thấp | **Rule engine, không phải ML** — `platform_adaptive_is_ml()` trả 0 |
| Kết nối | Wi-Fi / Bluetooth / USB đọc từ HAL thật | **NFC, UWB: không có phần cứng trên violet** — `platform_nfc_available()` / `uwb_available()` trả 0, bit không bao giờ bật |
| Bảo mật | Sandbox namespace `/data/app/<id>/`, từ chối traversal; permission 4 chế độ (deny / one-time / while-in-use / always), one-time tự tiêu thụ, cảnh báo truy cập ngầm; theft lock theo gia tốc | Cô lập ở mức **path policy**, chưa phải process/UID isolation của kernel |
| AI | Hàng đợi intent `platform_ai_enqueue/pull` cho VQEAF Tool xử lý phía host | **Không có model trên máy** — `platform_ai_available()` = 0, trang ghi "host only" |

## Giao diện Material kiểu Pixel (2026-11-08)

Vỏ giao diện đổi sang ngôn ngữ Material của AOSP 11. `apps/bootui/bootui.h` là
bảng token duy nhất — nền `#121212`, surface `#1E1E1E`, surface cao `#2D2D2D`,
viền `#3C4043`, accent `#8AB4F8`, chữ `#E8EAED` — nên launcher, màn khoá,
Settings hub và các trang Wi-Fi / Bluetooth / USB đổi cùng một lượt.

- **Home** (`apps/launcher/home.c`): thanh trạng thái, dòng At a Glance, lưới
  5x5 tile icon bo tròn, dock 4 app, pill tìm kiếm và thanh gesture. Không có
  lệnh vẽ nền full-screen nên wallpaper thật của compositor vẫn lộ ra.
- **Lock** (`apps/lock/lock_screen.c`): glyph khoá, dial lớn, nhãn
  `UP SINCE BOOT`, hai nút tròn và pill sạc. Dial in **uptime thật**
  (`sysio_monotonic_ms`) vì board không có RTC — không bịa giờ; pin vẫn đọc từ
  seam `power_supply`.
- **Settings hub**: icon tròn nền accent nhạt, hàng bo 12, section header màu
  accent; các trang con giữ nguyên bố cục và hit-test cũ.
- **Icon mới** là glyph procedural trong `bui_icon()`: phone, message, globe,
  camera, photo, pin, mail, clock, calendar, calculator, folder, sun,
  contacts, mic, terminal, search, wifi, signal, lock, music, store, notes,
  flash — không asset, không libm.

Gate đo lại trong cùng lượt: `make check` 18 objects; `motion-check` 44 checks;
`power-check` 16; `bootui-render` 54; `wifi-check` 87; `bt-check` 72;
`usb-check` 37; `features-check` 70; `keyboard-check` 36; `guard-test` 49 —
tất cả 0 failed. Ảnh: `out/motion_home.png`, `out/motion_lock.png`,
`out/lock_charging.png`, `out/settings_hub.png`, `out/wifi_list_on.png`,
`out/bootui_fastboot.png`, `out/bootui_recovery.png`.

## Nền động (zoom + blur + dim)

`gui/gfx/wallpaper.c` sinh ba mức mip (L0 270x585, L1 135x293, L2 68x147) và
`wallpaper_composite(zoom_q, blur_q, scrim_q)` kéo giãn một mức lên panel bằng
bilinear q8; `blur_q` đi từ L0 -> L1 -> L2 bằng cách trộn hai mức gần nhau.
`gui/compositor.c` giữ ba tween q8 (`COMP_DUR_MS 320`, ease-out cubic) và
`wm_open()` chỉ việc báo đích: Home = 256/0/0, Lock = 302/256/64, sheet =
302/256/0. Cửa sổ có `opaque = 1` (recovery, fastboot) thì `g_live = 0` — nền
nghỉ hẳn, không vẽ dưới thứ sẽ bị che. Màn đầu tiên sau boot đi qua
`compositor_settle()` để chốt ngay tại đích của Lock: không có ramp từ một ảnh
Home mà người dùng chưa từng thấy.

Chi phí đo trên host x86 `-O1`: 33 ms/frame giữa ramp, 33 ms/frame khi đã ổn
định, 35 ms/frame cho cả một `compositor_frame()` kèm lớp phủ mờ
[out/t/gate-motion.log:53-55](out/t/gate-motion.log). Đây là con số host, chưa
phải con số trên kính; máy chạy `-O2` nhưng bộ nhớ không nhanh hơn.

## Cấu trúc lưu trữ trên QEAFRESCUE

`fs/storage.c` là bảng duy nhất của năm thư mục: `Documents`, `Downloads`,
`Music`, `Pictures`, `Videos`. `bsp/violet/usb_gadget.c` gọi `storage_publish()`
ngay sau `fat16_init()`; nếu một thư mục không dựng được thì gadget báo
`LUN stays bare` và từ chối start — thà không có ổ còn hơn một ổ rỗng.

`fs/fat16.c` giờ dựng thư mục con thật, không chỉ ghi BPB rồi để root trống.
Mỗi thư mục chiếm một cluster: `.`, `..` và FAT entry `0xFFFF` được ghi
vào **cả hai** bản FAT, vì host có thể đọc bản nào. Một bản ghi tên dài (LFN)
luôn được viết trước bản 8.3, kể cả khi tên vừa vặn tám ký tự, vì trường 8.3
không giữ được chữ thường — `MUSIC` sẽ hiện ra thay vì `Music`. Tên dài hơn
tám ký tự lấy sáu ký tự đầu cộng `~n`, nên `Documents` → `DOCUME~1` và một stem
khác cùng sáu ký tự đầu nhận `DOCUME~2`; một reader chỉ biết 8.3 vẫn thấy năm
tên hợp lệ và phân biệt được.

Hai lỗi cũ đã sửa trong lượt này: `fat16_init` từng ghi ba byte rời cho
FAT[0]/FAT[1] nên `FAT[1]` thành `0x00FF` (giá trị reserved, host có quyền đọc
là đĩa hỏng), và `fs/fat16.c` giữ một mảng `g_fat16_data` 32 MiB trùng với
`g_lun_data` của gadget — đã bỏ, tiết kiệm 32 MiB BSS.

Cổng `make storage-check` dựng volume bằng chính code thiết bị, xuất MiB đầu
tiên ra `out/qeafrescue_meta.img`, rồi đọc lại hai lần bằng hai cài đặt độc
lập. Control trong lượt chạy: volume chỉ `fat16_init` đọc lại chỉ còn label
(không một thư mục), root bị zero thì walker tìm thấy 0 entry, các tên rỗng /
chứa `/` / quá dài bị từ chối mà không lấy một slot, và hai stem cùng sáu ký tự
đầu cho ra `DOCUME~1` + `DOCUME~2`. Control phía Python: lật một byte checksum
của một bản LFN thì đúng một thư mục mất tên dài và rơi về 8.3 — chứng minh
bộ đọc thật sự kiểm checksum chứ không bịa tên.

## Ảnh trên kính lấy bằng nào

`scripts/shot_png.py` đọc một `RESPONSE.TXT` do `scripts/send_rescue_cmd.py`
ghi lại, tìm khối `MAP cols rows scale pw ph ox oy`, giải mỗi mẫu một byte
3-3-2 thành RGB và xuất hai PNG (nguyên kích thước và phóng nearest-neighbour).
Đường chụp vẫn là đường cũ: đánh thức và chụp phải nằm **cùng một batch**,
vì round-trip host dài hơn hẹn giờ idle 60 s của shell.

Lượt 2026-11-05 nạp `m46-noandroid` vào p77 qua volume và đo lại màn
fastboot. Đây là **lịch sử**: ảnh m46 đã bị xoá, bản đang chạy là `m48`
(xem mục *Ảnh OS hiện tại* ở trên); các artifact dưới đây vẫn còn vì chúng là
phép đo, không phải ảnh nạp.

- Nạp thành công có hai bằng chứng, cả hai nằm trong cùng boot đang chạy:
  `RESPONSE.TXT` trả `flash: written and verified byte for byte`
  ([out/report-flash-1105/pass1/RESPONSE.TXT:2](out/report-flash-1105/pass1/RESPONSE.TXT))
  và `QEAFBOOT.LOG` in `lun: verified - recovery and BOOTIMG.BIN agree byte
  for byte over 22306816 bytes`
  ([out/report-flash-1105/pass1/QEAFBOOT.LOG:694](out/report-flash-1105/pass1/QEAFBOOT.LOG)).
- Ảnh Fastboot Mode của cây này vẽ đúng trên host
  (`make bootui-render` → `out/bootui_fastboot.png`, 1080x2340, 51 màu).
- Bản đang chạy trên máy **không có** cửa sổ fastboot: bảng cửa sổ của nó
  không có `fastboot`, nên thứ duy nhất trên kính là ảnh `Khoa`
  (`out/t/menu-shot-1105/menu-72x156-x8.png`). Verb `menu` trả `menu: up, band
  on Continue` nhưng canvas không đổi một byte nào — `snap` trước và sau khi
  mở menu cùng `sum 195510`, chỉ serial đi 4 → 5. Trang menu của init không tới
  được kính khi UMOS đang giữ nó.
- Màn fastboot **thật** nằm trong ABL đã ký: `bootloader` đưa máy vào đó,
  `fastboot devices` trả `ca1af25d fastboot` và `getvar all` trả
  `product:violet`, `unlocked:yes`
  ([out/t/fastboot-getvar-1105.log](out/t/fastboot-getvar-1105.log)). ABL không
  cho đọc framebuffer và không có kênh cứu hộ, nên không tồn tại ảnh chụp của
  màn đó; `fastboot reboot recovery` đưa máy về UMOS, kính lại sáng
  (`out/t/after-fastboot-1105/lock-back-72x156-x8.png`).
- Control chứng minh bộ chụp đọc được chữ thật, không phải nhiễu: crop
  `snap 200 620 680 260 4` hiện rõ `25 °C`
  (`out/t/crop-clock-1105/clockcrop-170x65-x6.png`).

## Build

### Userspace ELF (không nạp được)

```powershell
powershell -ExecutionPolicy Bypass -File build/build.ps1
```

Yêu cầu: Zig compiler tại `out/toolchain/zig-windows-x86_64-0.13.0/zig.exe`.
`make` cần `C:\msys64\usr/bin` trên PATH; Makefile tự pin `ZIG_GLOBAL_CACHE_DIR`.

### Ảnh OS nạp được (m48)

```powershell
python scripts/mk_initramfs.py
python scripts/mk_bootimg_violet.py `
  --output out/boot-qeaf-violet-m48.img `
  --append "qeaf.verbose qeaf.nopanel qeaf.umos qeaf.keepbootcmd"
python scripts/strip_android_from_image.py `
  --image out/boot-qeaf-violet-m48.img `
  --output out/boot-qeaf-violet-m48-noandroid.img --all
python scripts/validate_bootimg.py out/boot-qeaf-violet-m48-noandroid.img
```

`mk_initramfs.py` gói `out/stage` bằng newc writer `scripts/mk_cpio.py` rồi
gzip với `mtime=0`, nên hai lần dựng cho ra cùng byte. Symlink mà Git Bash trên
Windows không tạo được lấy từ `out/stage-symlinks.txt` (một dòng
`path<TAB>target`), nếu thiếu thì `/init` không có và boot chết với
`No working init found`.

`mk_bootimg_violet.py` chép nguyên page header của ảnh stock
(`out/reference/stock-violet/boot.img`) rồi chỉ ghi lại `ramdisk_size`,
`ramdisk_addr` và cmdline; kernel blob không đổi một byte.

### Nạp vào p77

```powershell
python scripts/prepare_rescue_flash.py `
  --image out/boot-qeaf-violet-m48-noandroid.img --volume F:
# đọc plan; --commit chính là bước đồng ý cho lần ghi phân vùng p77
python scripts/prepare_rescue_flash.py `
  --image out/boot-qeaf-violet-m48-noandroid.img --volume F: --commit
```

`prepare_rescue_flash.py` chạy `validate_bootimg.py` trước, mặc định dry-run,
từ chối volume không có label `QEAFRESCUE`, và không bao giờ `-Eject`. Bản thân
script chỉ ghi file lên LUN, nhưng `QEAF.CMD` của nó ra lệnh thiết bị ghi p77 —
vì thế `--commit` là sự đồng ý, và dòng lệnh luôn mang token `confirm`.

Đường fastboot dùng khi máy không lên được rescue OS:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\restore_p77.ps1
powershell -ExecutionPolicy Bypass -File scripts\restore_p77.ps1 `
  -Execute -ConfirmPartitionWrite
```

`-Execute` một mình bị từ chối (exit 2). Gửi verb tuỳ ý qua
`scripts/send_rescue_cmd.py` cũng cần `--confirm-partition-write` nếu trong
batch có lệnh `flash`.

## Thư mục

- `core/` — log, event queue, scheduler, heap, `sysio.c` (kênh syscall/host fixture)
- `hal/include/` — hợp đồng seam (wifi, bluetooth, battery, touch, display)
- `bsp/violet/` — board, backlight, touch, audio, `wcn3990.c` (nl80211 Wi-Fi),
  `bluetooth_hci.c` (HCI inquiry parser + rfkill), `power_supply.c`,
  `usb_gadget.c` (LUN + `DEVINFO.TXT`), `display_orientation.c` (chính sách xoay)
- `gui/`, `apps/` — `gfx.c` (ánh xạ xoay 4 góc + chạm ngược), widgets, các cửa
  sổ (lock, settings, wifiui, btui, bootui, recovery, fastboot), `usbui/`
  (sheet 3 chế độ USB), `keyboard/` (bàn phím Blueprint)
- `fs/` — `fat16.c` (builder volume cứu hộ, `fat16_write_file/read_file/
  file_span`), `storage.c` (năm thư mục lưu trữ), `rescue.c` (vòng lặp
  `QEAF.CMD` + flash verb có đọc lại xác minh)
- `scripts/` — pipeline ảnh: `mk_cpio.py` → `mk_initramfs.py` →
  `mk_bootimg_violet.py` → `strip_android_from_image.py` → `validate_bootimg.py`
  → `audit_rescue_channel.py` → `prepare_rescue_flash.py`; kèm
  `read_rescue_raw.py`, `shot_png.py`, `send_rescue_cmd.py`, `restore_p77.ps1`
- `tests/` — cổng host và fixture
