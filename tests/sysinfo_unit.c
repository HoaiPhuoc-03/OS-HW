// Host-only unit checks of Phuoc's actual kernel code. No real collectors or
// QEMU integration are simulated here. Dead-section elimination drops syscalls.
#include "../kernel/sysproc.c"

static struct sysinfo fixture;
static pte_t pages[32];
static int checks, failures;

// Test double only for page-table traversal in the pure pointer preflight.
pte_t*
walk(pagetable_t table, uint64 va, int alloc)
{
  (void)table;
  (void)alloc;
  return va / PGSIZE < 32 ? &pages[va / PGSIZE] : 0;
}

#define VERIFY(c) do { checks++; if(!(c)){ \
  printf("FAIL line %d: %s\n", __LINE__, #c); failures++; \
} } while(0)

static void
reset(void)
{
  memset(&fixture, 0, sizeof(fixture));
  fixture.file_count = 1;
  fixture.proc_count = 2;
  fixture.files[0].slot = 7;
  fixture.files[0].type = SYSINFO_INODE;
  fixture.files[0].mode = SYSINFO_READ | SYSINFO_WRITE;
  fixture.files[0].ref = 3;
  fixture.procs[0].pid = 11;
  fixture.procs[0].state = SYSINFO_RUNNING;
  fixture.procs[0].nfd = 2;
  fixture.procs[0].fds[0].fd = 0;
  fixture.procs[0].fds[0].file_slot = 7;
  fixture.procs[0].fds[1].fd = 1;
  fixture.procs[0].fds[1].file_slot = 7;
  fixture.procs[1].pid = 12;
  fixture.procs[1].ppid = 11;
  fixture.procs[1].state = SYSINFO_SLEEPING;
  fixture.procs[1].nfd = 1;
  fixture.procs[1].fds[0].fd = 0;
  fixture.procs[1].fds[0].file_slot = 7;
}

#define REJECT(change) do { reset(); change; VERIFY(sysinfo_link(&fixture) == -1); } while(0)

int
main(void)
{
  VERIFY(sizeof(struct sysinfo) == 37336);
  VERIFY(__alignof__(struct sysinfo) == 8);
  VERIFY(__builtin_offsetof(struct sysinfo, procs) == 27608);
  VERIFY(__builtin_offsetof(struct sysinfo_proc, schedule_count) == 16);
  reset();
  VERIFY(sysinfo_link(&fixture) == 0);
  VERIFY(fixture.files[0].npids == 2);
  VERIFY(fixture.files[0].pids[0] == 11 && fixture.files[0].pids[1] == 12);
  VERIFY(fixture.files[0].ref == 3); // three FDs, only two unique PIDs
  VERIFY(sysinfo_link(&fixture) == 0 && fixture.files[0].npids == 2);
  REJECT(fixture.file_count = NFILE + 1);
  REJECT(fixture.file_count = -1);
  REJECT(fixture.proc_count = NPROC + 1);
  REJECT(fixture.proc_count = 0);
  REJECT(fixture.files[0].slot = NFILE);
  REJECT(fixture.files[0].ref = 0);
  REJECT(fixture.files[0].type = SYSINFO_DEVICE + 1);
  REJECT(fixture.files[0].mode = 4);
  REJECT(fixture.file_count = 2; fixture.files[1] = fixture.files[0]);
  REJECT(fixture.procs[0].pid = 0);
  REJECT(fixture.procs[0].ppid = -1);
  REJECT(fixture.procs[0].state = SYSINFO_UNUSED);
  REJECT(fixture.procs[0].state = SYSINFO_ZOMBIE + 1);
  REJECT(fixture.procs[0].nfd = NOFILE + 1);
  REJECT(fixture.procs[1].pid = 11);
  REJECT(fixture.procs[0].fds[0].fd = NOFILE);
  REJECT(fixture.procs[0].fds[1].fd = 0);
  REJECT(fixture.procs[0].fds[0].file_slot = -1);
  REJECT(fixture.procs[0].fds[0].file_slot = 8); // valid slot, missing object

  struct proc p;
  memset(&p, 0, sizeof(p));
  p.sz = sizeof(pages) / sizeof(pages[0]) * PGSIZE;
  for(int i = 0; i < 32; i++) pages[i] = PTE_V | PTE_U | PTE_W;
  VERIFY(sysinfo_user_buffer(&p, PGSIZE + 3) == 0);
  VERIFY(sysinfo_user_buffer(&p, 0) == -1);
  VERIFY(sysinfo_user_buffer(&p, p.sz) == -1);
  VERIFY(sysinfo_user_buffer(&p, p.sz - sizeof(snapshot) + 1) == -1);
  VERIFY(sysinfo_user_buffer(&p, MAXVA) == -1);
  VERIFY(sysinfo_user_buffer(&p, ~(uint64)0) == -1);
  pages[5] &= ~PTE_W;
  VERIFY(sysinfo_user_buffer(&p, PGSIZE + 3) == -1);
  pages[5] = PTE_V | PTE_W; // guard page
  VERIFY(sysinfo_user_buffer(&p, PGSIZE + 3) == -1);
  pages[5] = 0; // unmapped page
  VERIFY(sysinfo_user_buffer(&p, PGSIZE + 3) == -1);
  p.sz = MAXVA + PGSIZE;
  VERIFY(sysinfo_user_buffer(&p, MAXVA - 1) == -1);
  printf("sysinfo host units: %d checks, %d failures\n", checks, failures);
  return failures != 0;
}
