---
name: glass-pixel-path
description: Debug "màn hình không hiện gì" trên violet bằng đường pixel canvas->scanout và verdict ba chiều (connector, backlight, scanout-vs-canvas) — kèm hợp đồng dirty-rect của gfx, lý do host không bao giờ thấy lỗi này, và các dụng cụ probe_lit/home_settle_probe/map_dump. Dùng khi có báo cáo kính tối, ảnh cũ, hoặc flush không hiệu quả.
---

# Đường pixel: từ canvas tới kính

Khi chủ máy báo *"không hiện gì, chỉ LED sáng"*, có **ba** khả năng tách nhau và
chúng cần bằng chứng khác nhau. Đừng kết luận cái nào nếu chưa có đủ ba cùng
lúc:

1. **Connector có được bật không** — `/sys/class/drm/card0-DSI-1/dpms`.
2. **Đèn nền có ăn dòng không** — các knob trong `/sys/class/backlight/`.
3. **Ảnh có nằm trong buffer mà crtc đang quét không** — `map_dump()` so cùng
   một điểm lấy mẫu giữa canvas và scanout, báo `scan N/M`.

Trong cây `QeafdropOs` rút gọn này, các dụng cụ `probe_lit.py`,
`home_settle_probe.py`, `autotest.py` chỉ còn ở `out/tmp/oldsrc/build/`; đường
đối thiểu còn lại là `scripts/send_rescue_cmd.py` + `tests/recovery_gui_png.py`
(`python tests\recovery_gui_png.py SCREEN.RAW out/snap 240 520`).

`build/probe_lit.py` tóm ba câu đó thành một verdict và exit code:
`1` = connector tối (mọi đọc số đều **không** là kết luận về khả năng nhìn),
`3` = knob brightness đều 0/từ chối trả lời (đường sáng, không phải đường
pixel), `4` = ảnh thiếu trong scanout, `0` = cả ba cùng đúng.

## Hợp đồng dirty-rect (nguồn của hai bug thật)

Mỗi frame đi qua đúng một chuỗi, ở
[gui/window_manager.c:1389](file:///D:/QeafivelsOfficial/QeafdropOs/gui/window_manager.c#L1389)
và [:1354](file:///D:/QeafivelsOfficial/QeafdropOs/gui/window_manager.c#L1354):

```c
dirty = gfx_dirty_take();
gfx_dirty_reset();
return hal_display_flush(dirty);
```

Ba bất biến phải giữ, và từng cái đã bị phá thật:

- **`take()` phải trả snapshot, không trả con trỏ vào accumulator.**
  [gui/gfx/gfx.c:181](file:///D:/QeafivelsOfficial/QeafdropOs/gui/gfx/gfx.c#L181).
  Trả `&g_dirty` nghĩa là `reset()` zero đúng cái struct mà `flush` sắp đọc →
  mọi incremental flush nhận `{0,0,0,0}`. Đo 2026-11-03: 15/17 flush từ boot
  rỗng, cái cuối `0x0@0,0`, và một Home đã yên **không khớp chính scanout của
  nó ở 3779/11232 điểm**.
- **Primitif đầu tiên phải được GÁN, không union.**
  [gui/gfx/gfx.c:151](file:///D:/QeafivelsOfficial/QeafdropOs/gui/gfx/gfx.c#L151)
  qua `dirty_add()`. Union với accumulator `{0,0,0,0}` nhiễm gốc 0 và sinh hộp
  sai to hơn nhưng lệch.
- **Mọi transition phải gọi `gfx_dirty_mark(NULL)`** (hộp nguyên panel) —
  [gui/window_manager.c:212](file:///D:/QeafivelsOfficial/QeafdropOs/gui/window_manager.c#L212),
  và `g_first_flush` ép nguyên panel lúc boot lẫn lúc wake.

**Chỉ hai chỗ ghi framebuffer tồn tại**: `fill_area` và `gfx_pixel`
([gui/gfx/gfx.c](file:///D:/QeafivelsOfficial/QeafdropOs/gui/gfx/gfx.c)), cả
hai đều đánh dấu dirty — nên hộp chính xác không thể che thiếu điểm. Kiểm tra
điều này **mỗi khi** thêm một primitif vẽ mới.

## Vì sao host không bao giờ thấy loại lỗi này

`bsp/host_win/board_win.c:285` bỏ tham số dirty:

```c
(void)dirty;   /* present whole; the panel is small enough to be honest */
```

Host luôn present nguyên panel, nên **mọi** test trên PC, mọi ảnh poster render
ra đều đúng kể cả khi đường flush của điện thoại hỏng hoàn toàn. Một lỗi hộp
dirty chỉ hiện trên kính. Muốn bắt nó trên PC thì phải gate **hợp đồng** chứ
không gate kết quả vẽ: xem `tests/dirty-flush-check.c` (16 check, replay đúng
chuỗi take/reset/flush và phép clamp của board).

## Đọc số trên kính

Marker `/run/qeaf-tsim-map` (dòng `cat cat /run/qeaf-tsim-map` trong batch)
mang: `seq`, `current`, `scan N/M`, `box=`, `black=`, `flush n/drop/empty`,
`last WxH@x,y f<full>`.

| Đọc thấy | Nghĩa |
|---|---|
| `scan 0/11232` | scanout **đồng ý** canvas — đường pixel sạch |
| `scan 3779/11232` | kính đang giữ ảnh cũ (buffer crtc quét ≠ canvas) |
| `flush ..., 0, N)` với N>0 | có flush rỗng — bug hộp dirty còn sống |
| `last 0x0@0,0` | flush gần nhất không chép gì |
| `drops=(n, d, e)` | `d` = bị từ chối vì kính tối, **không phải** lỗi vẽ |

## Hai núm sáng, và cái nào không được tin

Violet có **hai** knob: `panel0-backlight` và `backlight` (PMIC WLED, DT
`bl_ctrl_wled`). Cú ghi phải đi vào **mọi** knob
(`hal_display_backlight`, gate `make backlight-check` 64 check).

**Đừng chấm độ sáng từ `actual_brightness`**: `panel0-backlight` trả
`actual_brightness` = 0 ở mọi mức từng thử (4095 và 2538, cả lúc `dpms=On`),
vì panel video-mode không trả lời đọc lại từ DDIC. Tin `brightness` (thứ ta
ghi), không tin `actual_brightness`.

`glass_state()` trong [build/autotest.py:767](file:///D:/QeafivelsOfficial/QeafdropOs/build/autotest.py#L767)
**fold** `dpms` với hai knob trong cùng batch: từ m38, idle-blank giữ connector
`On` và chỉ vặn đèn về 0 (để digitizer còn nguồn), nên `On` + mọi knob `0` =
kính tối, trả `"Off"`. Batch không đọc knob thì giữ câu trả lời connector-only.
Giá trị ngoài bốn từ `{On, Standby, Suspend, Off}` trả `None` = *rig không biết*,
không phải *kính tối*.

## Dụng cụ

| Lệnh | Dùng khi |
|---|---|
| `python -u build/probe_lit.py` | verdict ba chiều, live (tự wake trong batch) |
| `python -u build/probe_lit.py --from out/report/<stem>RESPONSE.TXT` | **rig treo sau khi ghi capture** — tái lập verdict từ artifact, không đụng cáp |
| `python build/home_settle_probe.py` | Home đã yên mà `scan` vẫn ≠ 0 |
| `make dirty-flush-check` | hợp đồng take/reset/flush trên PC |
| `python -u build/autotest.py --only <i>` | một shot, kèm gate scan |

Ảnh dump xem được: `decode_map()` → PNG, và **phải mở ra nhìn** (bản `-8x.png`)
chứ không tin bộ đếm màu — `38.9% ink, 16 colours` nói có mực, không nói bố
cục đúng áp phích.

## Câu hỏi mà cáp không trả lời được

Kính "phát sáng" với **mắt người** không đo được qua kênh cứu hộ: rig không có
camera hay lux meter. Verdict `0` của `probe_lit.py` là proxy mạnh nhất, và nó
kết luận *"the pixels a person has to see are on the glass, at light the board
was told to emit"* — việc #105 vẫn là chủ máy nhìn và gật.
