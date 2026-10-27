#ifndef XV6_SYSINFO_H
#define XV6_SYSINFO_H

#include "types.h"
#include "param.h"

#define SYSINFO_READ  1
#define SYSINFO_WRITE 2

// Shared ABI values; never expose struct file or struct proc to user space.
enum sysinfo_type { SYSINFO_NONE, SYSINFO_PIPE, SYSINFO_INODE, SYSINFO_DEVICE };
enum sysinfo_state {
  SYSINFO_UNUSED, SYSINFO_USED, SYSINFO_SLEEPING,
  SYSINFO_RUNNABLE, SYSINFO_RUNNING, SYSINFO_ZOMBIE
};

struct sysinfo_fd {
  int fd;
  int file_slot;              // index in ftable.file[], not a user descriptor
};

struct sysinfo_file {
  int slot;
  int type;
  int mode;                   // SYSINFO_READ | SYSINFO_WRITE, not open() flags
  int ref;
  int npids;
  int pids[NPROC];            // each PID appears at most once
};

struct sysinfo_proc {
  int pid;
  int state;
  int ppid;                   // 0 if there is no parent
  int nfd;
  uint64 schedule_count;
  struct sysinfo_fd fds[NOFILE];
};

struct sysinfo {
  int file_count;
  int proc_count;
  struct sysinfo_file files[NFILE]; // packed entries, identified by .slot
  struct sysinfo_proc procs[NPROC];
};

#endif