#ifndef SYSINFO_H
#define SYSINFO_H


#define SYSINFO_MAX_FILES 100  
#define SYSINFO_MAX_PROCS 64   
#define SYSINFO_MAX_PIDS  8     
#define SYSINFO_MAX_FDS   16    

// Information about one open-file object (an ftable.file[] slot with ref > 0).
struct fileinfo {
  int id;                        
  int type;                     
  int readable;                  
  int writable;                 
  int ref;                       
  int npids;                     
  int pids[SYSINFO_MAX_PIDS];   
};

// Information about one process whose state is not UNUSED.
struct procinfo {
  int pid;
  int state;                      
  int ppid;                       
  int sched_count;               
  int nfds;                       
  int fds[SYSINFO_MAX_FDS];     
};

// Result of the sysinfo system call.
struct sysinfo {
  int nfiles;                                
  int nprocs;                                 
  struct fileinfo files[SYSINFO_MAX_FILES];   
  struct procinfo procs[SYSINFO_MAX_PROCS];   
};

#endif