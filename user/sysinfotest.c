// Tests for the sysinfo() system call.
//
//   sysinfotest        run every test and show snapshots of key scenarios
//   sysinfotest show   only print the current snapshot (for demos,
//                      e.g. "cat README | sysinfotest show")
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
static int passed, failed;
static char *path = "sysinfo.tmp";
extern char end[];

#define CHECK(c, why) do { if(!(c)){ \
  printf("  %s (line %d)\n", why, __LINE__); ok = 0; goto out; \
} } while(0)

// ---------------------------------------------------------------- printing

static char *statenames[] = {
  "UNUSED", "USED", "SLEEPING", "RUNNABLE", "RUNNING", "ZOMBIE"
};
static char *typenames[] = { "NONE", "PIPE", "INODE", "DEVICE" };
static char *modenames[] = { "--", "r-", "-w", "rw" };

static char*
statename(int s)
{
  return s >= SYSINFO_UNUSED && s <= SYSINFO_ZOMBIE ? statenames[s] : "?";
}

static char*
typename(int t)
{
  return t >= SYSINFO_NONE && t <= SYSINFO_DEVICE ? typenames[t] : "?";
}

static void
pad(int n)
{
  while(n-- > 0)
    printf(" ");
}

static int
width(uint64 v)
{
  int n = 1;
  while(v >= 10){
    v /= 10;
    n++;
  }
  return n;
}

// Left-aligned columns; xv6's printf has no field widths.
static void
col(uint64 v, int w)
{
  printf("%lu", v);
  pad(w - width(v));
}

static void
cols(char *s, int w)
{
  printf("%s", s);
  pad(w - (int)strlen(s));
}

// Print what sysinfo() returned: one row per open-file object and process.
static void
show(char *title, struct sysinfo *s)
{
  printf("  --- %s ---\n", title);
  printf("  open-file objects: %d\n", s->file_count);
  printf("    slot type    mode ref  pids\n");
  for(int i = 0; i < s->file_count; i++){
    struct sysinfo_file *f = &s->files[i];
    printf("    ");
    col(f->slot, 5);
    cols(typename(f->type), 8);
    cols(f->mode >= 0 && f->mode <= 3 ? modenames[f->mode] : "?", 5);
    col(f->ref, 5);
    for(int j = 0; j < f->npids; j++)
      printf("%d ", f->pids[j]);
    printf("\n");
  }
  printf("  processes (state != UNUSED): %d\n", s->proc_count);
  printf("    pid  ppid state     sched  fd->slot\n");
  for(int i = 0; i < s->proc_count; i++){
    struct sysinfo_proc *p = &s->procs[i];
    printf("    ");
    col(p->pid, 5);
    col(p->ppid, 5);
    cols(statename(p->state), 10);
    col(p->schedule_count, 7);
    for(int j = 0; j < p->nfd; j++)
      printf("%d->%d ", p->fds[j].fd, p->fds[j].file_slot);
    printf("\n");
  }
}

// ---------------------------------------------------------------- helpers

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

// Stricter checks for a quiet system, where no process is between
// open/close/exit: every reference to an object comes from a descriptor,
// every parent exists, and only init has no parent.
static int
quiet(struct sysinfo *s)
{
  int roots = 0;
  for(int i = 0; i < s->file_count; i++){
    struct sysinfo_file *f = &s->files[i];
    int fds = 0;
    for(int j = 0; j < s->proc_count; j++)
      for(int k = 0; k < s->procs[j].nfd; k++)
        if(s->procs[j].fds[k].file_slot == f->slot)
          fds++;
    if(fds != f->ref){
      printf("  slot %d: ref=%d but %d descriptors\n", f->slot, f->ref, fds);
      return 0;
    }
  }
  for(int i = 0; i < s->proc_count; i++){
    struct sysinfo_proc *p = &s->procs[i];
    if(p->ppid == 0)
      roots++;
    else if(!findproc(s, p->ppid)){
      printf("  pid %d: parent %d missing\n", p->pid, p->ppid);
      return 0;
    }
  }
  if(roots != 1){
    printf("  %d processes without a parent (only init may)\n", roots);
    return 0;
  }
  return 1;
}

// Poll until process pid reaches state; the matching snapshot is left in s.
static int
waitstate(struct sysinfo *s, int pid, int state)
{
  for(int i = 0; i < 100; i++){
    if(!capture(s))
      return 0;
    struct sysinfo_proc *p = findproc(s, pid);
    if(p && p->state == state)
      return 1;
    sleep(1);
  }
  return 0;
}

// Poll until every child of this process sleeps, then take a fresh
// snapshot: files are sampled before processes, so only a snapshot that
// starts after the children are asleep is fully stable.
static int
childrenasleep(struct sysinfo *s)
{
  int me = getpid();
  for(int t = 0; t < 100; t++){
    if(!capture(s))
      return 0;
    int busy = 0;
    for(int i = 0; i < s->proc_count; i++)
      if(s->procs[i].ppid == me && s->procs[i].state != SYSINFO_SLEEPING)
        busy++;
    if(busy == 0)
      return capture(s);
    sleep(1);
  }
  return 0;
}

// ---------------------------------------------------------------- tests

static int
basic(void)
{
  int ok = 1;
  CHECK(capture(&after), "basic snapshot");
  struct sysinfo_proc *p = findproc(&after, getpid());
  CHECK(p && p->state == SYSINFO_RUNNING, "caller must be RUNNING");
  printf("  ABI=%d bytes, files=%d, processes=%d\n",
         (int)sizeof(after), after.file_count, after.proc_count);
  show("initial state", &after);
out:
  return ok;
}

// init opens the console once and dups it to fds 0, 1 and 2.
static int
inittest(void)
{
  int ok = 1;
  CHECK(capture(&after) && quiet(&after), "quiet initial snapshot");
  struct sysinfo_proc *init = findproc(&after, 1);
  CHECK(init && init->ppid == 0, "init has pid 1 and no parent");
  CHECK(init->state == SYSINFO_SLEEPING, "init waits for children: SLEEPING");
  CHECK(init->nfd == 3, "init holds exactly fds 0, 1, 2");
  int con = slotof(&after, 1, 0);
  CHECK(slotof(&after, 1, 1) == con && slotof(&after, 1, 2) == con,
        "fds 0-2 of init share one console object");
  struct sysinfo_file *f = findfile(&after, con);
  CHECK(f && f->type == SYSINFO_DEVICE &&
        f->mode == (SYSINFO_READ | SYSINFO_WRITE), "console: read/write DEVICE");
  CHECK(haspid(f, 1) && haspid(f, getpid()), "console used by init and by us");
  struct sysinfo_proc *me = findproc(&after, getpid());
  CHECK(me && findproc(&after, me->ppid), "our parent is listed");
  CHECK(findproc(&after, me->ppid)->state == SYSINFO_SLEEPING,
        "our parent waits for us: SLEEPING");
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
  show("pipe: read end r- and write end -w", &after);
out:
  if(p[0] >= 0) close(p[0]);
  if(p[1] >= 0) close(p[1]);
  return ok;
}

// Children signal readiness, then wait for a token before closing inherited FDs.
// All exact reference checks concern only our private test file.
static int
sharedtest(int children, int duplicate, char *title)
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
  CHECK(childrenasleep(&after), "children hold inherited descriptors");
  CHECK(quiet(&after), "references match descriptors");
  for(int i = 0; i < started; i++){
    struct sysinfo_proc *p = findproc(&after, pids[i]);
    CHECK(p && p->ppid == getpid(), "child PID and parent PID");
    CHECK(p->state == SYSINFO_SLEEPING, "child blocked on a pipe: SLEEPING");
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
  show(title, &after);
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

static int
forktest(void)
{
  return sharedtest(1, 0, "fork: child shares the parent's open file");
}

static int
multiproctest(void)
{
  return sharedtest(2, 1, "two children, two descriptors on one object");
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
      CHECK(p->ppid == getpid(), "zombie keeps its parent");
      zombie = 1;
      break;
    }
    sleep(1);
  }
  CHECK(zombie, "child reached ZOMBIE within timeout");
  struct sysinfo_file *f = findfile(&after, slotof(&after, getpid(), fd));
  CHECK(f && f->ref == 1 && !haspid(f, pid), "exit releases child references");
  show("zombie: exited child not yet reaped", &after);
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
killtest(void)
{
  int ok = 1, p[2] = {-1, -1}, pid = -1, status;
  CHECK(pipe(p) == 0, "pipe");
  pid = fork();
  CHECK(pid >= 0, "fork sleeper");
  if(pid == 0){
    char c;
    close(p[1]);
    read(p[0], &c, 1);      // nobody writes: sleeps until killed
    exit(0);
  }
  CHECK(waitstate(&after, pid, SYSINFO_SLEEPING), "blocked reader is SLEEPING");
  CHECK(kill(pid) == 0, "kill the sleeping process");
  int reaped = wait(&status);
  int oldpid = pid;
  pid = -1;
  CHECK(reaped == oldpid && status == -1, "killed process exits with status -1");
  CHECK(capture(&after) && !findproc(&after, oldpid), "killed process is gone");
out:
  if(p[0] >= 0) close(p[0]);
  if(p[1] >= 0) close(p[1]);
  if(pid > 0){ kill(pid); wait(&status); }
  return ok;
}

// The child exits before the grandchild, which is then reparented to init.
static int
reparenttest(void)
{
  int ok = 1, p[2] = {-1, -1}, status, msg[2];
  CHECK(pipe(p) == 0, "pipe");
  int c = fork();
  CHECK(c >= 0, "fork child");
  if(c == 0){
    int g = fork();
    if(g < 0)
      exit(1);
    if(g == 0){
      int res[2];
      res[0] = 0;
      res[1] = getpid();
      for(int i = 0; i < 100; i++){
        struct sysinfo_proc *me;
        if(capture(&after) && (me = findproc(&after, getpid())) && me->ppid == 1){
          res[0] = 1;
          break;
        }
        sleep(1);
      }
      write(p[1], res, sizeof(res));
      exit(0);
    }
    exit(0);
  }
  close(p[1]);
  p[1] = -1;
  CHECK(wait(&status) == c && status == 0, "child exits first");
  CHECK(read(p[0], msg, sizeof(msg)) == sizeof(msg), "result from grandchild");
  CHECK(msg[0] == 1, "orphaned grandchild gets ppid 1 (init)");
  // let init reap the grandchild so later tests see a quiet system
  int gone = 0;
  for(int i = 0; i < 100 && !gone; i++){
    CHECK(capture(&after), "poll for grandchild");
    if(findproc(&after, msg[1]) == 0)
      gone = 1;
    else
      sleep(1);
  }
  CHECK(gone, "init reaps the grandchild");
out:
  if(p[0] >= 0) close(p[0]);
  if(p[1] >= 0) close(p[1]);
  return ok;
}

static int
scheduletest(void)
{
  int ok = 1, ready[2] = {-1, -1}, go[2] = {-1, -1}, pid = -1, status;
  char token = 'x';
  CHECK(capture(&before), "before sleep");
  struct sysinfo_proc *p = findproc(&before, getpid());
  CHECK(p && p->schedule_count > 0, "caller has been scheduled");
  uint64 count = p->schedule_count;
  CHECK(sleep(2) == 0 && capture(&after), "sleep and reschedule");
  p = findproc(&after, getpid());
  CHECK(p && p->schedule_count > count, "scheduling count increases");

  // A new process starts counting from zero and is not charged while asleep.
  CHECK(pipe(ready) == 0 && pipe(go) == 0, "synchronization pipes");
  pid = fork();
  CHECK(pid >= 0, "fork child");
  if(pid == 0){
    close(ready[0]);
    close(go[1]);
    if(write(ready[1], &token, 1) != 1 || read(go[0], &token, 1) != 1)
      exit(1);
    exit(0);
  }
  CHECK(read(ready[0], &token, 1) == 1, "child ready");
  CHECK(waitstate(&before, pid, SYSINFO_SLEEPING), "child blocks: SLEEPING");
  struct sysinfo_proc *c = findproc(&before, pid);
  p = findproc(&before, getpid());
  CHECK(c->schedule_count >= 1 && c->schedule_count < p->schedule_count,
        "child counter starts from zero, not inherited");
  CHECK(sleep(3) == 0 && capture(&after), "snapshot while the child sleeps");
  struct sysinfo_proc *c2 = findproc(&after, pid);
  CHECK(c2 && c2->schedule_count == c->schedule_count,
        "a SLEEPING process is not scheduled");
  CHECK(write(go[1], &token, 1) == 1, "release child");
out:
  if(go[0] >= 0) close(go[0]);
  if(go[1] >= 0) close(go[1]);
  if(ready[0] >= 0) close(ready[0]);
  if(ready[1] >= 0) close(ready[1]);
  if(pid > 0 && (wait(&status) != pid || status != 0)) ok = 0;
  return ok;
}

// More CPU-bound processes than CPUs: some of them must wait as RUNNABLE.
#define NSPIN (NCPU + 1)
static int
runnabletest(void)
{
  int ok = 1, pids[NSPIN], n = 0, status;
  for(int i = 0; i < NSPIN; i++){
    int pid = fork();
    CHECK(pid >= 0, "fork spinner");
    if(pid == 0)
      for(;;)
        ;
    pids[n++] = pid;
  }
  sleep(2);
  CHECK(capture(&before), "snapshot with spinners");
  int runnable = 0;
  uint64 sum0 = 0, sum1 = 0;
  for(int i = 0; i < n; i++){
    struct sysinfo_proc *p = findproc(&before, pids[i]);
    CHECK(p && (p->state == SYSINFO_RUNNING || p->state == SYSINFO_RUNNABLE),
          "spinner is RUNNING or RUNNABLE");
    if(p->state == SYSINFO_RUNNABLE)
      runnable++;
    sum0 += p->schedule_count;
  }
  show("CPU-bound processes compete for CPUs", &before);
  CHECK(runnable >= 1, "at least one spinner waits for a CPU: RUNNABLE");
  CHECK(sleep(5) == 0 && capture(&after), "snapshot after timer preemption");
  for(int i = 0; i < n; i++){
    struct sysinfo_proc *p = findproc(&after, pids[i]);
    CHECK(p, "spinner still listed");
    sum1 += p->schedule_count;
  }
  CHECK(sum1 > sum0, "timer interrupts keep rescheduling the spinners");
out:
  for(int i = 0; i < n; i++)
    kill(pids[i]);
  for(int i = 0; i < n; i++)
    wait(&status);
  return ok;
}

static int
fullfdtest(void)
{
  int ok = 1, fds[NOFILE], n = 0, fd;
  while(n < NOFILE && (fd = open("README", O_RDONLY)) >= 0)
    fds[n++] = fd;
  CHECK(capture(&after), "snapshot with a full descriptor table");
  struct sysinfo_proc *p = findproc(&after, getpid());
  CHECK(p && p->nfd == NOFILE, "all NOFILE descriptors are reported");
  for(int i = 0; i < NOFILE; i++)
    CHECK(slotof(&after, getpid(), i) >= 0, "every fd maps to an object");
  CHECK(quiet(&after), "references match descriptors");
out:
  for(int i = 0; i < n; i++)
    close(fds[i]);
  return ok;
}

static int
fullproctest(void)
{
  int ok = 1, p[2] = {-1, -1}, n = 0, status;
  CHECK(capture(&before), "before filling the process table");
  CHECK(pipe(p) == 0, "pipe");
  for(;;){
    int pid = fork();
    if(pid < 0)
      break;
    if(pid == 0){
      char c;
      close(p[1]);
      read(p[0], &c, 1);    // sleep until the parent closes the write end
      exit(0);
    }
    n++;
  }
  CHECK(n > 0, "fork works before the table is full");
  CHECK(childrenasleep(&after), "children asleep");
  printf("  process table full: %d processes (NPROC=%d), %d open-file objects\n",
         after.proc_count, NPROC, after.file_count);
  CHECK(after.proc_count == NPROC, "fork fails only when all NPROC slots are used");
  CHECK(quiet(&after), "references match descriptors");
  struct sysinfo_file *rf = findfile(&after, slotof(&after, getpid(), p[0]));
  CHECK(rf && rf->ref == n + 1 && rf->npids == n + 1,
        "pipe read end shared by the parent and every child");
out:
  if(p[0] >= 0) close(p[0]);
  if(p[1] >= 0) close(p[1]);
  for(int i = 0; i < n; i++)
    if(wait(&status) < 0 || status != 0)
      ok = 0;
  if(ok && (!capture(&after) || after.proc_count != before.proc_count)){
    printf("  process count not restored after wait\n");
    ok = 0;
  }
  return ok;
}

// Other processes keep opening, closing, forking and exiting while we call
// sysinfo(): every call must still succeed with a consistent snapshot.
static int
busytest(void)
{
  int ok = 1, n = 0, status, fails = 0;
  for(int i = 0; i < 3; i++){
    int pid = fork();
    if(pid < 0)
      break;
    if(pid == 0){
      char name[] = "sysinfo.busyX";
      name[12] = 'a' + i;
      for(int j = 0; j < 100; j++){
        int fd = open(name, O_CREATE | O_RDWR);
        int q[2];
        if(pipe(q) == 0){
          close(q[0]);
          close(q[1]);
        }
        int c = fork();
        if(c == 0)
          exit(0);
        if(c > 0)
          wait(0);
        if(fd >= 0)
          close(fd);
      }
      unlink(name);
      exit(0);
    }
    n++;
  }
  for(int i = 0; i < 300; i++)
    if(!capture(&after))
      fails++;
  for(int i = 0; i < n; i++)
    if(wait(&status) < 0 || status != 0)
      ok = 0;
  if(n != 3){
    printf("  could not start the busy processes\n");
    ok = 0;
  }
  if(fails){
    printf("  %d of 300 calls failed while the system was busy\n", fails);
    ok = 0;
  }
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
} tests[] = {
  {"01 basic sysinfo", basic},
  {"02 init and console", inittest},
  {"03 open", opentest},
  {"04 dup", duptest},
  {"05 close", closetest},
  {"06 pipe", pipetest},
  {"07 fork", forktest},
  {"08 multiple process references", multiproctest},
  {"09 exit/zombie/wait", exittest},
  {"10 kill sleeping process", killtest},
  {"11 reparent to init", reparenttest},
  {"12 scheduling count", scheduletest},
  {"13 runnable under CPU contention", runnabletest},
  {"14 full descriptor table", fullfdtest},
  {"15 full process table", fullproctest},
  {"16 busy system", busytest},
  {"17 invalid pointers", pointertest},
  {"18 repeated calls", repeattest},
  {"19 trace regression", tracetest},
};

int
main(int argc, char *argv[])
{
  // LAB_SYSCALL intentionally skips zeroing user pages in uvmalloc(),
  // so globals in BSS must be initialized explicitly.
  passed = failed = 0;
  if(argc == 2 && strcmp(argv[1], "show") == 0){
    if(!capture(&after))
      exit(1);
    show("sysinfo", &after);
    exit(0);
  }
  for(int i = 0; i < sizeof(tests)/sizeof(tests[0]); i++){
    printf("== %s\n", tests[i].name);
    if(tests[i].run()){
      printf("PASS %s\n", tests[i].name);
      passed++;
    } else {
      printf("FAIL %s\n", tests[i].name);
      failed++;
    }
  }
  printf("sysinfotest: %d PASS, %d FAIL\n", passed, failed);
  exit(failed ? 1 : 0);
}
