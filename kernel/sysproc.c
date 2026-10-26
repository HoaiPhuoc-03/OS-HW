#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"
#include "sysinfo.h"

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

// [BAN TAM - PHAN NGUOI 1] chi de nguoi 3 build va test; thay bang ban cua nguoi 1.
//
// struct sysinfo ~34 KB > kernel stack (4 KB) nen dung buffer tinh, bao ve
// bang sysinfo_lock (khoi tao tinh, tuong duong initlock()).
// Thu tu lock: sysinfo_lock -> ftable.lock ; sysinfo_lock -> wait_lock -> p->lock.
static struct sysinfo kinfo;
static struct spinlock sysinfo_lock = { .locked = 0, .name = "sysinfo", .cpu = 0 };

uint64
sys_sysinfo(void)
{
  uint64 addr;
  int r;

  argaddr(0, &addr);

  acquire(&sysinfo_lock);
  memset(&kinfo, 0, sizeof(kinfo));
  kinfo.nfile = filesinfo_collect(kinfo.files, NFILE);   // nguoi 2
  kinfo.nproc = procinfo_collect(kinfo.procs, NPROC);    // nguoi 3
  sysinfo_linkpids(kinfo.procs, kinfo.nproc,             // nguoi 3
                   kinfo.files, kinfo.nfile);
  r = copyout(myproc()->pagetable, addr, (char *)&kinfo, sizeof(kinfo));
  release(&sysinfo_lock);

  return r < 0 ? -1 : 0;
}
