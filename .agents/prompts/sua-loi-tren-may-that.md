# Prompt: sửa lỗi bằng máy thật (device-first bug fix)

> Dùng khi chủ máy báo một triệu chứng trên điện thoại ("không hiện gì", "vuốt
> không mở khóa", "đồng hồ sai giờ") và yêu cầu chạy test trên máy thật để sửa.
> Đây là chế độ mặc định của repo này: **một bug chỉ được coi là đã hiểu khi một
> phép đo trên kính kể được chuyện đó.**

## Mẫu gọi

```
test trên máy thật để sửa lỗi: <triệu chứng nguyên văn của chủ máy>
boot hiện tại: m<NN>
```

## Vòng bắt buộc (không được bỏ bước nào)

1. **Đọc lại triệu chứng nguyên văn.** Đừng dịch nó thành "UI hỏng". Ghi đúng câu
   chủ máy nói vào báo cáo; nhiều ca (M12) chủ máy nói "chỉ LED sáng" và có tới
   **ba** nguyên nhân khác nhau chồng lên.
2. **Xác định trạng thái kính TRƯỚC khi kết luận gì.** `dpms` và hai knob backlight
   phải nằm **cùng batch rescue** với phép đọc. Connector `Off` nghĩa là *mọi số
   đo điểm ảnh dưới này không phải kết luận về khả năng nhìn* — bo đó
   `disp_flush()` return sớm khi panel blank.
   → skill `rescue-channel`, `glass-pixel-path`.
3. **Tái hiện bằng dụng cụ, không bằng mô tả.** Chọn đúng probe:
   `build/probe_lit.py` (verdict ba chiều), `build/home_settle_probe.py`
   (canvas vs scanout khi đã yên), `python -u build/autotest.py --only <i>`
   (một shot), `build/fetch_klog.py` (kernel ring). Trong cây này những script đó
   nằm ở `out/tmp/oldsrc/build/`; nếu không có, tối thiểu dùng
   `scripts/send_rescue_cmd.py` + `scripts/shot_png.py`. Lưu stdout vào
   `out/t/<ten>-m<NN>.log` để con số sau này trace được về boot nào.
4. **Tìm cơ chế, không tìm triệu chứng.** Đọc code tới tận chỗ ghi/flush/commit
   cuối cùng. Câu hỏi phải trả lời được: *host gate nào lẽ ra đã đỏ, và vì sao nó
   không đỏ?* Nếu câu trả lời là "host không thấy được", đó chính là bằng chứng
   thiếu một gate — quay lại bước 5.
5. **Viết gate mà cổng đó làm ĐỎ trên bug cũ, và xanh trên bản sửa.**
   Đây là bước separating thật: một gate viết sau khi đã sửa mà chưa từng đỏ là
   gate trang trí. Nối nó vào một target của `Makefile` (như `guard-test`) chứ
   không để nó là script chạy tay.
6. **Sửa tối thiểu.** Không refactor, không cờ tương-thích-ngược, không fallback
   cho trường hợp không thể xảy ra. Hai lỗi trong M12 là hai sửa riêng biệt
   (`take()` trả snapshot; primitif đầu phải GÁN chứ không union) và mỗi cái có
   measurement của riêng nó.
7. **Dựng + nạp + đo lại trên chính kính.** Pipeline của cây này:
   `python scripts/mk_initramfs.py` → `python scripts/mk_bootimg_violet.py` →
   `python scripts/strip_android_from_image.py --all` →
   `python scripts/validate_bootimg.py` → `make guard-test` (phải xanh) →
   `python scripts/prepare_rescue_flash.py --volume F:\ --commit`
   (không bao giờ `-Eject`, không bao giờ ghi gì ngoài p77) → boot lại → chạy lại
   đúng probe ở bước 3 → so từng dòng, không so bằng đồng hồ tường.
8. **Nói rõ cái gì CHƯA chứng minh được.** Ví dụ: rig không có camera/đo sáng,
   nên "kính phát sáng với mắt người" không phải thứ cáp trả lời được
   (việc #105). Ghi thẳng vào docs, đừng để nó thành claim tốt đẹp.

## Ràng buộc cứng khi chạy vòng này

- Không sửa backend hiện có để lấy một phép đo.
- Kính tối thì phải đánh thức **trong cùng batch** với ảnh (`shot_batch(..., wake=True)`),
  vì idle timer của shell ngắn hơn round-trip của host.
- Không đọc `/proc/tp_*` (đã giết kênh cứu hộ một lần).
- Kênh rescue chết giữa lượt → `ChannelGone`: các bước còn lại là **chưa đo**,
  không phải đỏ; dừng và nói rõ shot nào chưa chạy.
- `scripts/read_rescue_raw.py` đọc raw sector; nếu lượt đọc treo SAU KHI đã ghi
  capture đủ, parse artifact trên đĩa
  (`probe_lit.py --from out/report/<stem>RESPONSE.TXT`), **không** gửi lại batch.

## Kết thúc

Xuất báo cáo bảy mục theo prompt `bao-cao-bay-muc.md`, trong đó mọi con số có
`out/...` + số dòng.
