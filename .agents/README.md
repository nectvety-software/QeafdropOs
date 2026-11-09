# .agents — luật làm việc của repo này

Thư mục này chứa những gì agent cần để làm việc **trên VQEAF_OS** mà không phải
đo lại từ đầu các bài học đã trả giá bằng thiết bị. Nó không phải tài liệu tính
năng (cái đó ở `README.md`), mà là tài liệu **phương pháp**.

Mọi dòng ở đây đều trace được về mã hoặc một artifact đo thật. Khi tài liệu này
mâu thuẫn với mã, **mã đúng** và file này là thứ phải sửa.

## Prompts (`prompts/`)

Frame cho những việc lặp lại. Gọi nguyên văn hoặc chép mẫu rồi điền.

| Prompt | Khi dùng |
|---|---|
| [sua-loi-tren-may-that.md](prompts/sua-loi-tren-may-that.md) | chủ máy báo triệu chứng trên điện thoại, yêu cầu test trên máy thật để sửa |
| [mot-buoc-roadmap-tu-dong.md](prompts/mot-buoc-roadmap-tu-dong.md) | "tiếp tục" / "bước kế tiếp" — chạy một bước roadmap không hỏi lại |
| [them-gate-cho-mot-bug-that.md](prompts/them-gate-cho-mot-bug-that.md) | vừa tìm ra cơ chế, cần cổng giữ nó trước khi sửa |
| [bao-cao-bay-muc.md](prompts/bao-cao-bay-muc.md) | kết thúc một bước — định dạng báo cáo duy nhất chủ máy chấp nhận |
| [cap-nhat-tai-lieu.md](prompts/cap-nhat-tai-lieu.md) | sau một bước, đồng bộ mọi tài liệu |

## Skills (`skills/`)

Kiến thức vận hành có thể nạp theo yêu cầu.

| Skill | Nội dung |
|---|---|
| [rescue-channel](skills/rescue-channel/SKILL.md) | kênh cứu hộ `QEAFRESCUE`: 47 verb thật, tên 8.3, ràng buộc batch, đường nạp p77, ba lệnh cấm |
| [glass-pixel-path](skills/glass-pixel-path/SKILL.md) | đường pixel canvas→scanout, verdict ba chiều, hợp đồng dirty-rect, hai núm sáng |
| [umos-gates](skills/umos-gates/SKILL.md) | thang cổng: chạy lệnh gì, mỗi cổng chứng minh gì và **không** chứng minh gì, setup build Windows |
| [phone-agent](skills/phone-agent/SKILL.md) | AI Phone Agent: observe→act→verify, plan file, `@name`, fixture offline |

## Bốn luật mà cả thư mục này phục vụ

1. **Không claim một gate đã chạy nếu không có stdout của nó trong phiên này.**
   Không claim ánh sáng / điểm ảnh / chạm / mở khóa / đã sửa khi chưa có phép đo
   tương ứng trên kính.
2. **Mỗi con số thuộc về artifact của nó** (`out/t/*.log:dòng`, boot suffix
   `-mN`). Một số đo trên input nào chỉ được trích cho kết luận về input đó.
3. **Kính tối thì mọi đo điểm ảnh là vô hiệu.** `dpms` và backlight phải nằm
   **cùng batch rescue** với ảnh — round-trip host dài hơn hẹn giờ idle 60 s.
   Connector `Off` = bo đó `disp_flush()` return sớm, ảnh chỉ nói shell *vẽ* gì.
4. **Không phải việc nào agent cũng xong được.** Camera/lux meter/người nhìn
   (#105), bo thứ hai (#50), adb thật trên boot UMOS (#60) cần thứ rig không có;
   nói thẳng thay vì sửa vòng quanh.

## Ràng buộc cứng khi đụng thiết bị

- Ghi **duy nhất p77 (recovery)**; p76 là boot slot của Android.
- **Chỉ có hai ảnh OS trong `out/`:** `boot-qeaf-violet-m48-noandroid.img`
  (bản đang chạy, sha256 `e2ff7df1…`) và `boot-qeaf-violet-m48.img` (nguồn
  trước khi vá chữ). Mọi ảnh `boot-qeaf-p77-install-m*` cũ đã bị xoá; đừng
  nạp lại chúng, đừng trích số đo của chúng cho bản m48.
- **Không bao giờ** eject volume cứu hộ (`-Eject` đời cũ; #65 — nó bỏ medium mà
  không reboot). `scripts/send_rescue_cmd.py` không có cờ đó và
  `scripts/prepare_rescue_flash.py` không bao giờ eject.
- **Không đọc** `/proc/tp_*` — một lần kiểm kê nodes đã giết kênh cứu hộ.
- Mất kênh giữa lượt là `ChannelGone`: phần còn lại **chưa đo**, không phải đỏ.
- `scripts/read_rescue_raw.py` có thể treo **sau khi** đã ghi capture đủ — parse
  artifact trên đĩa, **đừng gửi lại batch** (nó là write).
- Không thay backend hiện có; không commit khi chưa được yêu cầu.

## Thêm mới

- Skill: `skills/<ten>/SKILL.md`, frontmatter `name` + `description` (description
  là thứ dùng để match, phải nói rõ **khi nào dùng** và khi nào **không**).
- Prompt: `prompts/<ten>.md`, bắt đầu bằng block `>` nói lúc dùng, rồi mẫu gọi.
- Mỗi mục phải kèm đường dẫn mã hoặc artifact chứng minh nó không phải suy đoán.
- Cập nhật đúng hai bảng ở trên khi thêm file.
