# Lab 2 — Memory

Đồ án mở rộng xv6 RISC-V với ba chức năng:

| Mục | Chức năng |
| --- | --- |
| **4.1 — Enhance Sysinfo** | `sysinfo()` giữ thông tin file và tiến trình của Lab 1, đồng thời trả thêm `free_memory_bytes`. Kernel giữ khóa allocator khi đếm các trang vật lý còn rảnh. |
| **4.2 — Print a Page Table** | `vmprint()` in cây bảng trang Sv39 với chỉ số, giá trị PTE và địa chỉ vật lý của từng mục hợp lệ. `execp()` chỉ in bảng trang sau khi nạp chương trình thành công. |
| **4.3 — Detect Accessed Pages** | `pgaccess(start, npages, mask)` trả bitmap truy cập cho 1 đến 32 trang, với trang đầu ứng với bit 0. Kernel xóa các bit `PTE_A` đã ghi nhận sau khi chép bitmap thành công về user. |

## Build và chạy

Cần Linux, `make`, GNU toolchain RISC-V (`riscv64-unknown-elf-*` hoặc
`riscv64-linux-gnu-*`) và `qemu-system-riscv64`.

Từ thư mục gốc của repo, khởi động xv6 với 3 CPU ảo bằng lệnh:

```sh
make qemu CPUS=3
```

Thoát QEMU bằng cách nhấn `Ctrl+A`, thả phím, rồi nhấn `X`.

## Demo trên xv6

Chạy các lệnh sau tại dấu nhắc `$` của xv6.

| Lệnh | Kết quả mong đợi |
| --- | --- |
| `sysinfotest show` | Hiển thị số byte RAM vật lý còn trống cùng bảng file và tiến trình. |
| `sysinfotest` | `20 PASS, 0 FAIL`. Nhóm 20 kiểm tra RAM trống giảm khi cấp phát và tăng khi giải phóng. |
| `vmprinttest` | Chạy `echo hello` qua `execp()`, in bảng trang rồi in `hello`. |
| `vmprinttest x` | Chạy cùng chương trình qua `exec()` thường và chỉ in `hello`. |
| `pgaccesstest` | `31 PASS, 0 FAIL`. Kiểm tra đọc, ghi, bit 31, xóa bit sau khi quét và từ chối đối số không hợp lệ. |

Hai bộ test `sysinfotest` và `pgaccesstest` đã đạt trên QEMU với 1, 3 và 8 CPU.
Địa chỉ vật lý và số byte RAM trống có thể khác giữa các lần chạy.

Để bật trace riêng cho từng syscall, chạy trong xv6:

```sh
trace 8388608 sysinfotest show
trace 16777216 vmprinttest
trace 33554432 pgaccesstest
```

## Kiểm thử Sysinfo trên Linux

Chạy từ thư mục gốc của repo. Kết quả mong đợi là `46 checks, 0 failures`.

```sh
cc -O1 -Wall -Werror -fno-builtin -ffunction-sections -fdata-sections \
  -Wl,--gc-sections tests/sysinfo_unit.c -o /tmp/os-hw-sysinfo-unit
/tmp/os-hw-sysinfo-unit
```

## Mã nguồn và tài liệu

- [Allocator](kernel/kalloc.c), [ABI Sysinfo](kernel/sysinfo.h) và
  [handler Sysinfo](kernel/sysproc.c) triển khai mục 4.1.
- [Các hàm bảng trang](kernel/vm.c) và [handler execp](kernel/sysfile.c)
  triển khai in bảng trang theo yêu cầu và phát hiện truy cập trang.
- [Thiết kế Sysinfo của Lab 1](docs/sysinfo.md) mô tả cách thu thập dữ liệu file
  và tiến trình. [Chương 3 của sách xv6](https://mit-pdos.github.io/xv6-riscv-book/mem.html)
  trình bày allocator và bảng trang Sv39.

## Dọn trước khi đóng gói

Chạy `make clean` trước khi tạo gói source. Lưu các gói nộp bên ngoài repo
vì quy tắc `clean` cũng xóa các file `*.zip` ở thư mục gốc.
