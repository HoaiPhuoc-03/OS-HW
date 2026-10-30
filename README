# Lab 1 - File & Process Management

Cài đặt syscall `int sysinfo(struct sysinfo *)` trên xv6 RISC-V để thu thập:

- File đang mở toàn hệ thống: slot, type, mode, reference count và các PID tham chiếu.
- Process có `state != UNUSED`: PID, trạng thái, PID cha, số lần được lập lịch
  và danh sách file đang mở.

`fd` là chỉ số trong bảng file của từng process; `slot` là chỉ số open-file
object trong bảng file toàn hệ thống. Nhiều fd có thể cùng trỏ tới một slot.

## Môi trường và chạy

Cần Linux, `make`, RISC-V GNU toolchain (`riscv64-unknown-elf-*` hoặc
`riscv64-linux-gnu-*`) và `qemu-system-riscv64`.

Chạy tại thư mục gốc dự án:

```sh
make qemu          # mặc định 3 CPU
# Hoặc: make qemu CPUS=1
```

Thoát QEMU: nhấn `Ctrl+A`, thả phím, rồi nhấn `X`.

## Kiểm thử

Tại dấu nhắc `$` của xv6:

```sh
sysinfotest
sysinfotest show
trace 8388608 sysinfotest show
```

Lần lượt: chạy toàn bộ test, in snapshot hiện tại, và in snapshot kèm trace
riêng syscall `sysinfo` (số 23).

Các test bao gồm open/dup/close, pipe, fork/exit/wait, trạng thái process,
lập lịch, đầy bảng fd/process, tải đồng thời và con trỏ không hợp lệ.
Kết quả đã kiểm chứng: `19 PASS, 0 FAIL` trên 1, 3 và 8 CPU;
host unit test đạt 41 checks, 0 failures.

## Mã nguồn và giới hạn

- `kernel/sysinfo.h`: cấu trúc dữ liệu dùng chung giữa kernel và user.
- `kernel/sysproc.c`: xử lý syscall; `kernel/file.c`, `kernel/proc.c`: thu thập dữ liệu.
- `user/sysinfotest.c`: chương trình kiểm thử và in kết quả.
- `docs/sysinfo.md`: thiết kế, khóa và hướng dẫn chạy host unit test.

Snapshot được thu thập theo từng bảng, không nguyên tử. Khi các process
mở/đóng file đồng thời, thông tin có thể thiếu hoặc ghép sai nếu slot được
tái sử dụng giữa hai lượt thu thập.

## Dọn trước khi đóng gói

```sh
make clean
```

Đóng gói source sau bước này; chạy lại `make` sẽ sinh lại file build.
Lưu ý: quy tắc `clean` cũng xóa các file `*.zip` ở thư mục gốc dự án.
