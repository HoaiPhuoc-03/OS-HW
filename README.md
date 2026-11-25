# Lab 2 — Memory

## Khởi động xv6

Từ thư mục gốc của repo trên Linux, dùng `make`, GNU toolchain RISC-V
(`riscv64-unknown-elf-*` hoặc `riscv64-linux-gnu-*`) và `qemu-system-riscv64`
để build và khởi động xv6.

```sh
make qemu CPUS=3
```

Khi thấy dấu nhắc `$` của xv6, chạy lần lượt các mục dưới đây trong cùng phiên.
Để thoát QEMU, nhấn `Ctrl+A`, thả phím, rồi nhấn `X`.

## 4.1 Mở rộng Sysinfo

```sh
sysinfotest show
sysinfotest
```

- `sysinfotest show` hiển thị dòng `free memory: ... bytes` cùng thông tin file và tiến trình.
- `sysinfotest` kiểm tra số byte RAM trống giảm sau khi cấp phát trang và tăng sau khi giải phóng. Dòng tổng kết là `sysinfotest: 20 PASS, 0 FAIL`.

## 4.2 In bảng trang

```sh
vmprinttest
vmprinttest x
```

- `vmprinttest` gọi `execp()` để chạy `echo hello`. Kết quả bắt đầu bằng `page table 0x...`, tiếp theo là các dòng PTE hợp lệ theo từng cấp, rồi đến `hello`.
- `vmprinttest x` chạy cùng lệnh qua `exec()` thông thường. Kết quả chỉ có `hello`.

## 4.3 Phát hiện trang đã được truy cập

```sh
pgaccesstest
```

Chương trình kiểm tra bitmap trả về khi đọc hoặc ghi trang, việc xóa bit truy cập
sau khi quét và cách xử lý đối số không hợp lệ. Dòng tổng kết là
`pgaccesstest: 31 PASS, 0 FAIL`.
