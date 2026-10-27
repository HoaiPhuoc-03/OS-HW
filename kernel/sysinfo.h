// Thông tin của một đối tượng file đang được tham chiếu.
struct fileinfo {
  int id;                         // Slot index trong ftable.file[]
  int type;                       // FD_PIPE, FD_INODE, FD_DEVICE
  int readable;                   // Có quyền đọc không?
  int writable;                   // Có quyền ghi không?
  int ref;                        // Số tham chiếu của kernel
  int npids;                      // Số PID khác nhau tham chiếu file
  int pids[SYSINFO_MAX_PIDS];     // Danh sách PID
};

// Thông tin hệ thống trả về từ syscall sysinfo.
struct sysinfo {
  uint64 freemem;                 // Bộ nhớ trống (bytes)
  uint64 nproc;                   // Số process đang hoạt động

  int nfiles;                     // Số đối tượng file được ghi
  struct fileinfo files[SYSINFO_MAX_FILES];
};

#endif