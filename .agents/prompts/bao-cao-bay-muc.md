# Prompt: báo cáo bảy mục theo mốc

> Định dạng **duy nhất** mà chủ máy chấp nhận cho mỗi bước hoàn thành. Dùng khi
> kết thúc một vòng sửa lỗi / thêm tính năng / một bước roadmap, kể cả khi bước
> đó chỉ đo được chứ không sửa gì.

## Khung

```
## Bước
<một câu: bước này là gì, thuộc milestone nào (M##)>

## Đã triển khai
<cái gì đổi, ở file nào, vì sao đổi. Mỗi chỗ có [đường:dòng].>

## Test mới
<gate/dụng cụ nào viết thêm, và bằng chứng nó bắt được lỗi: đỏ trên bản cũ,
xanh trên bản sửa.>

## Gate
<bảng: cổng | kết quả | artifact (đường dẫn:dòng)>

## Android gate
<thứ gì chạy trên điện thoại thật: ảnh nào nạp, đo gì, số đọc được là bao,
chủ máy xác nhận gì.>

## Giới hạn còn lại
<việc #NN còn mở, rig không tự đo được gì, lỗi rig còn tồn tại. Nói thẳng.>

## Bước kế tiếp được phép
<một bước cụ thể, không phải danh sách mong muốn.>
```

## Luật khi điền khung

1. **Mọi con số trace về artifact.** "519 checks, all green" phải kèm
   `out/t/umos-verify-m37.log:56`. Không có artifact thì phải ghi là *chưa đo*.
2. **Số thuộc về input mà nó đo.** Không trích số của boot `m37` cho nhận định
   về boot `m38`; không tái dùng một con số qua các lượt chạy với phương pháp khác.
3. **Không claim thay dụng cụ.** Đã chạy gate ≠ gate đã quan sát gì.
   Không nói "hiển thị được", "mở khóa được", "kính sáng" nếu chưa có phép đo
   tương ứng trên kính. Connector `Off` thì mọi đo điểm ảnh là vô hiệu.
4. **Phân loại failure trước khi tóm tắt.** Một `24 failures` mà không nói 3 là
   kính-tối, 2 là flat-fill, 17 là cửa-không-tới… thì vô nghĩa.
5. **Giới hạn còn lại không được rỗng.** Nếu thấy không còn gì, nghĩa là chưa
   tìm đủ: luôn còn ít nhất câu "ai/cái gì xác nhận bằng mắt".
6. **Tiếng Việt có dấu trong chat và trong tài liệu repo.** Comment trong mã
   viết tiếng Anh không dấu. **Tên cửa sổ giữ ASCII** —
   [apps/apps.c:14](file:///D:/QeafivelsOfficial/QeafdropOs/apps/apps.c#L14)
   "Window names stay ASCII. They are what the log and the verify gate match":
   rig so `window=Home` / `current='Khoa'` vào đúng chuỗi đó, nên đặt tên có dấu
   là tự gãy gate.

## Việc cần tham chiếu

Đánh số theo task list của phiên (`#105`, `#129`, `#12`…), và ghi vào
`README.md` cho mục tính năng/ảnh OS, `.agents/` cho luật làm việc — mỗi mục kết
thúc bằng một khối "**Chưa chứng minh, nói thẳng.**".
