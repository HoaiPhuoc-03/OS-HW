// user/sysinfotest.c - kiem thu system call sysinfo().
//
//   sysinfotest        chay toan bo cac test
//   sysinfotest show   chi in snapshot hien tai (dung khi demo, vd: cat README | sysinfotest show)
//
// Moi snapshot deu qua sanity_check(); o cac thoi diem he thong "yen lang"
// (khong process nao dang mo/dong file) con qua check_links(): ref cua moi file
// phai bang tong so fd tro toi no, va danh sach PID phai khop voi ofile[].

#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/fcntl.h"
#include "kernel/sysinfo.h"
#include "user/user.h"

// struct sysinfo ~34 KB > user stack (4 KB) => phai la bien toan cuc.
static struct sysinfo info;
static struct sysinfo info2;

static char *curtest = "";

#define CHECK(cond, ...) do {                                        \
    if(!(cond)){                                                     \
      printf("FAIL [%s] dong %d: ", curtest, __LINE__);              \
      printf(__VA_ARGS__);                                           \
      printf("\n");                                                  \
      exit(1);                                                       \
    }                                                                \
  } while(0)

// ------------------------------------------------------------------ in ket qua

static char*
statestr(int s)
{
  static char *names[] = { "UNUSED", "USED", "SLEEPING", "RUNNABLE", "RUNNING", "ZOMBIE" };
  if(s >= 0 && s <= SI_ZOMBIE)
    return names[s];
  return "???";
}

static char*
typestr(int t)
{
  static char *names[] = { "NONE", "PIPE", "INODE", "DEVICE" };
  if(t >= 0 && t <= SI_FD_DEVICE)
    return names[t];
  return "???";
}

static void
pad(int n)
{
  while(n-- > 0)
    printf(" ");
}

static int
ndigits(long v)
{
  int n = (v < 0) ? 2 : 1;
  if(v < 0) v = -v;
  while(v >= 10){ v /= 10; n++; }
  return n;
}

static void pint(long v, int w) { printf("%ld", v); pad(w - ndigits(v)); }
static void pstr(char *s, int w) { printf("%s", s); pad(w - (int)strlen(s)); }

static void
print_info(struct sysinfo *si, char *title)
{
  printf("\n===== %s =====\n", title);
  printf("Open-file objects: %ld\n", si->nfile);
  printf("  slot type    mode ref  pids\n");
  for(int i = 0; i < si->nfile; i++){
    struct fileinfo *f = &si->files[i];
    printf("  ");
    pint(f->slot, 5);
    pstr(typestr(f->type), 8);
    printf("%s%s   ", f->readable ? "r" : "-", f->writable ? "w" : "-");
    pint(f->ref, 5);
    for(int k = 0; k < f->npids; k++)
      printf("%d ", f->pids[k]);
    printf("\n");
  }
  printf("Processes (state != UNUSED): %ld\n", si->nproc);
  printf("  pid  ppid state     nsched name         fd->slot\n");
  for(int i = 0; i < si->nproc; i++){
    struct procinfo *p = &si->procs[i];
    printf("  ");
    pint(p->pid, 5);
    pint(p->ppid, 5);
    pstr(statestr(p->state), 10);
    pint(p->nsched, 7);
    pstr(p->name, 13);
    for(int fd = 0; fd < SI_NOFILE; fd++)
      if(p->ofile[fd] >= 0)
        printf("%d->%d ", fd, p->ofile[fd]);
    printf("\n");
  }
}

// ------------------------------------------------------------------ tien ich

static struct procinfo*
findproc(struct sysinfo *si, int pid)
{
  for(int i = 0; i < si->nproc; i++)
    if(si->procs[i].pid == pid)
      return &si->procs[i];
  return 0;
}

static struct fileinfo*
findfile(struct sysinfo *si, int slot)
{
  for(int i = 0; i < si->nfile; i++)
    if(si->files[i].slot == slot)
      return &si->files[i];
  return 0;
}

static int
haspid(struct fileinfo *f, int pid)
{
  for(int k = 0; k < f->npids; k++)
    if(f->pids[k] == pid)
      return 1;
  return 0;
}

// Bat bien luon dung, ke ca khi process khac dang chay.
static void
sanity_check(struct sysinfo *si)
{
  CHECK(si->nproc >= 1 && si->nproc <= SI_NPROC, "nproc=%ld ngoai [1,%d]", si->nproc, SI_NPROC);
  CHECK(si->nfile <= SI_NFILE, "nfile=%ld > %d", si->nfile, SI_NFILE);
  for(int i = 0; i < si->nproc; i++){
    struct procinfo *p = &si->procs[i];
    CHECK(p->pid > 0, "pid=%d khong hop le", p->pid);
    CHECK(p->state >= SI_USED && p->state <= SI_ZOMBIE, "pid %d co state=%d", p->pid, p->state);
    for(int j = i + 1; j < si->nproc; j++)
      CHECK(si->procs[j].pid != p->pid, "pid %d xuat hien 2 lan", p->pid);
    int n = 0;
    for(int fd = 0; fd < SI_NOFILE; fd++){
      CHECK(p->ofile[fd] >= -1 && p->ofile[fd] < SI_NFILE, "pid %d fd %d slot=%d", p->pid, fd, p->ofile[fd]);
      if(p->ofile[fd] >= 0)
        n++;
    }
    CHECK(n == p->nofile, "pid %d: nofile=%d nhung dem duoc %d fd", p->pid, p->nofile, n);
  }
  for(int i = 0; i < si->nfile; i++){
    struct fileinfo *f = &si->files[i];
    CHECK(f->slot >= 0 && f->slot < SI_NFILE, "slot=%d ngoai bang", f->slot);
    CHECK(f->ref > 0, "slot %d co ref=%d", f->slot, f->ref);
    CHECK(f->npids >= 0 && f->npids <= SI_NPROC, "slot %d npids=%d", f->slot, f->npids);
  }
}

// Bat bien khi he thong yen lang: lien ket process <-> file phai khop tuyet doi.
static void
check_links(struct sysinfo *si)
{
  int ninit = 0;
  for(int i = 0; i < si->nfile; i++){
    struct fileinfo *f = &si->files[i];
    int nfd = 0, np = 0;
    CHECK(f->type >= SI_FD_PIPE && f->type <= SI_FD_DEVICE, "slot %d type=%d", f->slot, f->type);
    for(int j = 0; j < si->nproc; j++){
      int has = 0;
      for(int fd = 0; fd < SI_NOFILE; fd++)
        if(si->procs[j].ofile[fd] == f->slot){ nfd++; has = 1; }
      if(has){
        np++;
        CHECK(haspid(f, si->procs[j].pid), "slot %d thieu pid %d", f->slot, si->procs[j].pid);
      }
    }
    CHECK(nfd == f->ref, "slot %d: ref=%d nhung co %d fd tro toi", f->slot, f->ref, nfd);
    CHECK(np == f->npids, "slot %d: npids=%d nhung co %d process", f->slot, f->npids, np);
  }
  for(int j = 0; j < si->nproc; j++){
    struct procinfo *p = &si->procs[j];
    for(int fd = 0; fd < SI_NOFILE; fd++)
      if(p->ofile[fd] >= 0)
        CHECK(findfile(si, p->ofile[fd]) != 0, "pid %d fd %d -> slot %d khong co trong ds file",
              p->pid, fd, p->ofile[fd]);
    if(p->ppid == 0)
      ninit++;
    else
      CHECK(findproc(si, p->ppid) != 0, "pid %d co ppid %d khong ton tai", p->pid, p->ppid);
  }
  CHECK(ninit == 1, "co %d process khong co cha (chi init moi duoc)", ninit);
}

static void
snap(struct sysinfo *si)
{
  CHECK(sysinfo(si) == 0, "sysinfo() tra ve loi");
  sanity_check(si);
}

// Doi toi khi process pid dat trang thai state (toi da ~200 tick).
static struct procinfo*
wait_state(struct sysinfo *si, int pid, int state)
{
  for(int i = 0; i < 200; i++){
    snap(si);
    struct procinfo *p = findproc(si, pid);
    if(p && p->state == state)
      return p;
    sleep(1);
  }
  return 0;
}

// Doi toi khi moi process con cua minh deu SLEEPING (khong con dang mo/dong
// file giua chung), de check_links() khong bat nham trang thai trung gian:
//  (1) sys_close() xoa ofile[fd] TRUOC roi moi giam ref trong fileclose();
//  (2) sysinfo chup bang file truoc, bang process sau -> hai thoi diem khac nhau.
static void
wait_children_asleep(struct sysinfo *si)
{
  int me = getpid();
  for(int t = 0; t < 200; t++){
    snap(si);
    int busy = 0;
    for(int i = 0; i < si->nproc; i++)
      if(si->procs[i].ppid == me && si->procs[i].state != SI_SLEEPING)
        busy++;
    if(busy == 0){
      // Chup lai: snapshot vua roi chup bang file TRUOC bang process, nen
      // phan file co the cu hon. Snapshot moi bat dau khi moi con da ngu.
      snap(si);
      return;
    }
    sleep(1);
  }
  CHECK(0, "cac process con khong chiu ngu");
}

static void
begin(char *name)
{
  curtest = name;
  printf("test %s: ", name);
}

static void
ok(void)
{
  printf("OK\n");
}

// ------------------------------------------------------------------ cac test

// Trang thai ban dau: init, sh, chinh minh; console duoc chia se qua fd 0,1,2.
void
test_initial(void)
{
  begin("initial");
  snap(&info);
  check_links(&info);
  print_info(&info, "Trang thai ban dau");

  int me = getpid();
  struct procinfo *p = findproc(&info, me);
  CHECK(p != 0, "khong thay chinh minh (pid %d)", me);
  CHECK(p->state == SI_RUNNING, "process goi sysinfo phai RUNNING, la %s", statestr(p->state));
  CHECK(strcmp(p->name, "sysinfotest") == 0, "ten la '%s'", p->name);
  CHECK(p->nsched >= 1, "nsched=%ld, da chay thi phai >= 1", p->nsched);

  struct procinfo *init = findproc(&info, 1);
  CHECK(init != 0 && init->ppid == 0, "init (pid 1) phai co ppid 0");
  CHECK(init->state == SI_SLEEPING, "init dang wait() nen phai SLEEPING");

  struct procinfo *par = findproc(&info, p->ppid);
  CHECK(par != 0, "khong thay process cha %d", p->ppid);
  CHECK(par->state == SI_SLEEPING, "cha dang wait() nen phai SLEEPING, la %s", statestr(par->state));

  // fd 0,1,2 duoc init tao bang open("console") + dup + dup -> cung mot slot
  CHECK(p->ofile[0] >= 0 && p->ofile[0] == p->ofile[1] && p->ofile[1] == p->ofile[2],
        "fd 0,1,2 phai cung tro toi console");
  struct fileinfo *con = findfile(&info, p->ofile[0]);
  CHECK(con->type == SI_FD_DEVICE && con->readable && con->writable, "console phai la DEVICE rw");
  CHECK(haspid(con, 1) && haspid(con, me) && haspid(con, par->pid), "console thieu pid init/sh/minh");
  ok();
}

// Doi so sai: con tro rac, NULL (trang text chi doc), vung nho chi hop le mot phan.
void
test_badargs(void)
{
  begin("badargs");
  CHECK(sysinfo((struct sysinfo *)0xeaeb0b5b00002f5eULL) == -1, "con tro rac phai tra ve -1");
  CHECK(sysinfo((struct sysinfo *)0) == -1, "NULL (trang text chi doc) phai tra ve -1");
  char *mem = sbrk(4096);          // 1 trang heap moi; byte cuoi cung la dinh bo nho
  CHECK(mem != (char *)-1, "sbrk that bai");
  CHECK(sysinfo((struct sysinfo *)(mem + 4096 - 64)) == -1, "struct vuot qua dinh bo nho phai tra ve -1");
  sbrk(-4096);
  CHECK(sysinfo(&info) == 0, "goi lai voi con tro dung phai thanh cong");
  ok();
}

// fork(): nproc +1, ppid dung, con thua huong fd; wait(): nproc tro lai.
void
test_fork(void)
{
  begin("fork");
  snap(&info);
  long n0 = info.nproc;
  int me = getpid();
  struct procinfo mine = *findproc(&info, me);

  int pid = fork();
  CHECK(pid >= 0, "fork that bai");
  if(pid == 0){
    snap(&info);
    struct procinfo *c = findproc(&info, getpid());
    CHECK(info.nproc == n0 + 1, "trong con nproc=%ld, mong doi %ld", info.nproc, n0 + 1);
    CHECK(c && c->ppid == me, "ppid cua con phai la %d", me);
    CHECK(c->state == SI_RUNNING, "con dang goi sysinfo phai RUNNING");
    CHECK(strcmp(c->name, mine.name) == 0, "fork phai chep ten process");
    for(int fd = 0; fd < SI_NOFILE; fd++)
      CHECK(c->ofile[fd] == mine.ofile[fd], "fd %d cua con tro slot %d, cua cha %d",
            fd, c->ofile[fd], mine.ofile[fd]);
    CHECK(wait_state(&info, me, SI_SLEEPING) != 0, "cha dang wait() phai SLEEPING");
    exit(0);
  }
  int st = -1;
  CHECK(wait(&st) == pid && st == 0, "con bao loi (status %d)", st);
  snap(&info);
  check_links(&info);
  CHECK(info.nproc == n0, "sau wait nproc=%ld, mong doi %ld", info.nproc, n0);
  CHECK(findproc(&info, pid) == 0, "con %d van con trong bang sau wait", pid);
  ok();
}

// exit() -> ZOMBIE (van duoc dem, da dong het file); kill() mot process SLEEPING.
void
test_zombie_kill(void)
{
  begin("zombie+kill");
  snap(&info);
  long n0 = info.nproc;
  int me = getpid();

  int pid = fork();
  CHECK(pid >= 0, "fork that bai");
  if(pid == 0)
    exit(7);
  CHECK(wait_state(&info, pid, SI_ZOMBIE) != 0, "con da exit nhung khong thanh ZOMBIE");
  snap(&info);   // chup lai sau khi con da ZOMBIE de phan file cung moi
  struct procinfo *z = findproc(&info, pid);
  CHECK(z->nofile == 0, "ZOMBIE phai da dong het file, con %d", z->nofile);
  CHECK(z->ppid == me, "ppid cua zombie phai la %d", me);
  CHECK(info.nproc == n0 + 1, "ZOMBIE van phai duoc dem (state != UNUSED)");
  check_links(&info);
  print_info(&info, "Co mot ZOMBIE chua duoc wait()");
  int st;
  CHECK(wait(&st) == pid && st == 7, "wait phai tra ve status 7");
  snap(&info);
  CHECK(info.nproc == n0 && findproc(&info, pid) == 0, "sau wait zombie phai bien mat");

  // kill() mot process dang ngu tren pipe
  int p[2];
  CHECK(pipe(p) == 0, "pipe that bai");
  pid = fork();
  CHECK(pid >= 0, "fork that bai");
  if(pid == 0){
    char c;
    close(p[1]);
    read(p[0], &c, 1);   // ngu mai mai vi khong ai ghi
    exit(0);
  }
  CHECK(wait_state(&info, pid, SI_SLEEPING) != 0, "con doc pipe rong phai SLEEPING");
  CHECK(kill(pid) == 0, "kill that bai");
  CHECK(wait(&st) == pid && st == -1, "process bi kill phai co status -1, la %d", st);
  close(p[0]);
  close(p[1]);
  snap(&info);
  CHECK(info.nproc == n0, "sau kill+wait nproc=%ld, mong doi %ld", info.nproc, n0);
  ok();
}

// Nhieu process tinh toan hon so CPU -> co process RUNNABLE; nsched tang do timer.
#define NSPIN 4
void
test_runnable(void)
{
  begin("runnable");
  int pids[NSPIN];
  for(int i = 0; i < NSPIN; i++){
    pids[i] = fork();
    CHECK(pids[i] >= 0, "fork that bai");
    if(pids[i] == 0)
      for(;;)
        ;
  }
  sleep(2);
  snap(&info);
  int nrun = 0, nrunnable = 0;
  for(int i = 0; i < NSPIN; i++){
    struct procinfo *s = findproc(&info, pids[i]);
    CHECK(s != 0, "khong thay process tinh toan %d", pids[i]);
    CHECK(s->state == SI_RUNNING || s->state == SI_RUNNABLE,
          "process tinh toan %d phai RUNNING/RUNNABLE, la %s", s->pid, statestr(s->state));
    if(s->state == SI_RUNNING) nrun++; else nrunnable++;
  }
  print_info(&info, "4 process tinh toan tranh CPU");
  // process dang goi sysinfo chiem 1 CPU; QEMU mac dinh 3 CPU => it nhat 1 RUNNABLE
  CHECK(nrunnable >= 1, "voi %d process tinh toan phai co it nhat 1 RUNNABLE (running=%d)", NSPIN, nrun);

  sleep(5);
  snap(&info2);
  for(int i = 0; i < NSPIN; i++){
    struct procinfo *a = findproc(&info, pids[i]);
    struct procinfo *b = findproc(&info2, pids[i]);
    CHECK(b->nsched > a->nsched, "pid %d bi timer ngat lien tuc nhung nsched khong tang (%ld -> %ld)",
          pids[i], a->nsched, b->nsched);
  }
  for(int i = 0; i < NSPIN; i++)
    kill(pids[i]);
  for(int i = 0; i < NSPIN; i++)
    wait(0);
  ok();
}

// nsched: tang moi lan duoc lap lich lai, reset khi cap phat process,
// khong tang khi dang SLEEPING.
void
test_nsched(void)
{
  begin("nsched");
  int me = getpid();
  snap(&info);
  uint64 s0 = findproc(&info, me)->nsched;
  for(int i = 0; i < 5; i++)
    sleep(1);          // moi lan ngu roi thuc day = 1 lan scheduler() chon lai
  snap(&info);
  uint64 s1 = findproc(&info, me)->nsched;
  CHECK(s1 >= s0 + 5, "sau 5 lan sleep nsched chi tang %ld -> %ld", s0, s1);

  int sync[2];
  CHECK(pipe(sync) == 0, "pipe that bai");
  int pid = fork();
  CHECK(pid >= 0, "fork that bai");
  if(pid == 0){
    close(sync[0]);
    snap(&info);
    struct procinfo *c = findproc(&info, getpid());
    struct procinfo *par = findproc(&info, me);
    // con duoc allocproc() reset ve 0, khong thua huong bo dem cua cha
    CHECK(c->nsched >= 1 && c->nsched < par->nsched,
          "con moi chay phai co nsched nho (con=%ld, cha=%ld)", c->nsched, par->nsched);
    // cha dang chan tren read(sync): nsched khong duoc tang khi SLEEPING
    struct procinfo *a = wait_state(&info, me, SI_SLEEPING);
    CHECK(a != 0, "cha phai SLEEPING khi doc pipe");
    uint64 x = a->nsched;
    sleep(5);
    snap(&info);
    struct procinfo *b = findproc(&info, me);
    CHECK(b->state == SI_SLEEPING && b->nsched == x,
          "cha dang ngu ma nsched doi %ld -> %ld", x, b->nsched);
    write(sync[1], "x", 1);
    exit(0);
  }
  close(sync[1]);
  char c;
  read(sync[0], &c, 1);
  close(sync[0]);
  int st = -1;
  CHECK(wait(&st) == pid && st == 0, "con bao loi (status %d)", st);
  snap(&info);
  CHECK(findproc(&info, me)->nsched > s1, "cha thuc day sau read nhung nsched khong tang");
  ok();
}

// Cha chet truoc -> chau duoc reparent() cho init, ppid doi thanh 1.
void
test_reparent(void)
{
  begin("reparent");
  int p[2];
  CHECK(pipe(p) == 0, "pipe that bai");
  int c = fork();
  CHECK(c >= 0, "fork that bai");
  if(c == 0){
    int g = fork();
    if(g < 0)
      exit(1);
    if(g == 0){
      int msg[2] = { 0, getpid() };   // {ket qua, pid cua chau}
      for(int i = 0; i < 200; i++){
        snap(&info);
        if(findproc(&info, getpid())->ppid == 1){
          msg[0] = 1;
          break;
        }
        sleep(1);
      }
      write(p[1], msg, sizeof(msg));
      exit(0);
    }
    exit(0);   // con thoat truoc chau
  }
  close(p[1]);
  int st;
  wait(&st);
  CHECK(st == 0, "process con loi");
  int msg[2] = { 0, 0 };
  CHECK(read(p[0], msg, sizeof(msg)) == sizeof(msg), "khong nhan duoc ket qua tu chau");
  close(p[0]);
  CHECK(msg[0] == 1, "chau mo coi khong duoc chuyen cho init (ppid != 1)");
  // doi init wait() chau xong, de cac test sau thay he thong yen lang
  for(int i = 0; i < 200; i++){
    snap(&info);
    if(findproc(&info, msg[1]) == 0)
      break;
    sleep(1);
  }
  CHECK(findproc(&info, msg[1]) == 0, "init khong don chau %d", msg[1]);
  ok();
}

// Danh sach file cua process qua open / dup / close / pipe / fork.
void
test_ofile(void)
{
  begin("ofile");
  int me = getpid();
  snap(&info);
  int k = findproc(&info, me)->nofile;

  int fd = open("sitest_f", O_CREATE | O_RDWR);
  CHECK(fd >= 0, "open that bai");
  snap(&info);
  struct procinfo *p = findproc(&info, me);
  int s = p->ofile[fd];
  struct fileinfo *f = findfile(&info, s);
  CHECK(p->nofile == k + 1 && s >= 0, "sau open nofile=%d", p->nofile);
  CHECK(f->type == SI_FD_INODE && f->readable && f->writable && f->ref == 1,
        "file vua mo phai INODE rw ref 1");
  CHECK(f->npids == 1 && f->pids[0] == me, "chi minh tham chieu file vua mo");

  int fd2 = dup(fd);
  int ro = open("sitest_f", O_RDONLY);
  int wo = open("sitest_f", O_WRONLY);
  int pp[2];
  CHECK(fd2 >= 0 && ro >= 0 && wo >= 0 && pipe(pp) == 0, "dup/open/pipe that bai");
  snap(&info);
  check_links(&info);
  p = findproc(&info, me);
  CHECK(p->nofile == k + 6, "nofile=%d, mong doi %d", p->nofile, k + 6);
  CHECK(p->ofile[fd2] == s, "dup() phai tro toi cung slot %d", s);
  f = findfile(&info, s);
  CHECK(f->ref == 2 && f->npids == 1, "sau dup: ref=%d (2), npids=%d (1, khong lap pid)", f->ref, f->npids);
  CHECK(p->ofile[ro] != s && p->ofile[wo] != s, "open() lan nua phai tao open-file object moi");
  struct fileinfo *fr = findfile(&info, p->ofile[ro]);
  struct fileinfo *fw = findfile(&info, p->ofile[wo]);
  CHECK(fr->readable && !fr->writable, "O_RDONLY phai la r-");
  CHECK(!fw->readable && fw->writable, "O_WRONLY phai la -w");
  struct fileinfo *p0 = findfile(&info, p->ofile[pp[0]]);
  struct fileinfo *p1 = findfile(&info, p->ofile[pp[1]]);
  CHECK(p0->type == SI_FD_PIPE && p0->readable && !p0->writable, "dau doc pipe phai PIPE r-");
  CHECK(p1->type == SI_FD_PIPE && !p1->readable && p1->writable, "dau ghi pipe phai PIPE -w");

  // fork: con chia se moi open-file object; con dong fd, fd2 roi bao cho cha
  int go[2], ack[2];
  CHECK(pipe(go) == 0 && pipe(ack) == 0, "pipe that bai");
  int pid = fork();
  CHECK(pid >= 0, "fork that bai");
  if(pid == 0){
    char c;
    close(fd);
    close(fd2);
    write(ack[1], "x", 1);
    read(go[0], &c, 1);     // cho cha kiem tra xong
    exit(0);
  }
  char c;
  read(ack[0], &c, 1);
  snap(&info);
  check_links(&info);
  print_info(&info, "Sau open/dup/pipe va fork (con da dong 2 fd)");
  struct procinfo *cp = findproc(&info, pid);
  p = findproc(&info, me);
  CHECK(cp->ofile[fd] == -1 && cp->ofile[fd2] == -1, "con da close nhung fd van con");
  CHECK(cp->nofile == p->nofile - 2, "con phai it hon cha 2 fd");
  CHECK(cp->ofile[ro] == p->ofile[ro] && cp->ofile[pp[0]] == p->ofile[pp[0]],
        "fd thua huong qua fork phai cung slot");
  f = findfile(&info, s);
  CHECK(f->ref == 2 && f->npids == 1 && f->pids[0] == me, "con dong fd thi file chi con cha tham chieu");
  fr = findfile(&info, p->ofile[ro]);
  CHECK(fr->ref == 2 && fr->npids == 2 && haspid(fr, me) && haspid(fr, pid), "file ro phai do 2 process chia se");

  write(go[1], "x", 1);
  wait(0);
  close(fd); close(fd2); close(ro); close(wo);
  close(pp[0]); close(pp[1]);
  close(go[0]); close(go[1]); close(ack[0]); close(ack[1]);
  snap(&info);
  check_links(&info);
  CHECK(findproc(&info, me)->nofile == k, "sau khi dong het, nofile phai ve %d", k);
  CHECK(findfile(&info, s) == 0, "file da dong het fd phai bien mat khoi ftable");
  unlink("sitest_f");
  ok();
}

// Bang fd cua process day (NOFILE fd).
void
test_fullfd(void)
{
  begin("fullfd");
  int me = getpid();
  int fds[SI_NOFILE];
  int n = 0;
  int fd;
  while((fd = open("README", O_RDONLY)) >= 0)
    fds[n++] = fd;
  snap(&info);
  check_links(&info);
  struct procinfo *p = findproc(&info, me);
  CHECK(p->nofile == SI_NOFILE, "bang fd day nhung nofile=%d", p->nofile);
  for(int i = 0; i < SI_NOFILE; i++)
    CHECK(p->ofile[i] >= 0, "fd %d trong du bang day", i);
  for(int i = 0; i < n; i++)
    close(fds[i]);
  ok();
}

// Bang process day (NPROC process): danh sach process va danh sach pid cua console deu day.
void
test_fullproc(void)
{
  begin("fullproc");
  snap(&info);
  long n0 = info.nproc;
  int p[2];
  CHECK(pipe(p) == 0, "pipe that bai");
  int n = 0;
  for(;;){
    int pid = fork();
    if(pid < 0)
      break;
    if(pid == 0){
      char c;
      close(p[1]);
      read(p[0], &c, 1);    // ngu toi khi cha dong dau ghi
      exit(0);
    }
    n++;
  }
  wait_children_asleep(&info);
  check_links(&info);
  printf("\n===== Bang process day: %d con + %ld process san co =====\n", n, n0);
  printf("nproc = %ld (NPROC = %d), nfile = %ld\n", info.nproc, SI_NPROC, info.nfile);
  CHECK(info.nproc == SI_NPROC, "fork bi tu choi nhung nproc=%ld != NPROC", info.nproc);
  struct procinfo *me = findproc(&info, getpid());
  struct fileinfo *con = findfile(&info, me->ofile[0]);
  printf("console (slot %d): ref = %d, npids = %d\n", con->slot, con->ref, con->npids);
  CHECK(con->npids == SI_NPROC, "moi process deu giu console, npids=%d", con->npids);
  close(p[0]);
  close(p[1]);
  for(int i = 0; i < n; i++)
    wait(0);
  snap(&info);
  check_links(&info);
  CHECK(info.nproc == n0, "sau khi don dep nproc=%ld, mong doi %ld", info.nproc, n0);
  ok();
}

int
main(int argc, char *argv[])
{
  if(argc > 1 && strcmp(argv[1], "show") == 0){
    snap(&info);
    print_info(&info, "sysinfo");
    exit(0);
  }

  printf("sysinfotest: start\n");
  test_initial();
  test_badargs();
  test_fork();
  test_zombie_kill();
  test_runnable();
  test_nsched();
  test_reparent();
  test_ofile();
  test_fullfd();
  test_fullproc();
  printf("sysinfotest: OK\n");
  exit(0);
}
