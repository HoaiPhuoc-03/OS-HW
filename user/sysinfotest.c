#include "kernel/types.h"
#include "kernel/riscv.h"
#include "kernel/fcntl.h"
#include "kernel/syscall.h"
#include "kernel/sysinfo.h"
#include "user/user.h"

// Never put the 37 KB ABI on xv6's single-page user stack.
static struct sysinfo before, after;
static union {
  uint64 align;
  char bytes[sizeof(struct sysinfo) + 1];
} unaligned;
static int passed, failed, blocked;
static char *path = "sysinfo.tmp";
extern char end[];

#define CHECK(c, why) do { if(!(c)){ \
  printf("  %s (line %d)\n", why, __LINE__); ok = 0; goto out; \
} } while(0)

static struct sysinfo_proc*
findproc(struct sysinfo *s, int pid)
{
  for(int i = 0; i < s->proc_count; i++)
    if(s->procs[i].pid == pid)
      return &s->procs[i];
  return 0;
}

static struct sysinfo_file*
findfile(struct sysinfo *s, int slot)
{
  for(int i = 0; i < s->file_count; i++)
    if(s->files[i].slot == slot)
      return &s->files[i];
  return 0;
}

static int
slotof(struct sysinfo *s, int pid, int fd)
{
  struct sysinfo_proc *p = findproc(s, pid);
  if(p)
    for(int i = 0; i < p->nfd; i++)
      if(p->fds[i].fd == fd)
        return p->fds[i].file_slot;
  return -1;
}

static int
haspid(struct sysinfo_file *f, int pid)
{
  if(f)
    for(int i = 0; i < f->npids; i++)
      if(f->pids[i] == pid)
        return 1;
  return 0;
}

static int
valid(struct sysinfo *s)
{
  if(s->file_count < 0 || s->file_count > NFILE ||
     s->proc_count < 1 || s->proc_count > NPROC)
    return 0;
  for(int i = 0; i < s->file_count; i++){
    struct sysinfo_file *f = &s->files[i];
    if(f->slot < 0 || f->slot >= NFILE || f->ref <= 0 ||
       f->type < SYSINFO_NONE || f->type > SYSINFO_DEVICE ||
       f->mode < 0 || f->mode > 3 || f->npids < 0 || f->npids > NPROC)
      return 0;
    for(int j = 0; j < i; j++)
      if(s->files[j].slot == f->slot)
        return 0;
    for(int j = 0; j < f->npids; j++){
      struct sysinfo_proc *p = findproc(s, f->pids[j]);
      if(p == 0 || p->nfd < 0 || p->nfd > NOFILE)
        return 0;
      int found = 0;
      for(int k = 0; k < p->nfd; k++)
        if(p->fds[k].file_slot == f->slot)
          found = 1;
      if(!found)
        return 0;
      for(int k = 0; k < j; k++)
        if(f->pids[k] == f->pids[j])
          return 0;
    }
  }
  for(int i = 0; i < s->proc_count; i++){
    struct sysinfo_proc *p = &s->procs[i];
    if(p->pid <= 0 || p->ppid < 0 || p->state <= SYSINFO_UNUSED ||
       p->state > SYSINFO_ZOMBIE || p->nfd < 0 || p->nfd > NOFILE)
      return 0;
    for(int j = 0; j < i; j++)
      if(s->procs[j].pid == p->pid)
        return 0;
    for(int j = 0; j < p->nfd; j++){
      struct sysinfo_fd *fd = &p->fds[j];
      if(fd->fd < 0 || fd->fd >= NOFILE ||
         !haspid(findfile(s, fd->file_slot), p->pid))
        return 0;
      for(int k = 0; k < j; k++)
        if(p->fds[k].fd == fd->fd)
          return 0;
    }
  }
  return 1;
}

static int
capture(struct sysinfo *s)
{
  if(sysinfo(s) < 0){
    printf("  sysinfo returned -1 for a valid static buffer\n");
    return 0;
  }
  if(!valid(s)){
    printf("  snapshot violates bounds or file/process relationships\n");
    return 0;
  }
  return 1;
}

static int
basic(void)
{
  int ok = 1;
  CHECK(capture(&after), "basic snapshot");
  struct sysinfo_proc *p = findproc(&after, getpid());
  CHECK(p && p->state == SYSINFO_RUNNING, "caller must be RUNNING");
  printf("  ABI=%d bytes, files=%d, processes=%d\n",
         (int)sizeof(after), after.file_count, after.proc_count);
out:
  return ok;
}

static int
opentest(void)
{
  int ok = 1, fd = -1;
  CHECK(capture(&before), "before open");
  fd = open(path, O_CREATE | O_RDWR | O_TRUNC);
  CHECK(fd >= 0, "open test file");
  CHECK(capture(&after), "after open");
  int slot = slotof(&after, getpid(), fd);
  struct sysinfo_file *f = findfile(&after, slot);
  CHECK(f && !findfile(&before, slot), "open allocates a new object");
  CHECK(f->type == SYSINFO_INODE && f->mode == 3 && f->ref == 1,
        "new inode object: read/write, ref=1");
  CHECK(f->npids == 1 && haspid(f, getpid()), "single owner PID");
  CHECK(after.file_count == before.file_count + 1, "object count after open");
  close(fd);
  fd = open(path, O_RDONLY);
  CHECK(fd >= 0 && capture(&after), "read-only open");
  f = findfile(&after, slotof(&after, getpid(), fd));
  CHECK(f && f->mode == SYSINFO_READ, "read-only mode");
  close(fd);
  fd = open(path, O_WRONLY);
  CHECK(fd >= 0 && capture(&after), "write-only open");
  f = findfile(&after, slotof(&after, getpid(), fd));
  CHECK(f && f->mode == SYSINFO_WRITE, "write-only mode");
out:
  if(fd >= 0) close(fd);
  unlink(path);
  return ok;
}

static int
duptest(void)
{
  int ok = 1, fd = -1, d = -1;
  fd = open(path, O_CREATE | O_RDWR);
  CHECK(fd >= 0 && capture(&before), "open before dup");
  int slot = slotof(&before, getpid(), fd);
  struct sysinfo_file *f = findfile(&before, slot);
  CHECK(f, "original object exists");
  int ref = f->ref;
  d = dup(fd);
  CHECK(d >= 0 && capture(&after), "dup and snapshot");
  CHECK(slotof(&after, getpid(), d) == slot, "dup shares object slot");
  f = findfile(&after, slot);
  CHECK(f && f->ref == ref + 1 && f->npids == 1, "ref increases; PID unique");
  CHECK(before.file_count == after.file_count, "dup creates no file object");
out:
  if(d >= 0) close(d);
  if(fd >= 0) close(fd);
  unlink(path);
  return ok;
}

static int
closetest(void)
{
  int ok = 1, fd = -1, d = -1;
  fd = open(path, O_CREATE | O_RDWR);
  CHECK(fd >= 0, "open before close");
  d = dup(fd);
  CHECK(d >= 0 && capture(&before), "dup before close");
  int slot = slotof(&before, getpid(), fd);
  struct sysinfo_file *f = findfile(&before, slot);
  CHECK(f && f->ref == 2, "two descriptors before close");
  CHECK(close(d) == 0, "close duplicated descriptor");
  d = -1;
  CHECK(capture(&after), "after first close");
  f = findfile(&after, slot);
  CHECK(f && f->ref == 1, "first close decrements ref");
  CHECK(close(fd) == 0, "close last descriptor");
  fd = -1;
  CHECK(capture(&after), "after final close");
  CHECK(!findfile(&after, slot), "last close releases object");
  CHECK(after.file_count == before.file_count - 1, "object count after close");
out:
  if(d >= 0) close(d);
  if(fd >= 0) close(fd);
  unlink(path);
  return ok;
}

// Children signal readiness, then wait for a token before closing inherited FDs.
// All exact reference checks concern only our private test file.
static int
sharedtest(int children, int duplicate)
{
  int ok = 1, fd = -1, d = -1, ready[2] = {-1, -1}, go[2] = {-1, -1};
  int pids[2], started = 0, status, slot = -1, ref = 0;
  char token = 'x';
  fd = open(path, O_CREATE | O_RDWR);
  CHECK(fd >= 0, "open shared file");
  if(duplicate){
    d = dup(fd);
    CHECK(d >= 0, "dup shared file");
  }
  CHECK(pipe(ready) == 0 && pipe(go) == 0, "synchronization pipes");
  CHECK(capture(&before), "before fork");
  slot = slotof(&before, getpid(), fd);
  struct sysinfo_file *f = findfile(&before, slot);
  CHECK(f, "parent file object");
  ref = f->ref;
  for(int i = 0; i < children; i++){
    int pid = fork();
    CHECK(pid >= 0, "fork child");
    if(pid == 0){
      close(ready[0]);
      close(go[1]);
      if(write(ready[1], &token, 1) != 1 || read(go[0], &token, 1) != 1)
        exit(1);
      exit(0);
    }
    pids[started++] = pid;
  }
  for(int i = 0; i < started; i++)
    CHECK(read(ready[0], &token, 1) == 1, "child ready");
  CHECK(capture(&after), "children hold inherited descriptors");
  for(int i = 0; i < started; i++){
    struct sysinfo_proc *p = findproc(&after, pids[i]);
    CHECK(p && p->ppid == getpid(), "child PID and parent PID");
    CHECK(slotof(&after, pids[i], fd) == slot, "inherited object slot");
    if(duplicate)
      CHECK(slotof(&after, pids[i], d) == slot, "both inherited descriptors");
  }
  f = findfile(&after, slot);
  CHECK(f && f->ref == ref * (started + 1), "fork duplicates references per FD");
  CHECK(f->npids == started + 1 && haspid(f, getpid()), "unique referencing PIDs");
  for(int i = 0; i < started; i++)
    CHECK(haspid(f, pids[i]), "child in file PID list");
  CHECK(after.file_count == before.file_count, "fork allocates no file objects");
  for(int i = 0; i < started; i++)
    CHECK(write(go[1], &token, 1) == 1, "release child");
out:
  // Closing the last writer also releases waiting children on an assertion error.
  if(go[0] >= 0) close(go[0]);
  if(go[1] >= 0) close(go[1]);
  if(ready[0] >= 0) close(ready[0]);
  if(ready[1] >= 0) close(ready[1]);
  for(int i = 0; i < started; i++){
    if(wait(&status) < 0 || status != 0) ok = 0;
  }
  if(ok){
    if(!capture(&after) || !(f = findfile(&after, slot)) || f->ref != ref ||
       f->npids != 1){
      printf("  inherited references not released after wait\n");
      ok = 0;
    }
  }
  if(d >= 0) close(d);
  if(fd >= 0) close(fd);
  unlink(path);
  return ok;
}

static int forktest(void) { return sharedtest(1, 0); }
static int multiproctest(void) { return sharedtest(2, 1); }

static int
pipetest(void)
{
  int ok = 1, p[2] = {-1, -1};
  CHECK(capture(&before), "before pipe");
  CHECK(pipe(p) == 0 && capture(&after), "pipe and snapshot");
  int r = slotof(&after, getpid(), p[0]), w = slotof(&after, getpid(), p[1]);
  struct sysinfo_file *rf = findfile(&after, r), *wf = findfile(&after, w);
  CHECK(r != w && rf && wf, "pipe uses two file objects");
  CHECK(rf->type == SYSINFO_PIPE && wf->type == SYSINFO_PIPE, "pipe types");
  CHECK(rf->mode == SYSINFO_READ && wf->mode == SYSINFO_WRITE, "pipe modes");
  CHECK(rf->ref == 1 && wf->ref == 1, "pipe endpoint references");
  CHECK(after.file_count == before.file_count + 2, "two additional objects");
out:
  if(p[0] >= 0) close(p[0]);
  if(p[1] >= 0) close(p[1]);
  return ok;
}

static int
exittest(void)
{
  int ok = 1, fd = -1, pid = -1, status;
  fd = open(path, O_CREATE | O_RDWR);
  CHECK(fd >= 0, "open before child exit");
  pid = fork();
  CHECK(pid >= 0, "fork exiting child");
  if(pid == 0) exit(0);
  // Do not reap until ZOMBIE is observed; bounded polling avoids a timing guess.
  int zombie = 0;
  for(int i = 0; i < 100; i++){
    CHECK(capture(&after), "poll child state");
    struct sysinfo_proc *p = findproc(&after, pid);
    CHECK(p, "unreaped child must remain in table");
    if(p->state == SYSINFO_ZOMBIE){
      CHECK(p->nfd == 0, "zombie has no descriptors");
      zombie = 1;
      break;
    }
    sleep(1);
  }
  CHECK(zombie, "child reached ZOMBIE within timeout");
  struct sysinfo_file *f = findfile(&after, slotof(&after, getpid(), fd));
  CHECK(f && f->ref == 1 && !haspid(f, pid), "exit releases child references");
  int reaped = wait(&status);
  CHECK(reaped == pid && status == 0, "wait returns child PID and status");
  int oldpid = pid;
  pid = -1;
  CHECK(capture(&after) && !findproc(&after, oldpid), "reaped child absent");
out:
  if(pid > 0){ kill(pid); wait(&status); }
  if(fd >= 0) close(fd);
  unlink(path);
  return ok;
}

static int
scheduletest(void)
{
  int ok = 1;
  CHECK(capture(&before), "before sleep");
  struct sysinfo_proc *p = findproc(&before, getpid());
  CHECK(p && p->schedule_count > 0, "caller has been scheduled");
  uint64 count = p->schedule_count;
  CHECK(sleep(2) == 0 && capture(&after), "sleep and reschedule");
  p = findproc(&after, getpid());
  CHECK(p && p->schedule_count > count, "scheduling count increases");
out:
  return ok;
}

static int
pointertest(void)
{
  int ok = 1;
  uint64 top = (uint64)sbrk(0);
  uint64 bad[] = {0, MAXVA, ~(uint64)0, top,
                 top - sizeof(after) + 1,
                 PGROUNDUP((uint64)end), (uint64)pointertest};
  for(int i = 0; i < sizeof(bad)/sizeof(bad[0]); i++)
    CHECK(sysinfo((struct sysinfo *)bad[i]) == -1,
          "reject null/unmapped/overflow/short/guard/read-only pointer");
  // A mapped prefix followed by an out-of-range byte must remain untouched.
  char *tail = (char *)(top - sizeof(after) + 1);
  char saved = *tail;
  *tail = 0x5a;
  int rc = sysinfo((struct sysinfo *)tail);
  int unchanged = *tail == 0x5a;
  *tail = saved;
  CHECK(rc == -1 && unchanged, "failed preflight must not write prefix");
out:
  return ok;
}

static int
repeattest(void)
{
  int ok = 1;
  CHECK(capture(&before), "before repeated calls");
  for(int i = 0; i < 50; i++)
    CHECK(capture(&after), "repeated valid snapshot");
  CHECK(slotof(&before, getpid(), 1) == slotof(&after, getpid(), 1),
        "stdout descriptor preserved");
  CHECK(before.file_count == after.file_count, "no leaked file objects");
  CHECK(findproc(&before, getpid())->nfd == findproc(&after, getpid())->nfd,
        "no leaked descriptors");
  // An unaligned buffer spanning many pages is also a legal destination.
  int rc = sysinfo((struct sysinfo *)(unaligned.bytes + 1));
  CHECK(rc == 0, "copyout supports an unaligned writable buffer");
  // Exercise competing callers of the kernel's shared snapshot buffer.
  int children = 0, status;
  for(int i = 0; i < 2; i++){
    int pid = fork();
    if(pid < 0){
      ok = 0;
      break;
    }
    if(pid == 0){
      for(int j = 0; j < 20; j++)
        if(!capture(&after) || !findproc(&after, getpid()))
          exit(1);
      exit(0);
    }
    children++;
  }
  for(int i = 0; i < children; i++)
    if(wait(&status) < 0 || status != 0)
      ok = 0;
  if(!ok) printf("  concurrent sysinfo callers failed\n");
out:
  return ok;
}

static int
tracetest(void)
{
  int ok = 1, pid = -1, status;
  CHECK(trace(1 << SYS_getpid) == 0, "enable getpid tracing");
  int parent = getpid();
  CHECK(parent > 0, "traced getpid result");
  pid = fork();
  CHECK(pid >= 0, "trace inheritance fork");
  if(pid == 0){
    int child = getpid();
    trace(0);
    exit(child > 0 && child != parent ? 0 : 1);
  }
  CHECK(wait(&status) == pid && status == 0, "child inherits working trace mask");
  pid = -1;
  CHECK(trace(0) == 0, "disable trace");
out:
  trace(0);
  if(pid > 0){ kill(pid); wait(&status); }
  return ok;
}

static struct {
  char *name;
  int (*run)(void);
  int needs_collectors;
} tests[] = {
  {"01 basic sysinfo", basic, 1},
  {"02 open", opentest, 1},
  {"03 dup", duptest, 1},
  {"04 close", closetest, 1},
  {"05 fork", forktest, 1},
  {"06 pipe", pipetest, 1},
  {"07 exit/zombie/wait", exittest, 1},
  {"08 scheduling count", scheduletest, 1},
  {"09 invalid pointers", pointertest, 0},
  {"10 repeated calls", repeattest, 1},
  {"11 multiple process references", multiproctest, 1},
  {"12 trace regression", tracetest, 0},
};

int
main(void)
{
  // LAB_SYSCALL intentionally skips zeroing user pages in uvmalloc().
  passed = failed = blocked = 0;
  int available = sysinfo(&after) == 0;
#ifndef SYSINFO_COLLECTORS_READY
  if(!available){
    printf("PENDING_TEAM_INTEGRATION: collectors disabled; data tests BLOCKED\n");
    memset(&after, 0x5a, sizeof(after));
    for(int i = 0; i < 20; i++)
      if(sysinfo(&after) != -1){
        printf("FAIL: pending syscall returned success\n");
        exit(1);
      }
    for(int i = 0; i < sizeof(after); i++)
      if(((uchar *)&after)[i] != 0x5a){
        printf("FAIL: pending syscall modified user buffer\n");
        exit(1);
      }
  }
#else
  // A real collector failure must fail tests, never masquerade as pending work.
  available = 1;
#endif
  for(int i = 0; i < sizeof(tests)/sizeof(tests[0]); i++){
    if(tests[i].needs_collectors && !available){
      printf("BLOCKED %s: real collectors required\n", tests[i].name);
      blocked++;
    } else if(tests[i].run()){
      printf("PASS %s\n", tests[i].name);
      passed++;
    } else {
      printf("FAIL %s\n", tests[i].name);
      failed++;
    }
  }
  printf("sysinfotest: %d PASS, %d FAIL, %d BLOCKED\n", passed, failed, blocked);
  exit(failed ? 1 : blocked ? 2 : 0);
}
