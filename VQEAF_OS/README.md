# VQEAF_OS

```
VQEAF_OS/
├── bootloader/   # Khởi động hệ điều hành (stock kernel copy + mk_bootimg)
├── kernel/       # Nhân hệ điều hành (stock Android kernel, chưa custom)
├── system/       # Thành phần hệ thống (homo-init PID1, initramfs root, fs/storage 3 thư mục)
├── drivers/      # Trình điều khiển -> homo/platform/violet (DRM/Input/LUN)
├── services/     # Dịch vụ nền -> homo/services + core/fs
├── framework/    # Framework và API -> homo/framework (LVGL, desktop, i18n, app_loader)
├── apps/         # Ứng dụng .qeapp – 12 app mới (manifest+src+assets) ở /apps initramfs
├── themes/       # Giao diện và chủ đề -> homo/custom/assets/ios (Ubuntu, wallpaper)
├── recovery/     # Recovery (fs/rescue.c, QEAF.CMD, p77-only)
├── fastboot/     # Fastboot (host flash)
├── tools/        # Công cụ phát triển -> homo/scripts
└── README.md
```

SDCARD (LUN FAT16 32 MiB RAM): `VQEAF/UserData/Install` + 38 thư mục con, qua `fat16_mkdir_in`. Mất sau reboot; `.qeapp` bền qua reboot ở `/apps` initramfs.

Junction tới cây hiện có nên build (`homo/scripts/build_violet.py`,
`package_violet.py`, gates) vẫn chạy từ gốc repo. Chạy:

```
python homo/scripts/build_violet.py
python homo/scripts/package_violet.py
python scripts/prepare_rescue_flash.py --image out/boot-qeafdropos-violet.img --volume F: --commit
```
