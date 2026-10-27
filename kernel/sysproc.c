#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"
#include "sleeplock.h"
#include "sysinfo.h"

static struct sleeplock sysinfo_lock;
static struct sysinfo snapshot;

_Static_assert(sizeof(struct sysinfo_fd) == 8, "sysinfo fd ABI");
_Static_assert(sizeof(struct sysinfo_proc) == 24 + NOFILE * 8,
               "sysinfo proc ABI");
_Static_assert((int)ZOMBIE == (int)SYSINFO_ZOMBIE, "sysinfo state ABI");

void
sysinfoinit(void)
{
  initsleeplock(&sysinfo_lock, "sysinfo");
}

// Preflight every page: copyout alone may write a prefix before failing.
// xv6 has no threads sharing this page table, so mappings remain stable here.
static int
sysinfo_user_buffer(struct proc *p, uint64 addr)
{
  uint64 len = sizeof(snapshot);
  if(addr == 0 || addr >= p->sz || len > p->sz - addr ||
     addr >= MAXVA || len > MAXVA - addr)
    return -1;
  for(uint64 va = PGROUNDDOWN(addr); va < addr + len; va += PGSIZE){
    pte_t *pte = walk(p->pagetable, va, 0);
    if(pte == 0 || (*pte & (PTE_V | PTE_U | PTE_W)) !=
                          (PTE_V | PTE_U | PTE_W))
      return -1;
  }
  return 0;
}

static int
sysinfo_collect(struct sysinfo *info)
{
#ifdef SYSINFO_COLLECTORS_READY
  if(collect_file_info(info) != 0 || collect_proc_info(info) != 0)
    return -1;
  return 0;
#else
  // PENDING_TEAM_INTEGRATION: no fake or empty successful snapshot.
  (void)info;
  return -1;
#endif
}

// Validate packed collector output and derive unique file -> PID links.
// This detects structural mismatches, not slot reuse between two observations.
static int
sysinfo_link(struct sysinfo *info)
{
  if(info->file_count < 0 || info->file_count > NFILE ||
     info->proc_count < 1 || info->proc_count > NPROC)
    return -1;
  for(int i = 0; i < info->file_count; i++){
    struct sysinfo_file *f = &info->files[i];
    if(f->slot < 0 || f->slot >= NFILE || f->ref < 1 ||
       f->type < SYSINFO_NONE || f->type > SYSINFO_DEVICE ||
       f->mode < 0 || f->mode > (SYSINFO_READ | SYSINFO_WRITE))
      return -1;
    for(int j = 0; j < i; j++)
      if(info->files[j].slot == f->slot)
        return -1;
    f->npids = 0;
    memset(f->pids, 0, sizeof(f->pids));
  }
  for(int i = 0; i < info->proc_count; i++){
    struct sysinfo_proc *p = &info->procs[i];
    if(p->pid <= 0 || p->ppid < 0 || p->state <= SYSINFO_UNUSED ||
       p->state > SYSINFO_ZOMBIE || p->nfd < 0 || p->nfd > NOFILE)
      return -1;
    for(int j = 0; j < i; j++)
      if(info->procs[j].pid == p->pid)
        return -1;
    for(int j = 0; j < p->nfd; j++){
      struct sysinfo_fd *fd = &p->fds[j];
      if(fd->fd < 0 || fd->fd >= NOFILE ||
         fd->file_slot < 0 || fd->file_slot >= NFILE)
        return -1;
      for(int k = 0; k < j; k++)
        if(p->fds[k].fd == fd->fd)
          return -1;
      int k;
      for(k = 0; k < info->file_count; k++)
        if(info->files[k].slot == fd->file_slot)
          break;
      if(k == info->file_count)
        return -1;
      struct sysinfo_file *f = &info->files[k];
      int n;
      for(n = 0; n < f->npids; n++)
        if(f->pids[n] == p->pid)
          break;
      if(n == f->npids){
        if(n == NPROC)
          return -1;
        f->pids[f->npids++] = p->pid;
      }
    }
  }
  return 0;
}

uint64
sys_sysinfo(void)
{
  uint64 addr;
  struct proc *p = myproc();
  int result = -1;

  argaddr(0, &addr);
  if(sysinfo_user_buffer(p, addr) < 0)
    return -1;

  // Serializes this storage only; source tables still need their own locks.
  acquiresleep(&sysinfo_lock);
  memset(&snapshot, 0, sizeof(snapshot));
  if(sysinfo_collect(&snapshot) == 0 && sysinfo_link(&snapshot) == 0)
    result = copyout(p->pagetable, addr, (char *)&snapshot, sizeof(snapshot));
  releasesleep(&sysinfo_lock);
  return result;
}

uint64
sys_exit(void)
{
  int n;
  argint(0, &n);
  exit(n);
  return 0;  // not reached
}

uint64
sys_getpid(void)
{
  return myproc()->pid;
}

uint64
sys_fork(void)
{
  return fork();
}

uint64
sys_wait(void)
{
  uint64 p;
  argaddr(0, &p);
  return wait(p);
}

uint64
sys_sbrk(void)
{
  uint64 addr;
  int n;

  argint(0, &n);
  addr = myproc()->sz;
  if(growproc(n) < 0)
    return -1;
  return addr;
}

uint64
sys_sleep(void)
{
  int n;
  uint ticks0;

  argint(0, &n);
  if(n < 0)
    n = 0;
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(killed(myproc())){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

uint64
sys_kill(void)
{
  int pid;

  argint(0, &pid);
  return kill(pid);
}

// return how many clock tick interrupts have occurred
// since start.
uint64
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

uint64
sys_trace(void)
{
  int mask;

  argint(0, &mask);
  myproc()->tracemask = mask;

  return 0;
}
