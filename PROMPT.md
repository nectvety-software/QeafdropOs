# QeafdropOs — Prompt cho AI agent

Vision: Redmi Note 7 Pro (violet) chạy Homo OS / LVGL 9.4, `homo-init` là PID 1,
khởi động từ initramfs trên stock kernel. Panel 1080x2340, shell dọc `540x1170`
full-screen (2x, không viền), ngang `1170x540` full-screen. App ngang
`1170x540`, Cài đặt chạy trên canvas shell (responsive, light 1 cột).

## Quy tắc không phá

1. Chỉ ghi **p77 (recovery)**; không bao giờ `p76/boot/userdata`, không eject `F:`.
2. Mọi lần ghi p77 phải xin đồng ý rõ ràng; nạp xong phải có 2 bằng chứng:
   `flash: written and verified byte for byte` + `lun: verified ... agree byte for byte`.
3. Mọi claim phải từ ảnh `snap` (SCREEN.RAW 240x520) hoặc log `HOMOLOG.TXT`/`RESPONSE.TXT`,
   không claim suông.
4. Cài đặt light mode 1 cột, font Ubuntu; Wi-Fi trên violet báo
   `No wireless hardware` đúng thực tế (driver `wlan`+`cfg80211` đã có nhưng chưa có firmware).
5. Vuốt/kéo trên kính phải kiểm bằng tay; `snap` không mô phỏng được chạm.
6. LUN `F:` là RAM 32 MiB, mất sau reboot. `.qeapp` bền qua reboot phải đóng vào initramfs `/apps/*.qeapp` (xem `homo/scripts/package_violet.py` + `homo/docs/qeapp.md`).

## SDCARD layout (LUN FAT16, `storage_publish` qua `fat16_mkdir_in`)

```
VQEAF/system/config,settings,language,fonts,permissions
VQEAF/apps/ (12 .qeapp Homo ZIP)
VQEAF/framework/libraries,runtime,api,plugins
VQEAF/themes/default/wallpaper,icons,fonts,ui + custom/
VQEAF/boot/splash,recovery
VQEAF/drivers/display,storage,audio,input,network
VQEAF/services/notification,power,storage,update
VQEAF/updates/packages,cache
VQEAF/logs, cache
UserData/Documents,Downloads,Pictures,Music,Videos,Backups,AppData
Install/Apps,Themes,Drivers,Updates
README.txt
```

Top-level ở root: `VQEAF`, `UserData`, `Install` (3 thư mục). `fs/storage.h` `UMOS_STORAGE_COUNT=3`.
Thư mục rác đã xóa: `System Volume Information`, `Documents/Downloads/Music/Pictures/Videos` gốc,
`out/report-*` (295), `out/t`, `out/snap-*.png`, 118 thư mục `out/*` và 666 file tạm trong `out/`.

## App `.qeapp` mới (12 app)

Mỗi app: `manifest.json` + `src/*.c` (`lv_obj_t *create(lv_obj_t*)` + `q_create`) + `assets/icon.png`
+ `README.md`, build bằng `python homo/scripts/make_qeapp.py <dir> out/apps/<id>.qeapp`.

```
bootui, browser, fastboot, gallery, installer, keyboard,
launcher, lock, messages, recovery, settings, usbui
```

`out/apps/*.qeapp` được đóng vào initramfs `/apps/` (qua reboot) và copy qua LUN lên `F:\VQEAF\apps\`.
`out/installer.qeapp` là bản copy của `out/apps/installer.qeapp` (8246 B).

## Build / gate

```powershell
$env:PATH="C:\msys64\usr\bin;$env:PATH"
python homo\scripts\build_violet.py
python homo\scripts\package_violet.py
make violet-core-check   # panel_scale 540x1170/1170x540 + validate + audit
make rescue-channel-check # 39 checks
make storage-check       # 31 checks, VQEAF/UserData/Install + nested
make check               # 21 objects
python scripts\validate_bootimg.py out\boot-qeafdropos-violet.img
```

`out/toolchain/zig-windows-x86_64-0.13.0/zig.exe` bắt buộc (tải lại nếu mất sau dọn `out/`).

## Flash + kiểm

```powershell
python scripts\prepare_rescue_flash.py --image out\boot-qeafdropos-violet.img --volume F: --commit
Write-VolumeCache -DriveLetter F; Start-Sleep -Seconds 12
python scripts\read_rescue_raw.py --drive F: --out out\report-flash --files RESPONSE.TXT QEAFBOOT.LOG
python -c "open('F:/QEAF.CMD','w',newline='\r\n').write('reboot recovery\n')"
```

Verbs: `help status wifi sync usb rotate lang echo ls kmsg snap reboot flash`.
`wifi net|dmesg|sdio|mod|fw` để probe driver/firmware trước khi đụng firmware.
Test tự động: `out/auto_test.py` (status/help/ls, wifi, rotate/lang, usb/snap/kmsg).

Hiện tại image: `out/boot-qeafdropos-violet.img` `28590080 B` `sha256 c7edf677…ef26b3ee3cf` `PASS`.
`phone_agent.py` chỉ còn `__pycache__` (nguồn đã xóa khi dọn) và `rescue.c` chưa có `tap/swipe/hold`.

## Giới hạn hiện tại (tra cứu 2026-11-10)

| Repo | Là gì | Liên quan tới violet hiện tại |
|---|---|---|
| `oosexgang/hakios-xnu-kernel` | XNU kernel trên Android, loader + driver riêng, Metal trên Adreno, chạy RAM | Chạy trên POCO X5 Pro SM7325, chưa có port violet SDM660; `wifi/lx` chỉ stub, không có Linux WCN3990 combo cho violet |
| `oosexgang/hakios-metal` | Đã gộp vào kernel `hakios-metal/` từ 2.3.3, Metal trên Adreno 7c+ Gen3 `0x06030500` qua kernel GPU driver | Violet là A630 SDM660, không dùng Metal; hiện tại dùng DRM dumb buffer + LVGL |
| `oosexgang/hakios-uefi` | UEFI Mu-Silicium + XNU loader, driver vendor không có trong repo, lấy từ `xbl` của máy mình | Cần platform UEFI riêng cho violet (chưa có); firmware chưa build/start |
| `SimplyLuma/luma` | Linux (Fedora + GTK), chung nền desktop/tablet/mobile, Android phụ qua Waydroid | Không có profile violet/WCN3990; không giúp được Linux WCN3990 combo hiện tại |
| `youzjuzi/dipper-xnu` | XNU cho dipper SDM845 + A630 + **WCN3990**, SpringBoard iOS 16.5, Wi-Fi đã vào mạng lấy IP + Safari | Gần nhất: cùng combo WCN3990/A630. Chứng minh Wi-Fi sống nếu có firmware từ `modem/persist` (root extract 4 GPU fw). Nhưng là XNU, không phải Linux Android kernel của homo-init; chỉ tham khảo tách firmware, không mang driver nguyên |
| `derekross/omarchy-mobile` | Arch Linux ARM + Hyprland trên **mainline kernel** v7.3-rc6, fw trích từ factory image của mình, NV checksums ở `persist/modem/efs` | Mẫu Linux mainline + trích fw đúng nguyên tắc violet cần (modem p82/persist p74), nhưng violet hiện tại dùng kernel vendor 4.14, không phải mainline; chỉ tham khảo quy trình backup/provision |

**Kết luận:** 6 repo đều **không** cung cấp driver Linux WCN3990 sẵn cho violet 4.14. Wi-Fi vẫn `wlan0 present: no` tới khi staged `modem/persist` firmware + `wlan_mac.bin`/`WCNSS_qcom_cfg.ini` vào `/lib/firmware`. XNU/Metal/UEFI không thay thế được DRM/LVN hiện tại trên SDM660.
