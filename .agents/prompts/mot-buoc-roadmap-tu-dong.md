# Prompt: một bước roadmap tự động ("tiếp tục")

> Dùng khi chủ máy nói **"tiếp tục"** / **"bước kế tiếp"** / một câu roadmap ngắn.
> Chế độ này nghĩa là: **không hỏi lại từng bước**, tự viết thiếu cổng nào thì
> viết cổng đó, thiết bị rơi kênh thì cắm lại và chạy tiếp — nhưng **vẫn** phát
> báo cáo bảy mục khi kết thúc.

## Cách chọn bước

1. Đọc task list, lấy việc `in_progress` cũ nhất; nếu không có, lấy `pending`
   có số nhỏ nhất mà không bị block.
2. **Loại những bước cần thứ rig không có.** Camera, lux meter, người nhìn, một
   chiếc điện thoại khác (#50 sirius), adb thật (#60) — những việc đó không phải
   "bước kế tiếp được phép" của agent.
3. Trước khi sửa gì, kiểm tra bước đó có **bằng chứng gần nhất** chưa. Nhiều việc
   (`#105`) chỉ thiếu một câu xác nhận của chủ máy, không thiếu code.

## Trình tự một bước (mặc định của repo)

```
đo hiện trạng (artifact + boot suffix -mN)
  -> tìm cơ chế trong mã, tới tận chỗ ghi/flush/commit cuối
  -> viết gate, chạy trên bản code cũ: PHẢI ĐỎ
  -> sửa tối thiểu
  -> make check (13 objects, không warning)
  -> dựng ảnh (pipeline thật của cây này):
       python scripts/mk_initramfs.py
       python scripts/mk_bootimg_violet.py --output out/boot-qeaf-violet-m48.img \
         --append "qeaf.verbose qeaf.nopanel qeaf.umos qeaf.keepbootcmd"
       python scripts/strip_android_from_image.py \
         --image out/boot-qeaf-violet-m48.img \
         --output out/boot-qeaf-violet-m48-noandroid.img --all
       python scripts/validate_bootimg.py out/boot-qeaf-violet-m48-noandroid.img
  -> make guard-test: PHẢI xanh trước khi nạp
  -> nạp: python scripts/prepare_rescue_flash.py --volume F:\ --commit
     (chỉ p77, không -Eject)
  -> đo lại trên kính, so từng dòng với lượt trước
  -> docs: README.md + .agents/ (skill umos-gates, rescue-channel) nếu số/luật đổi
  -> báo cáo bảy mục
```

## Những lần "tiếp tục" đã hỏng vì đi tắt

| Đi tắt | Hậu quả đo được |
|---|---|
| Kết luận "kính tối" từ ảnh dump trong khi connector `Off` | `disp_flush()` return sớm khi panel blank, ảnh chỉ nói shell **vẽ** gì — vài lượt đổ vào một hypothesis sai trước khi đo `dpms` chung batch |
| Sửa triệu chứng rồi mới viết gate | gate xanh trên cả code cũ, không bảo vệ gì |
| Dùng lại số của boot `m37` cho boot `m38` | hai kết luận trái nhau cùng mang một con số |
| Gửi lại batch khi rig treo | batch đó là write; treo là ở đầu **đọc**, artifact đã nằm trên đĩa |
| Claim "đã sửa" vì host suite xanh | host BSP present cả panel (`board_win.c:285`) nên host không thể thấy flush rỗng |

## Ràng buộc không được nới dù đang tự động

- Không đổi backend hiện có.
- Không commit khi chưa được yêu cầu.
- Không đụng partition khác p77; không `-Eject`; không `/proc/tp_*`.
- Không bỏ qua bước "nhìn ảnh bằng mắt" khi kết luận về bố cục — bộ đếm màu chỉ
  nói có mực.
- Không claim đã chạy một gate nếu không có stdout của gate đó trong phiên này.
