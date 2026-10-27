# Project 1: contract tích hợp sysinfo

## Trạng thái

Phần Phước đã có ABI, syscall 23, pointer validation, storage, liên kết PID/file,
user tests và build entry. **PENDING_TEAM_INTEGRATION**: chưa có collector thật
trong `kernel/file.c` và `kernel/proc.c`; bản mặc định luôn trả `-1`, không ghi
buffer user. Không có stub collector hay snapshot thành công chứa dữ liệu giả.
Không sửa ba file `kernel/file.c`, `kernel/proc.c`, `kernel/proc.h`.

## ABI và ownership

`kernel/sysinfo.h` dùng chung kernel/user, không chứa pointer hay lock.
Với NFILE=100, NPROC=64, NOFILE=16 và RV64:

| Kiểu | Size (byte) | Alignment |
|---|---:|---:|
| sysinfo_fd | 8 | 4 |
| sysinfo_file | 276 | 4 |
| sysinfo_proc | 152 | 8 |
| sysinfo | 37336 | 8 |

Hai count chiếm 8 byte, files bắt đầu tại offset 8, procs tại offset 27608.
`schedule_count` nằm tại offset 16 trong mỗi process. Không có flexible array.
Arrays là packed: chỉ entries `[0, count)` có nghĩa; không dùng slot làm index
vào mảng files trả về. ABI chưa có version: thay layout phải rebuild cả kernel
và user. Không diễn giải dữ liệu từ kernel/user có version khác nhau.

- `slot`: index của object trong `ftable.file[NFILE]`, **không phải** fd.
- `fd`: index trong `p->ofile[NOFILE]`.
- `mode = (readable ? SYSINFO_READ : 0) | (writable ? SYSINFO_WRITE : 0)`.
  Không chứa O_CREATE/O_TRUNC hay các cờ open ban đầu không được lưu trong file.
- type và state dùng giá trị ABI trong header, tương ứng enum xv6 hiện tại.
  NONE vẫn hợp lệ khi một object có ref>0 đang được khởi tạo.
- `ppid=0` nếu không có cha. Parent có thể biến mất giữa hai lần quan sát.
- PID trong file không trùng; nhiều fd cùng PID vẫn đóng góp nhiều ref.
  **Không khẳng định ref == npids**, cũng không ép ref bằng số fd quan sát được.

## Giao diện Tài cần cung cấp

```c
int collect_file_info(struct sysinfo *info);
int sysinfo_file_slot(struct file *f);
```

`collect_file_info` điền `file_count` và packed `files[]`: slot, type, mode, ref.
Chỉ lấy object ref>0. Không sửa `proc_count/procs[]`; không cần điền pids/npids.
Khóa nguồn được lấy/thả bên trong collector, không trả về khi còn giữ spinlock.
Trả 0 thành công, -1 khi không thể tạo dữ liệu theo contract; không vượt NFILE.

`sysinfo_file_slot` dùng cho Phát: ánh xạ địa chỉ object sang slot, trả -1 nếu
không thuộc table. Helper chỉ xác định vị trí, không dereference object, không
đọc ref/type và **không lấy ftable.lock** vì caller có thể giữ p->lock.
Có thể so sánh equality với địa chỉ từng phần tử; tránh pointer subtraction
trên pointer không thuộc array. Helper không bảo đảm object còn hoạt động hay
chưa bị tái sử dụng. Pointer này chỉ dùng nội bộ kernel, không ghi vào ABI.

## Giao diện Phát cần cung cấp

```c
int collect_proc_info(struct sysinfo *info);
```

Điền `proc_count` và packed `procs[]` cho state != UNUSED: pid, state, ppid,
uint64 schedule_count, nfd và packed fds[] chứa fd/file_slot. Không sửa files.
Trả 0 thành công, -1 khi lỗi; không vượt NPROC/NOFILE, không trả fd trùng.
Bộ đếm khởi tạo 0 khi alloc/reuse, tăng dưới p->lock ngay trước dispatch từ
RUNNABLE sang RUNNING; không kế thừa counter từ cha. ZOMBIE không còn fd.

## Locking và mức nhất quán

Phước dùng một buffer static 37336 byte và một sleeplock, khởi tạo sau fileinit
và trước userinit trên boot CPU. Caller giữ sleeplock từ memset qua collectors,
validation/linking và copyout. Caller khác phải chờ, không ghi đè snapshot.
Mọi đường lỗi sau acquire đều đi qua releasesleep. Sleeplock bảo vệ **storage**,
không tự khóa bảng nguồn; không giữ spinlock khi acquire/release sleeplock.
Không collector nào được gọi lại sysinfo hoặc lấy sysinfo_lock.

Lock order quan sát được ở repo:

- fork giữ np->lock khi gọi filedup -> ftable.lock; sau đó thả np->lock trước
  khi lấy wait_lock và công bố parent.
- exit đóng các file trước wait_lock; sau đó wait_lock -> p->lock.
- wait dùng wait_lock -> child p->lock; scheduler dùng từng p->lock.
- sys_open/sys_dup/sys_close và exit cập nhật ofile **không lấy p->lock**.
  Vì vậy chỉ lấy p->lock trong collector không đủ đồng bộ ofile hay toàn bộ
  type/mode đang được sys_open khởi tạo. ftable.lock chủ yếu bảo vệ ref/allocation.

Collector không được lấy ftable.lock rồi p->lock: đảo thứ tự fork. Parent phải
được đọc theo wait_lock -> p->lock. Helper slot không lấy ftable.lock. Hai
collector phải thả mọi source lock trước khi return, đặc biệt trước copyout.

Contract hiện chỉ hướng tới **best-effort**, chưa có snapshot nguyên tử hoặc
cơ chế generation để phát hiện slot reuse. `sysinfo_link` phát hiện counts,
slot/fd/PID trùng, giá trị ngoài miền và fd tham chiếu slot không có trong
snapshot; trả -1 thay vì xuất snapshot sai cấu trúc. Nó không phát hiện được
một slot vừa bị đóng và tái sử dụng giữa hai collector. Không retry ngầm.

**Dependency cần Tài/Phát giải quyết và thống nhất trước khi bật tích hợp:**
quy tắc lấy mẫu ofile và metadata khi writer đang chạy. Đọc ofile dưới p->lock
đơn thuần không chứng minh được an toàn. Muốn bảo đảm chặt chẽ cần đồng bộ với
các writer, pin/generation hoặc cơ chế snapshot được nhóm thống nhất; các thay
đổi vượt file phụ trách cần phối hợp, không được tự nhận đã giải quyết bằng
sysinfo_lock. Các test exact-ref giữ riêng file thử và đồng bộ child; test
concurrent sẽ kiểm tra thêm khi collector thật sẵn sàng.

## Luồng syscall và user pointer

User stub đặt a7=23 -> ecall -> dispatch sys_sysinfo; trace giữ logic cũ và có
thêm tên sysinfo. argaddr chỉ lấy uint64, không dereference pointer user.
Preflight từ chối null, phạm vi vượt p->sz/MAXVA (dùng phép trừ tránh overflow),
và mọi page thiếu PTE_V/PTE_U/PTE_W. Chỉ sau đó mới lấy snapshot lock.
Sau collect, validator dựng unique PID links từ fds; copyout dùng pagetable
của caller và return được kiểm tra. Thành công 0, mọi lỗi -1.

Preflight kiểm tra toàn bộ mapping trước khi ghi, tránh copyout một prefix rồi
mới phát hiện page lỗi. xv6 hiện không có thread chia sẻ pagetable nên mapping
không đổi bởi thread khác trong syscall. Không hứa rollback copyout nói chung.
Kernel stack chỉ chứa pointer/scalar; test dùng hai struct global và một union cho buffer unaligned (112016 byte),
không đặt struct trên user stack. Buffer tĩnh không dùng heap allocator kernel.

## Bật tích hợp và chạy

1. Tài/Phát thêm implementation đúng prototype trong defs.h; review locking ở
   trên trước khi bật. Không thêm một hàm thành công nhưng không thu thập dữ liệu.
2. Giữ collectors chỉ điền phần mình sở hữu; Phước tự dựng npids/pids sau cả hai.
3. Backup các archive cần giữ: clean rule sẵn có của repo xóa `*.zip` ở root.
4. `make clean`, sau đó `make -j4 SYSINFO_COLLECTORS=1 kernel/kernel fs.img`.
   Phải clean khi đổi flag: Makefile hiện không track thay đổi command-line flags.
5. `make CPUS=1 SYSINFO_COLLECTORS=1 qemu`, chạy `sysinfotest`; chạy lại CPUS=3.
6. `trace 32 cat README`, `trace 8388608 sysinfotest`; kiểm tra trace read và tên
   sysinfo. `echo normal-output` phải không sinh dòng syscall trace.

`sysinfotest` có 12 nhóm: basic, open (3 mode), dup, close, fork, pipe,
exit/zombie/wait, scheduling, invalid pointer, repeated/unaligned/concurrent,
nhiều PID + nhiều fd, trace + kế thừa mask. Child được đồng bộ qua ready/go pipe;
zombie được polling có timeout trước wait. Mọi file thử có tên `sysinfo.tmp` và
được cleanup; chạy trong môi trường xv6 test riêng, không có workload nền.
Exact global object deltas giả định môi trường test ổn định này, không phải hệ
thống có workload bất kỳ. Không hardcode PID, boot count hoặc file slot.

Bản pending: 10 nhóm dữ liệu BLOCKED; invalid-pointer chỉ kiểm tra rejection
(tất cả syscall vốn trả -1, chưa chứng minh đường copy thành công); trace vẫn
chạy được. Test thêm 20 valid pending calls, yêu cầu -1 và giữ nguyên buffer.
Exit status: 0 nếu tất cả PASS, 1 nếu có FAIL, 2 nếu còn BLOCKED.
Trace regression trong runner kiểm tra API/return/inheritance và phát ra hai
dòng getpid trên UART; cần đối chiếu UART bên ngoài để xác minh output trace.
Bật collectors mà valid call lỗi sẽ FAIL, không bị chuyển thành BLOCKED.

Không dùng grade-lab-syscall để chấm Project 1: sysinfotest cũ của lab dùng ABI
freemem/nproc, khác đề hiện tại. Mặc định `make` chỉ build kernel; cần fs.img
để chắc chắn user tests cũng được compile và đưa vào image. trace/sysinfotest
đều nằm ngoài conditional LAB=syscall và chạy được khi `LAB=`.

Trong LAB=syscall, uvmalloc chủ động bỏ zero user pages phục vụ bài attack cũ.
Runner tự khởi tạo counters, không dựa vào BSS bằng 0 và không dùng malloc với
freep chưa được khởi tạo của lab này. Giữ nguyên hành vi Project 0.

## Kết quả xác minh ngày 27/10/2026

- Baseline `make -j4` và `make -j4 fs.img`: thành công trước khi sửa code.
- `make clean`, `make -j4`, `make -j4 fs.img`: thành công; archive SourceCode.zip
  cũ được backup/restore vì clean rule xóa zip, SHA-256 trước/sau trùng nhau.
- Clean build `make -j4 LAB= kernel/kernel fs.img`: thành công. QEMU LAB=,
  CPUS=1 chạy được cả sysinfotest và trace.
- Bản cuối default LAB=syscall được clean build lại thành công.
- QEMU CPUS=1 và CPUS=3: mỗi lần sysinfotest cho 2 PASS, 0 FAIL, 10 BLOCKED.
  Hai PASS là invalid-pointer rejection và trace regression. Data tests 01-08,
  10-11 BLOCKED. Không có snapshot thành công hay full integration PASS.
- 20 valid pending calls trả -1 và không đổi buffer sentinel; hai runner đồng
  thời cũng hoàn tất đúng summary (stdout riêng, trace vẫn đi UART).
- UART xác minh hai dòng getpid của parent/child; `trace 32 cat README` có read;
  `trace 8388608 sysinfotest` có `syscall sysinfo -> -1`; echo sau đó không trace.
- Host unit chạy 38 checks, 0 failures trên actual sysproc.c: ABI/layout,
  unique PID links, ref khác npids, malformed output và pointer preflight.
  walk dùng test double, **không** thay collector hoặc giả lập tích hợp kernel.
- Compile trực tiếp kernel và user với SYSINFO_COLLECTORS_READY: thành công.
  `-fstack-usage` trên RISC-V báo sys_sysinfo frame=96 byte; user test frame lớn
  nhất=160 byte. Đây là kích thước từng frame, chưa gồm collector tương lai.
- Negative check `make -j4 SYSINFO_COLLECTORS=1 kernel/kernel fs.img` sau clean:
  **thất bại đúng dependency**, undefined collect_file_info/collect_proc_info.
  Không thêm stub để che lỗi. Sau đó restore clean default build thành công.
- `git diff --check`: thành công. file.c/proc.c/proc.h/vm.c/user/trace.c không đổi.
- Linker vẫn có warning LOAD segment RWX như baseline. Không có compile error
  ở cấu hình mặc định. Các đường successful collect/copyout trong QEMU,
  semantics file/process/scheduling và source-table concurrency: **NOT RUN**,
  bị chặn bởi hai collector cùng giao thức đồng bộ chưa có.

Chạy lại unit độc lập trên Ubuntu LP64, không cần boot xv6:

```sh
cc -O1 -Wall -Werror -fno-builtin -ffunction-sections -fdata-sections \
  -Wl,--gc-sections tests/sysinfo_unit.c -o /tmp/sysinfo-unit
/tmp/sysinfo-unit
```

Log chi tiết phiên kiểm tra nằm tại `/tmp/os-hw1-review/` trên máy hiện tại:
`default-final-build.log`, `no-lab-build.log`, `enabled-negative-build.log`,
`qemu-cpus1.log`, `qemu-cpus3.log`, `qemu-no-lab-cpus1.log`, và các file `.su`.
Nhánh triển khai: `phuoc/sysinfo-user-kernel`. Tích hợp collector vẫn đang chờ Tài/Phát.
