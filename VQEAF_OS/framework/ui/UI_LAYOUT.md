# Bố cục UI: dọc 9:16 (mặc định) và ngang 16:9

Shell dọc là canvas **540x1170** (đúng 2x 1080x2340, không viền, 2.0x vừa khít). Shell ngang là **1170x540** (2x vừa khít 2340x1080, không viền). App ngang (non-Settings) dùng canvas **1170x540**; Cài đặt chạy trên canvas shell (dọc/ngang theo hướng) và nền tảng đổi canvas khi mở/đóng app.
nền tảng đổi canvas khi mở/đóng app. Toàn bộ số dưới đây do `ios_metrics.c`
tính từ phân số của màn tham chiếu iPhone 390x844, nên đổi canvas là đổi theo
mà không phải sửa tay; `make ios-metrics-check` (`tests/ios_metrics_test.c`)
khoá cả hai nhánh.

Font hệ thống là **Ubuntu** (Google Fonts, OFL) + fallback Liberation/PingFang;
Ubuntu bao phủ đầy đủ Latin/Việt có dấu nên nhãn Việt không còn phụ thuộc
fallback.

## Home dọc — lưới icon

| Thành phần | Công thức | Trên 540x1170 |
|---|---|---|
| Số cột | hằng số | 4 |
| Cạnh icon | 60/390 W | 83 |
| Khoảng cách cột | 32/390 W | 44 |
| Lề trái = phải | (W - 4*icon - 3*gap)/2 | 38 |
| Tâm cột 0 / bước cột | margin + icon/2 + c*(icon+gap) | 79 / 127 |
| Cỡ chữ nhãn | 11/390 W | 15 |
| Khoảng icon -> nhãn | 4/390 W | 6 |
| Bề rộng nhãn | icon + 30/390 W | 125 |
| Đỉnh hàng 1 | 79/844 H | 110 |
| Bước hàng | 107/844 H (không nhỏ hơn icon) | 148 |
| Số hàng trước dock | (dock_top - first_row_top)/row_pitch | 6 |
| Dock: cao / đỉnh | 82/844 H; H - 20/844 H - cao | 114 / 1028 |
| Dock: lề trong / bo góc | 8/390 W; 39% chiều cao dock | 11 / 44 |
| Home bar | 134/390 W x 5/844 H; y = H - 8/844 H - cao | 186 x 7 @ 1152 |
| Status bar | 33/844 H; chữ 16/390 W | cao 46, chữ 22 |
| Status: pad trên/trái, wifi, pin | 10/390, 20/390, 23/390, 32/390 | 14, 28, 32, 44 |

Icon được đặt tại tâm cột; nhãn auto-height, canh giữa dưới icon với khoảng
`label_gap`. Dock dàn đều các icon theo bề ngang trừ hai lề trong, nên số app
trong dock không đổi bố cục. Khi số hàng vượt `rows_capacity` thì
`layout_home_icons()` mới nén bước hàng — dưới ngưỡng đó pitch là hằng số.

Chạm nhanh icon mở app. Nhấn giữ lâu (`LONG_PRESSED`) vào chế độ chỉnh sửa
(badge X đỏ hiện, icon đầu tiên bắt đầu kéo ngay), kéo (PRỂIETING) di chuyển, thả (RELEASED) snap ô gần nhất.
Nhấn X là gỡ icon (ẩn + đưa xuống cuối Home) và ghi `/etc/qeaf-apps.conf`.
Chạm nền thoát chế độ chỉnh sửa. Chạm Home bar mở **đa nhiệm** (danh sách app
đang chạy, chạm để chuyển, X đỏ để đóng hẳn và giải phóng view).

## Nhãn không bị cắt theo chiều dọc

`lv_tiny_ttf_set_size` đặt `line_height = scale*(ascent - descent + line_gap)`
và `base_line = scale*(line_gap - descent)`; `lv_draw_label.c:612` vẽ glyph tại
`y + (line_height - base_line) - box_h - ofs_y`. Với Ubuntu 15 px,
`line_height` ~17, baseline rơi ~13 px và descender hết ~16 px — nằm trong hộp
dòng. Nhãn còn auto-height nên cột nhãn cao 83 + 6 + 17 = 106 px, thấp hơn bước
hàng 122 px.

Ảnh dump của shell (`snap` -> `SCREEN.RAW`) chỉ là **240x520**, tức lấy mẫu
nearest-neighbour ~4.5:1 của scanout: một font 15 px chỉ còn ~5 pixel thô, nên
vệt như "bị cắt" trong ảnh dump là phép lấy mẫu, không phải hộp vẽ. Muốn soi
glyph thì đọc metrics như trên hoặc chụp ở độ phân giải cao hơn.

## Status bar và vùng an toàn

Portrait: dải cao 46 px, chữ 22 (Ubuntu), pad trên 14 / trái 28; hàng đầu của
lưới bắt đầu ở y110 nên không chồng lấn. Wifi/pin căn giữa dọc cùng tâm. Đồng hồ trên status bar cập nhật mỗi
giây qua `hachimi_clock_format("%H:%M")` + timer 1 s. Control Center giữ status
bar **sắc nét** phía trên sheet (giống iOS): `capture_background()` ẩn status
bar của Home đúng lúc `lv_snapshot_take_to_draw_buf()` rồi bật lại ngay.

App module 800x480 vẫn dùng hằng số iPad-style trong `ios_layout.h`:
`IOS_STATUS_BAR_CONTENT_BOTTOM` 32, `IOS_STATUS_BAR_SAFE_GAP` 26,
`IOS_APP_CONTENT_SAFE_TOP` 58, `IOS_APP_CONTENT_PANEL_TOP` 28. Trước khi đổi
các số này, xác nhận trên canvas 800x480 rằng icon status không đè nội dung app.

## Ngôn ngữ

Settings > Ngôn ngữ (dark list như tham chiếu): 8 hàng VI/EN/JA/ZH/ES/DE/KO +
Automatic from browser, chip trái, tick xanh cho hàng đang chọn, row được chọn
tô xanh đậm. `core/i18n` lưu mọi giá trị (VI..AUTO) vào prefs; catalog nhãn
LVGL có VI/EN/ZH, các giá trị còn lại hiện nhãn tiếng Anh nhưng giữ nguyên
preference sau reboot. Rescue verb `lang vi|en|zh` có sẵn.

## Control Center dọc

`control_center_controls.c` tính theo canvas; trên 540x1170 vẫn giữ tỷ lệ cũ
(lề 5% W, slider cao H - slider_y - 13% H...). Sheet nằm trên `lv_layer_top()`;
`hachimi_control_center_relayout()` đóng sheet khi bố cục đổi (xoay màn hình).

## Wi-Fi (trung thực với phần cứng)

Verb rescue `wifi` / `wifi net|dmesg|sdio|mod|fw` là dụng cụ thật: `wifi` đọc `/sys/class/net` + dmesg wlan, `wifi net` `ip link` (operstate+addr), `wifi dmesg` lọc wlan/WCN/cfg/firmware, `wifi sdio` liệt kê `/sys/bus/mmc`, `wifi mod` `/sys/module`+`/lib/modules`, `wifi fw` 6 thư mục firmware. Trên violet đã đo: module `wlan`+`cfg80211` đã có, `wlan_hdd_state` initialized, `mmc0:0001` có, nhưng **không có `wlan0`** và 6 thư mục firmware absent trong initramfs -> netdev không lên. Driver có, firmware chưa có.
Settings > Mạng trên violet báo **No wireless hardware** và tắt quét/kết nối đúng. Quét/kết nối thật (WPA2-PSK/WPA3-SAE) chỉ trên board Linux có helper `hachimi-network` (wpa_cli + /sys/class/net/wlan0 + systemctl).

## Landscape (full, không viền)

Shell ngang `1170x540` khớp panel 2x, không viền như dọc. App ngang dùng cùng canvas `1170x540` nên xoay ngang full-screen. Lưới ngang 6 cột icon 64 giữ khi test `ios_metrics` với 800x480, nhưng display thật là 1170x540 (lề giữa tự nới theo chiều rộng).
