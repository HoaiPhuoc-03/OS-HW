#include "kernel/types.h"
#include "kernel/riscv.h"
#include "kernel/memlayout.h"
#include "user/user.h"

enum { NPAGES = 32 };
static int checks;
static int failures;
static volatile uchar read_value;

static void
check(int ok, const char *name)
{
  checks++;
  if(ok){
    printf("PASS %s\n", name);
  } else {
    failures++;
    printf("FAIL %s\n", name);
  }
}

static void
expect_mask(void *addr, int npages, uint expected, const char *name)
{
  uint mask = ~0U;
  int rc = pgaccess(addr, npages, &mask);
  if(rc != 0 || mask != expected)
    printf("  rc=%d mask=%x expected=%x\n", rc, mask, expected);
  check(rc == 0 && mask == expected, name);
}

int
main(void)
{
  uint64 oldbreak = (uint64)sbrk(0);
  char *base = (char *)PGROUNDUP(oldbreak);
  int size = (uint64)base - oldbreak + NPAGES * PGSIZE;
  if(sbrk(size) == (char *)-1){
    fprintf(2, "pgaccesstest: sbrk failed\n");
    exit(1);
  }

  char *end = base + NPAGES * PGSIZE;
  volatile uchar *page = (volatile uchar *)base;
  uint mask = 0xa5a5a5a5U;

  expect_mask(base, NPAGES, 0, "fresh pages");
  page[0] = 1;
  page[5 * PGSIZE] = 1;
  read_value = page[17 * PGSIZE];
  page[31 * PGSIZE] = 1;
  expect_mask(base, NPAGES, 1U | (1U << 5) | (1U << 17) | (1U << 31),
              "read and write bitmap including bit 31");
  expect_mask(base, NPAGES, 0, "accessed bits reset after a successful call");
  read_value = page[31 * PGSIZE];
  expect_mask(base, NPAGES, 1U << 31, "previously cleared page can be marked again");

  page[2 * PGSIZE] = 1;
  expect_mask(base + PGSIZE + 17, 2, 2, "starts at the page containing an unaligned address");

  page[0] = 1;
  char *out = base + 2 * PGSIZE - 2;
  out[-1] = 'L';
  out[sizeof(mask)] = 'R';
  int rc = pgaccess(base, 1, out);
  uint copied = 0;
  memmove(&copied, out, sizeof(copied));
  check(rc == 0 && copied == 1 && out[-1] == 'L' && out[sizeof(mask)] == 'R',
        "writes exactly four bytes across a page boundary");

  page[0] = 1;
  check(pgaccess(base, 1, base) == 0, "output buffer may overlap the scanned range");
  expect_mask(base, 1, 0, "kernel copyout does not mark the page as accessed by user mode");
  memmove(&copied, base, sizeof(copied));
  check(copied == 1, "overlapping buffer contains the first snapshot");
  expect_mask(base, 1, 1, "reading the overlapping buffer marks the page again");

  check(pgaccess(base, 0, &mask) == -1, "rejects zero pages");
  check(pgaccess(base, -1, &mask) == -1, "rejects a negative page count");
  check(pgaccess(base, NPAGES + 1, &mask) == -1, "rejects more than 32 pages");
  check(pgaccess((void *)MAXVA, 1, &mask) == -1, "rejects an address at MAXVA");
  check(pgaccess((void *)~0UL, 1, &mask) == -1, "rejects an out-of-range address");
  check(pgaccess((void *)(MAXVA - PGSIZE), 2, &mask) == -1, "rejects a range crossing MAXVA");
  check(pgaccess((void *)TRAPFRAME, 1, &mask) == -1, "rejects a supervisor-only page");
  check(pgaccess(end, 1, &mask) == -1, "rejects an unmapped page");
  check(pgaccess((void *)(MAXVA / 2), 1, &mask) == -1,
        "rejects a range with no intermediate page table");

  page[31 * PGSIZE] = 1;
  check(pgaccess(end - PGSIZE, 2, &mask) == -1 && mask == 0xa5a5a5a5U,
        "invalid scan leaves the output mask intact");
  expect_mask(end - PGSIZE, 1, 1, "invalid scan preserves the access bit");
  check(pgaccess(end - PGSIZE, 1, 0) == -1, "rejects a null output buffer");
  check(pgaccess(end - PGSIZE, 1, (void *)main) == -1, "rejects a read-only output buffer");
  check(pgaccess(end - PGSIZE, 1, (void *)TRAPFRAME) == -1,
        "rejects a supervisor-only output buffer");
  check(pgaccess(end - PGSIZE, 1, (void *)(MAXVA - 2)) == -1,
        "rejects an output buffer crossing MAXVA");
  end[-2] = 'A';
  end[-1] = 'B';
  check(pgaccess(end - PGSIZE, 1, end - 2) == -1 && end[-2] == 'A' && end[-1] == 'B',
        "unmapped output page does not receive a partial write");

  // Use page 0 to check an invalid output does not consume its access bit.
  page[0] = 1;
  check(pgaccess(base, 1, end - 2) == -1, "rejects partially mapped output");
  expect_mask(base, 1, 1, "invalid output preserves the access bit");
  expect_mask(end - PGSIZE, 1, 1, "last page remains accessible after errors");

  page[0] = 1;
  int pid = fork();
  if(pid == 0){
    uint child_mask;
    if(pgaccess(base, 1, &child_mask) != 0 || child_mask != 0)
      exit(1);
    read_value = page[0];
    exit(pgaccess(base, 1, &child_mask) != 0 || child_mask != 1);
  }
  int status = -1;
  check(pid > 0 && wait(&status) == pid && status == 0,
        "fork starts a separate access history for the child");
  expect_mask(base, 1, 1, "child scan preserves the parent's access bit");

  printf("pgaccesstest: %d PASS, %d FAIL\n", checks - failures, failures);
  exit(failures != 0);
}
