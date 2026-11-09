# Prompt: cập nhật tài liệu sau một bước

> Dùng khi một bước đã xong và **mọi** tài liệu phải theo. Tài liệu của repo này
> không phải tóm tắt miễn phí: nó là nơi một con số sống sau khi phiên tắt.

## Bản đồ tài liệu — sửa cái nào khi

| File | Ngôn ngữ | Sửa khi |
|---|---|---|
| [README.md](file:///D:/QeafivelsOfficial/QeafdropOs/README.md) | Việt, không dấu | hợp đồng layer/HAL đổi, cổng mới vào thang build, lệnh người dùng phải gõ, **ảnh OS hiện tại** đổi |
| [.agents/README.md](file:///D:/QeafivelsOfficial/QeafdropOs/.agents/README.md) | Việt có dấu | một **luật làm việc** hoặc một **ràng buộc của rig** vừa được học |
| [.agents/skills/umos-gates/SKILL.md](file:///D:/QeafivelsOfficial/QeafdropOs/.agents/skills/umos-gates/SKILL.md) | Việt có dấu | thang cổng đổi: lệnh, số check, đường dẫn artifact |
| [.agents/skills/rescue-channel/SKILL.md](file:///D:/QeafivelsOfficial/QeafdropOs/.agents/skills/rescue-channel/SKILL.md) | Việt có dấu | verb/kênh nạp đổi, **ảnh mặc định** đổi, tên 8.3 hoặc ràng buộc batch đổi |
| [.agents/skills/glass-pixel-path/SKILL.md](file:///D:/QeafivelsOfficial/QeafdropOs/.agents/skills/glass-pixel-path/SKILL.md) | Việt có dấu | đường pixel canvas→scanout hoặc verdict ba chiều đổi |
| [.agents/skills/phone-agent/SKILL.md](file:///D:/QeafivelsOfficial/QeafdropOs/.agents/skills/phone-agent/SKILL.md) | Việt có dấu | quy trình observe→act→verify đổi |
| [.agents/prompts/](file:///D:/QeafivelsOfficial/QeafdropOs/.agents/prompts) | Việt có dấu | quy trình làm việc đổi; bản đồ tài liệu trong file này phải khớp cây thật |
| [Makefile](file:///D:/QeafivelsOfficial/QeafdropOs/Makefile) | — | target/cổng đổi; `guard-test` là danh sách gate cho ảnh |
| [tests/](file:///D:/QeafivelsOfficial/QeafdropOs/tests) | — | gate mới, số check đổi, hoặc artifact mẫu đổi |

Cây này **không có** `docs/umos/`; đừng trỏ tài liệu vào đó. Tài liệu tính năng
duy nhất là `README.md`, tài liệu phương pháp là `.agents/`.

## Bảy quy tắc viết

1. **Mọi con số kèm artifact + số dòng.** `519 checks, all green
   (out/t/umos-verify-m37.log:56)`. Không có artifact → ghi *chưa đo*.
2. **Số thuộc về boot của nó.** Đuôi `-mN` là danh tính; đừng lấy số của `m37`
   gắn cho nhận định về `m38`.
3. **Mỗi mục kết thúc bằng khối "Chưa chứng minh, nói thẳng."** Liệt kê đúng thứ
   rig không với tới (#105 photon, #114 DRM debugfs), không viết an ủi.
4. **Ghi cả cơ chế失败.** Bảng "đo → vì sao sai → sửa" trong README có giá trị
   hơn mô tính năng: nó chặn người sau thử lại cách cũ.
5. **Comment trong mã: tiếng Anh, không dấu.** Tên cửa sổ trong `apps/apps.c`
   giữ ASCII vì log và verify gate khớp vào đó.
6. **Không tự bịa lệnh.** Trước khi viết một lệnh build/probe, `grep` nó trong
   `Makefile` / `build/` hoặc chạy `make help`.verb nào đưa vào tài liệu thì lấy
   từ bảng dispatch thật, không từ trí nhớ.
7. **File này phải giải thích được vì sao nó tồn tại bằng một phép đo,** nếu
   không nó là tài liệu trang trí.

## Kiểm tra trước khi xong

- [ ] `grep -n "checks" <file>` — mọi con số check còn đúng với lượt chạy mới nhất?
- [ ] Có claim nào về "đã hiển thị / đã mở khóa / đã nạp" mà không trỏ artifact?
- [ ] Số trong bảng cổng của `.agents/skills/umos-gates/SKILL.md` còn khớp README?
- [ ] `.agents/README.md` đã liệt kê đủ prompt + skill đang có?
- [ ] Mọi đường dẫn ảnh còn trỏ file **đang tồn tại**; sha256 và kích thước khớp
      `Get-FileHash`; chỉ còn đúng cặp ảnh m48 (bản chạy + bản trước vá chữ)?
- [ ] Việc còn mở (#105, #12, …) được ghi là còn mở ở README và trong skill liên quan?
