// kernel/sysinfo.h
// Cau truc du lieu ma system call sysinfo() tra ve cho user space.
// Dung chung cho kernel (sysproc.c, file.c, proc.c) va user (sysinfotest.c).
//
// Quy uoc da thong nhat giua 3 nguoi:
//   - ID cua mot open-file object = chi so (slot) cua no trong ftable.file[],
//     tu 0 den SI_NFILE-1.  file.c cung cap fileslot() de doi struct file* -> slot.
//   - procinfo.ofile[fd] = slot cua file ma fd tro toi, hoac -1 neu fd trong.
//   - fileinfo.pids[] duoc dien SAU khi co ca 2 snapshot, bang sysinfo_linkpids()
//     trong proc.c (moi PID chi xuat hien 1 lan du process do dup() nhieu fd).
//   - struct sysinfo kha lon (~34 KB): khong dat tren kernel stack hay user stack
//     (moi stack chi 1 trang = 4 KB).  Kernel dung buffer tinh, user dung bien toan cuc.

#ifndef XV6_SYSINFO_H
#define XV6_SYSINFO_H

#define SI_NPROC   64    // = NPROC  trong param.h
#define SI_NOFILE  16    // = NOFILE trong param.h
#define SI_NFILE   100   // = NFILE  trong param.h

// Ma trang thai process, cung thu tu voi enum procstate trong proc.h.
#define SI_UNUSED    0
#define SI_USED      1
#define SI_SLEEPING  2
#define SI_RUNNABLE  3
#define SI_RUNNING   4
#define SI_ZOMBIE    5

// Ma loai file, cung thu tu voi enum trong struct file (file.h).
#define SI_FD_NONE    0
#define SI_FD_PIPE    1
#define SI_FD_INODE   2
#define SI_FD_DEVICE  3

// Thong tin mot open-file object (mot phan tu cua ftable.file[] co ref > 0).
struct fileinfo {
  int slot;              // ID: chi so trong ftable.file[]
  int type;              // SI_FD_PIPE / SI_FD_INODE / SI_FD_DEVICE
  int readable;          // che do truy cap: 1 neu doc duoc
  int writable;          // che do truy cap: 1 neu ghi duoc
  int ref;               // reference count
  int npids;             // so process dang tham chieu toi file nay
  int pids[SI_NPROC];    // PID cua cac process do
};

// Thong tin mot process co state != UNUSED.
struct procinfo {
  int pid;
  int state;             // SI_USED .. SI_ZOMBIE
  int ppid;              // PID cua cha; 0 neu khong co cha (init)
  int nofile;            // so file descriptor dang mo
  uint64 nsched;         // so lan duoc scheduler() chon de chay
  int ofile[SI_NOFILE];  // ofile[fd] = slot cua file, -1 neu fd trong
  char name[16];         // ten process (de doc ket qua de hon)
};

struct sysinfo {
  uint64 nfile;                      // so open-file object trong toan he thong
  struct fileinfo files[SI_NFILE];
  uint64 nproc;                      // so process co state != UNUSED
  struct procinfo procs[SI_NPROC];
};

#endif // XV6_SYSINFO_H
