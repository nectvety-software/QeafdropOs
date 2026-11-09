---
name: phone-agent
description: Điều khiển điện thoại qua AI Phone Agent (build/phone_agent.py) — observe -> act -> verify, hai cầu rescue/adb, quy tắc bắt buộc expect, @name rect từ boot hiện tại, plan file, fixture offline cho gate, và vì sao "command đã được ack" không phải kết quả. Dùng khi muốn dẫn động máy thật nhiều bước mà vẫn có bằng chứng.
---

# AI Phone Agent

Vòng **observe → act → verify** có CLI fronts, đi trên hai debug bridge. Trong cây `QeafdropOs` rút gọn này, `build/phone_agent.py` **chỉ còn `__pycache__/*.pyc`** (nguồn đã xóa khi dọn `out/`). Dùng cứu hộ qua `scripts/read_rescue_raw.py` + `out/auto_test.py` cho test tự động hiện tại.

| Cầu | Với tới | Ghi chú đo được |
|---|---|---|
| `rescue` | UMOS | batch dòng vào `QEAF.CMD`, một `RESPONSE.TXT` về; đọc bằng `scripts/read_rescue_raw.py` vì góc nhìn `F:` mount là **cũ** |
| `adb` | Android | dưới UMOS, adb là **boot flag** (`qeaf.adb`) và chưa image nào của ta mang nó → trên boot UMOS cầu này không trả lời gì; `status` nói đúng vậy thay vì bịa thiết bị |

 `rescue` là cầu **duy nhất** tới Homo/QeafdropOs và chỉ mang các verb hiện tại:
`help status wifi sync usb rotate lang echo ls kmsg snap reboot flash`.
`tap/swipe/hold/tsim` **không** còn trong `fs/rescue.c` (UMOS cũ có). `adb` trên boot Homo
không trả lời gì. Dùng `out/auto_test.py` cho test tự động qua rescue.
Tra cứu: hakios-xnu-kernel/hakios-metal là XNU+Metal không dùng trên SDM660 violet (hiện DRM+LVGL), dipper-xnu WCN3990 sống trên XNU nhưng không port Linux cho violet, luma/omarchy-mobile là Linux Fedora/mainline không có profile violet.

## Vì sao verify là bắt buộc, không phải style

Ngày 2026-11-02: máy nhận ngón thật, mở khóa (`out/t/umoslog-19701.txt` in
`lock: unlock (up=354 need=284 ...)`) mà chủ máy **vẫn không thấy gì**. "Lệnh
đã được ack" chưa bao giờ là câu hỏi. Agent từ chối mọi action không đặt tên
một trong bốn nhân chứng:

1. `current=` của marker `/run/qeaf-tsim-map`
2. `dpms` của connector (fold với backlight thành glass state của rig)
3. dòng `lock:` / `home:` của shell
4. bức ảnh (`snap`)

## Lệnh

```bash
py -3.12 -u build/phone_agent.py status
py -3.12 -u build/phone_agent.py observe --json      # seam cho vòng tự động
py -3.12 -u build/phone_agent.py act "tap @navhome" --expect window=Home
py -3.12 -u build/phone_agent.py run plans/open_drawer.txt
py -3.12 -u build/phone_agent.py repl
py -3.12 -u build/phone_agent.py --channel adb shell "getprop ro.build.version.release"
```

hoặc qua make: `make agent PLAN=plans/open_drawer.txt`, `make agent-dry` (in batch,
**không đọc gì** — resolve một `@name` tự nó đã là một lần đọc điện thoại),
`make agent-live` (prompt từng bước).

## Plan file

Một bước một dòng: `action | verifier [| verifier]`, `#` comment, `retry=N` bất
kỳ đâu trong dòng. **Bước không có verifier bị từ chối trước khi gửi gì**, nên
plan không thể âm thầm thoái hóa thành danh sách keystroke báo thành công.

`@name` resolve từ các dòng rect mà **boot này** in ra (`T <name> x y w h`,
`build/autotest.py harvest_rects`), **không bao giờ** từ pixel nhớ lại: widget mà
điện thoại không vẽ sẽ fail **theo tên**, thay vì chạm vào chỗ trống.

Verifier phải thuộc đúng bảy từ trong `EXPECTS` của
[build/phone_agent.py:79](file:///D:/QeafivelsOfficial/QeafdropOs/build/phone_agent.py#L79)
— `window` `dpms` `log` `changed` `page` `rect` `scan` — và agent từ chối dòng có
verifier ngoài bảng đó. Hình thái hay dùng: `window=Home`, `dpms=On`, `changed`
(seq của marker dịch — khác biệt giữa một cửa sổ **đổi** và một lệnh được ack),
`log~<regex>` khớp chính dòng của shell, `scan` (ảnh chỉ là bằng chứng của kính
khi canvas của shell và buffer mà display controller quét **đồng ý** nhau).

Xem mẫu đã chú thích đầy đủ:
[plans/open_drawer.txt](file:///D:/QeafivelsOfficial/QeafdropOs/plans/open_drawer.txt).

## Chứng minh offline (dùng cho gate)

```bash
py -3.12 -u build/phone_agent.py --fixture out/report/<harvest> act ... --expect ...
```

`autotest_selftest.py:check_phone_agent()` dùng đúng đường này để chứng minh lời
từ chối là **load-bearing** mà không cần điện thoại. Planner cố tình **không**
nằm trong `phone_agent.py`: việc của agent là trao cho model (hoặc người) một
quan sát đáng tin và một action tự chịu trách nhiệm — vòng tự động đọc
`observe --json`.
