# Project 1: system call sysinfo

`int sysinfo(struct sysinfo *info)` (syscall số 23) chụp thông tin hệ thống
đang chạy và chép về user space. Trả 0 nếu thành công, -1 nếu lỗi.

- Các open-file object trong toàn hệ thống (`ftable.file[]` có `ref > 0`):
  slot, type, mode, reference count, danh sách PID đang tham chiếu.
- Các process có `state != UNUSED`: pid, state, pid của cha, số lần được
  lập lịch, danh sách file đang mở (fd và slot của object).

## Phân công

| Phần | Người | File |
|---|---|---|
| ABI, đăng ký syscall, kiểm tra con trỏ user, liên kết file → PID, `copyout`, chương trình test | Phước | `kernel/sysinfo.h`, `kernel/syscall.[ch]`, `kernel/sysproc.c`, `kernel/main.c`, `user/user.h`, `user/usys.pl`, `user/sysinfotest.c`, `Makefile` |
| Thu thập bảng file: `collect_file_info()`, `sysinfo_file_slot()` | Tài | `kernel/file.c` |
| Bộ đếm lập lịch `nsched`, thu thập bảng process: `collect_proc_info()` | Phát | `kernel/proc.c`, `kernel/proc.h` |

## ABI (`kernel/sysinfo.h`)

Dùng chung cho kernel và user, không chứa con trỏ hay lock. Với NFILE=100,
NPROC=64, NOFILE=16 trên RV64:

| Kiểu | Kích thước (byte) |
|---|---:|
| `struct sysinfo_fd` | 8 |
| `struct sysinfo_file` | 276 |
| `struct sysinfo_proc` | 152 |
| `struct sysinfo` | 37336 |

- `file_count`, `proc_count`: số phần tử có nghĩa trong `files[]`, `procs[]`
  (mảng packed, chỉ `[0, count)` có nghĩa).
- `slot`: chỉ số của object trong `ftable.file[]`, **không phải** fd.
- `type`: `SYSINFO_NONE/PIPE/INODE/DEVICE`, cùng giá trị với `FD_*` của xv6.
- `mode = (readable ? SYSINFO_READ : 0) | (writable ? SYSINFO_WRITE : 0)`.
  xv6 không lưu cờ `O_CREATE`/`O_TRUNC` trong `struct file`.
- `pids[]`: mỗi PID xuất hiện một lần, kể cả khi process có nhiều fd cùng trỏ
  tới object (sau `dup()`), nên `ref` có thể lớn hơn `npids`.
- `state`: `SYSINFO_UNUSED..SYSINFO_ZOMBIE`, cùng giá trị với `enum procstate`
  (kiểm tra bằng `_Static_assert` trong `proc.c` và `sysproc.c`).
- `ppid = 0` khi không có cha (chỉ init).
- `schedule_count`: số lần `scheduler()` chọn process để chạy.
- `fds[]`: cặp (fd, slot) cho mỗi fd đang mở, `nfd` phần tử.

Struct 37 KB lớn hơn kernel stack và user stack (mỗi cái 4 KB): kernel dùng
một buffer tĩnh, chương trình user phải khai báo biến toàn cục.

## Luồng xử lý `sys_sysinfo()`

1. `argaddr()` lấy địa chỉ, `sysinfo_user_buffer()` kiểm tra trước toàn bộ
   vùng nhớ đích: khác NULL, nằm trong `p->sz` và dưới `MAXVA` (dùng phép trừ
   để tránh tràn số), mọi trang đều có `PTE_V | PTE_U | PTE_W`. Sai thì trả -1
   và không ghi byte nào.
2. Lấy sleeplock `sysinfo_lock` bảo vệ buffer tĩnh `snapshot`, rồi `memset`.
3. `collect_file_info()` (Tài): dưới `ftable.lock`, chép slot, type, mode,
   ref của mọi object có `ref > 0`.
4. `collect_proc_info()` (Phát): lấy `wait_lock` rồi lần lượt từng `p->lock`,
   chép pid, state, ppid, `nsched` và đổi `p->ofile[fd]` thành slot bằng
   `sysinfo_file_slot()`.
5. `sysinfo_link()` (Phước): kiểm tra cấu trúc và dựng danh sách PID cho
   từng object từ `fds[]` của các process.
6. `copyout()` về user, nhả sleeplock.

## Bộ đếm lập lịch

- Trường `uint64 nsched` trong `struct proc`, thuộc nhóm trường do `p->lock`
  bảo vệ.
- Tăng trong `scheduler()` ngay sau `p->state = RUNNING` và trước `swtch()`,
  khi đang giữ `p->lock`: đây là nơi duy nhất process chuyển từ RUNNABLE sang
  RUNNING, nên mỗi lần lập lịch được đếm đúng một lần.
- Đặt về 0 trong `allocproc()` (process con không thừa hưởng bộ đếm của cha)
  và `freeproc()`. `exec()` không đặt lại vì vẫn là cùng một process.

## Khóa và mức nhất quán

- Không giữ spinlock nào giữa hai collector. Thứ tự khóa:
  `sysinfo_lock` (sleeplock) → `wait_lock` → `p->lock`, giống `wait()`.
  `ftable.lock` không bao giờ được giữ cùng lúc với `p->lock` (fork giữ
  `np->lock` khi gọi `filedup()`, nên thứ tự ngược lại có thể deadlock).
- `sysinfo_file_slot()` chỉ so sánh địa chỉ, không lấy khóa, không đọc nội
  dung object, nên gọi được khi đang giữ `p->lock`.
- `wait_lock` giữ cho `p->parent` không đổi và cho process cha không bị
  `freeproc()` trong lúc đọc `parent->pid`.
- Snapshot là *best-effort*, không đồng thời tuyệt đối:
  - Bảng file được chụp trước bảng process. Nếu `open()` hoặc `exit()`
    (`exit` đóng file trước rồi mới xóa `ofile[]`) xảy ra giữa hai lần chụp,
    một fd có thể trỏ tới object không có trong snapshot; `sysinfo_link()`
    bỏ fd đó, snapshot vẫn nhất quán.
  - `sys_close()` xóa `ofile[fd]` trước rồi mới giảm `ref`, nên trong khoảnh
    khắc đó `ref` lớn hơn số fd quan sát được.
  - Một slot bị đóng rồi được cấp lại giữa hai lần chụp thì không phát hiện được.
  - Khi hệ thống yên lặng (không process nào đang open/close/exit), `ref` của
    mỗi object bằng đúng tổng số fd trỏ tới nó; test kiểm tra chặt điều này.

## Các trường hợp `sysinfo()` trả -1

| Nơi | Điều kiện | Ghi chú |
|---|---|---|
| `sysinfo_user_buffer()` | địa chỉ NULL, vượt `p->sz` hoặc `MAXVA`, tràn số, có trang chưa map / trang guard / trang chỉ đọc | không ghi gì vào bộ nhớ user |
| `sysinfo_collect()` | một collector trả khác 0 | hai collector hiện luôn trả 0 |
| `sysinfo_link()` | count ngoài miền, slot/PID/fd trùng, giá trị type/state/mode sai, quá NPROC PID cho một object | kiểm tra phòng thủ; collector hiện tại không tạo ra dữ liệu như vậy |
| `copyout()` | không chép được | không xảy ra sau khi đã kiểm tra trước ở bước 1 |

fd trỏ tới object không có trong snapshot **không** làm lời gọi thất bại
(xem mục trên).

## Build và chạy

```sh
make clean
make qemu            # 3 CPU (mặc định)
make qemu CPUS=1
```

Trong xv6:

```
$ sysinfotest              # chạy 19 nhóm test, in bảng ở các tình huống chính
$ sysinfotest show         # chỉ in snapshot hiện tại
$ cat README | sysinfotest show   # thấy hai process nối nhau qua pipe
```

`Makefile` thêm `-march=rv64g`: toolchain RISC-V mới (GCC 15) mặc định sinh
lệnh của profile RVA23 (ví dụ `c.mul`) mà CPU mặc định của QEMU không chạy được.

## Danh sách test (`user/sysinfotest.c`)

| # | Test | Kiểm tra |
|---|---|---|
| 01 | basic | process gọi là RUNNING; in snapshot ban đầu |
| 02 | init and console | init: pid 1, ppid 0, SLEEPING, fd 0-2 cùng một object console DEVICE rw; cha của test SLEEPING; ref khớp số fd |
| 03 | open | object mới INODE rw ref 1; O_RDONLY → `r-`, O_WRONLY → `-w` |
| 04 | dup | cùng slot, ref +1, PID không lặp |
| 05 | close | ref giảm; đóng fd cuối thì object biến mất |
| 06 | pipe | hai object PIPE, đầu đọc `r-`, đầu ghi `-w` |
| 07 | fork | con dùng chung object, ref nhân đôi, con SLEEPING, ppid đúng |
| 08 | multiple process references | 2 con, 2 fd trên một object: ref = 2 × 3, npids = 3 |
| 09 | exit/zombie/wait | ZOMBIE vẫn được đếm, không còn fd, ppid giữ nguyên; wait xong thì biến mất |
| 10 | kill sleeping process | process ngủ trên pipe bị kill thoát với status -1 |
| 11 | reparent to init | cha thoát trước, cháu có ppid = 1 và được init dọn |
| 12 | scheduling count | bộ đếm tăng; process con bắt đầu từ 0; không tăng khi SLEEPING |
| 13 | runnable under CPU contention | NCPU+1 process tính toán: có RUNNABLE, bộ đếm tăng nhờ ngắt timer |
| 14 | full descriptor table | đủ NOFILE fd đều được báo cáo |
| 15 | full process table | fork tới khi đầy: đúng NPROC process, ref khớp số fd |
| 16 | busy system | 300 lần gọi trong khi 3 process liên tục open/pipe/fork/exit: không lần nào lỗi |
| 17 | invalid pointers | NULL, chưa map, tràn số, thiếu chỗ, trang guard, trang chỉ đọc: -1 và không ghi |
| 18 | repeated calls | gọi lặp, buffer không căn lề, hai process gọi đồng thời |
| 19 | trace regression | syscall `trace` của bài trước vẫn chạy, có tên `sysinfo` |

Với `LAB_SYSCALL`, `uvmalloc()` không xóa trang nhớ user về 0 (phục vụ bài
attack trước), nên BSS của chương trình user có thể chứa rác; `sysinfotest`
tự khởi tạo mọi biến toàn cục.

## Unit test trên host

Kiểm tra layout ABI, `sysinfo_link()` và `sysinfo_user_buffer()` của chính
`kernel/sysproc.c` (hàm `walk()` được thay bằng bản giả):

```sh
cc -O1 -Wall -Werror -fno-builtin -ffunction-sections -fdata-sections \
  -Wl,--gc-sections tests/sysinfo_unit.c -o /tmp/sysinfo-unit
/tmp/sysinfo-unit
```

## Kết quả kiểm thử (27/10/2026)

Môi trường: Ubuntu 26.04 trên WSL2, GCC 15.2 (`riscv64-linux-gnu`), QEMU 10.2.

- `sysinfotest`: 19 PASS, 0 FAIL ở cả 10 lần chạy (1 CPU × 2, 3 CPU × 5,
  4 CPU × 2, 8 CPU × 1).
- Stress (chương trình tạm, không nằm trong bài nộp): 4 process liên tục
  open/pipe/fork/exit trong khi 2 process gọi `sysinfo()` 3000 lần mỗi
  process; 0 lần lỗi ở cả 3 lần chạy (3 và 4 CPU). Trước khi `sysinfo_link()`
  bỏ qua fd trỏ tới object không có trong snapshot, khoảng 0,3% số lần gọi
  trả -1 với 3 CPU.
- `usertests -C`: 55 test OK; chỉ `sbrkmuch` và `bsstest` lỗi. Hai test này
  cũng lỗi trên mã nguồn gốc vì `uvmalloc()` không xóa trang nhớ khi build với
  `LAB_SYSCALL`.
- Unit test trên host: 41 checks, 0 failures.
