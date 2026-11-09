# Prompt: viết gate cho một bug thật

> Dùng ngay sau khi tìm ra cơ chế của một lỗi, **trước khi** sửa. Một gate viết
> xong mà chưa từng đỏ trên bản code cũ thì không phải gate — nó là minh họa.

## Yêu cầu tối thiểu

1. **Tái hiện đúng hợp đồng đã vỡ, không tái hiện triệu chứng.**
   `tests/dirty-flush-check.c` không vẽ UI rồi so ảnh; nó replay
   `take → reset → flush` và chép nguyên số học clamp của board
   (`PW 1080 / PH 2340`) — vì đó là chỗ hỏng.
2. **Chứng minh gate nhìn thấy:** chạy trên bản code cũ, phải ĐỎ. Ghi lại số
   check đỏ và artifact (`dirty-flush-check đỏ 9 check trên bản trước khi sửa`).
3. **Control âm trong cùng lượt chạy.** Sample rỗng, batch chỉ một bucket, A/B
   một phía = dụng cụ hỏng chứ không phải kết quả.
4. **Vật lý của thiết bị nằm trong gate, không nằm trong lời kể.** Nếu host gate
   và kính disagree, thường là gate thiếu trục thời gian (idle timer của shell
   ngắn hơn round-trip của host). Mang cả biến thể nhanh + chậm kèm wait, và nói
   rõ bằng chứng đến từ uinput, từ digitizer hay ngón thật.
5. **Nối vào thang cổng**, không để script chạy tay: thêm target vào `Makefile`
   (`.PHONY` + recipe, như `guard-test` / `storage-check`).

## Khuôn một gate host

```c
/* tests/<rule>-check.c
 * why this exists, with the measurement: one paragraph, the artifact path,
 * and what the code did before. */
#define PW 1080          /* #define, not `static const int`: a VLA here trips
                          * -Wgnu-folding-constant under -Werror */
static uint16_t fb[(size_t)PW * PH];

static const struct umos_rect *frame_flush_rect(void) { /* the real sequence */ }
```

- Build: `$(HOSTCC) -std=c99 $(WARNINGS) -Werror -I. -o out/<name>.exe tests/<name>.c <module>.c`
  và pin `ZIG_GLOBAL_CACHE_DIR=$(OUT)/zig-global` như các target khác.
- `main()` in `"<name>: %d checks, %d failures"` và `return failures ? 1 : 0`.
- Test module nào thì **link đúng module đó**, không chép lại hàm sang file test
  (chép lại = gate so bản chép, không so bản shipped).

## Khi lỗi nằm ở đường chỉ hiện trên thiết bị

Host không thấy được flush rỗng vì `bsp/host_win/board_win.c:285` bỏ tham số
dirty và present cả panel. Hai lựa chọn, và repo này dùng cả hai:

- **gate hợp đồng trên host** (con số, con trỏ, thứ tự — như dirty-flush-check);
- **đo trên kính** qua marker: `scan N/M` (canvas vs scanout), `flush n/drop/empty`,
  `last WxH@x,y f<full>` trong `/run/qeaf-tsim-map`.

## Danh sách tự kiểm trước khi báo "đã có gate"

- [ ] Gate đỏ trên bug cũ, và tôi có artifact của lượt đỏ đó.
- [ ] Gate xanh trên bản sửa, chạy trong `make guard-test` (hoặc target vừa thêm
      vào `Makefile`).
- [ ] Không có con số nào trong gate là tọa độ cứng của một bo khác.
- [ ] Lời comments trong file test nói *vì sao* nó tồn tại, kèm phép đo.
- [ ] Docs (`README.md`, `.agents/skills/umos-gates/SKILL.md`) đã ghi số check
      mới + artifact.
