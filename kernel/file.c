//
// Support functions for system calls that involve file descriptors.
//

#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "fs.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "file.h"
#include "stat.h"
#include "proc.h"
#include "sysinfo.h"

// Process table is defined in proc.c; used by collect_file_info().
extern struct proc proc[NPROC];

struct devsw devsw[NDEV];
struct {
  struct spinlock lock;
  struct file file[NFILE];
} ftable;

void
fileinit(void)
{
  initlock(&ftable.lock, "ftable");
}

// Allocate a file structure.
struct file*
filealloc(void)
{
  struct file *f;

  acquire(&ftable.lock);
  for(f = ftable.file; f < ftable.file + NFILE; f++){
    if(f->ref == 0){
      f->ref = 1;
      release(&ftable.lock);
      return f;
    }
  }
  release(&ftable.lock);
  return 0;
}

// Increment ref count for file f.
struct file*
filedup(struct file *f)
{
  acquire(&ftable.lock);
  if(f->ref < 1)
    panic("filedup");
  f->ref++;
  release(&ftable.lock);
  return f;
}

// Close file f.  (Decrement ref count, close when reaches 0.)
void
fileclose(struct file *f)
{
  struct file ff;

  acquire(&ftable.lock);
  if(f->ref < 1)
    panic("fileclose");
  if(--f->ref > 0){
    release(&ftable.lock);
    return;
  }
  ff = *f;
  f->ref = 0;
  f->type = FD_NONE;
  release(&ftable.lock);

  if(ff.type == FD_PIPE){
    pipeclose(ff.pipe, ff.writable);
  } else if(ff.type == FD_INODE || ff.type == FD_DEVICE){
    begin_op();
    iput(ff.ip);
    end_op();
  }
}

// Get metadata about file f.
// addr is a user virtual address, pointing to a struct stat.
int
filestat(struct file *f, uint64 addr)
{
  struct proc *p = myproc();
  struct stat st;
  
  if(f->type == FD_INODE || f->type == FD_DEVICE){
    ilock(f->ip);
    stati(f->ip, &st);
    iunlock(f->ip);
    if(copyout(p->pagetable, addr, (char *)&st, sizeof(st)) < 0)
      return -1;
    return 0;
  }
  return -1;
}

// Read from file f.
// addr is a user virtual address.
int
fileread(struct file *f, uint64 addr, int n)
{
  int r = 0;

  if(f->readable == 0)
    return -1;

  if(f->type == FD_PIPE){
    r = piperead(f->pipe, addr, n);
  } else if(f->type == FD_DEVICE){
    if(f->major < 0 || f->major >= NDEV || !devsw[f->major].read)
      return -1;
    r = devsw[f->major].read(1, addr, n);
  } else if(f->type == FD_INODE){
    ilock(f->ip);
    if((r = readi(f->ip, 1, addr, f->off, n)) > 0)
      f->off += r;
    iunlock(f->ip);
  } else {
    panic("fileread");
  }

  return r;
}

// Write to file f.
// addr is a user virtual address.
int
filewrite(struct file *f, uint64 addr, int n)
{
  int r, ret = 0;

  if(f->writable == 0)
    return -1;

  if(f->type == FD_PIPE){
    ret = pipewrite(f->pipe, addr, n);
  } else if(f->type == FD_DEVICE){
    if(f->major < 0 || f->major >= NDEV || !devsw[f->major].write)
      return -1;
    ret = devsw[f->major].write(1, addr, n);
  } else if(f->type == FD_INODE){
    // write a few blocks at a time to avoid exceeding
    // the maximum log transaction size, including
    // i-node, indirect block, allocation blocks,
    // and 2 blocks of slop for non-aligned writes.
    // this really belongs lower down, since writei()
    // might be writing a device like the console.
    int max = ((MAXOPBLOCKS-1-1-2) / 2) * BSIZE;
    int i = 0;
    while(i < n){
      int n1 = n - i;
      if(n1 > max)
        n1 = max;

      begin_op();
      ilock(f->ip);
      if ((r = writei(f->ip, 1, addr + i, f->off, n1)) > 0)
        f->off += r;
      iunlock(f->ip);
      end_op();

      if(r != n1){
        // error from writei
        break;
      }
      i += r;
    }
    ret = (i == n ? n : -1);
  } else {
    panic("filewrite");
  }

  return ret;
}

// ---------------------------------------------------------------
// sysinfo support
// ---------------------------------------------------------------

// Return the ftable slot index of open-file object f,
// or -1 if f is null / not in ftable.
// Used by proc.c to describe each process's open files.
int
sysinfo_file_slot(struct file *f)
{
  if(f < ftable.file || f >= ftable.file + NFILE)
    return -1;
  return (int)(f - ftable.file);
}

// Fill si->nfiles and si->files[] with every open-file object
// (ref > 0), including the PIDs that reference each one.
// si must be a kernel buffer; the caller (sys_sysinfo) copies it
// out to user space. Returns the number of open files found.
//
// Lock order: ftable.lock and p->lock are never held together.
// f->ref is not modified, so sysinfo has no side effect on the system.
int
collect_file_info(struct sysinfo *si)
{
  int n = 0;

  for(int i = 0; i < NFILE && n < SYSINFO_MAX_FILES; i++){
    struct file *f = &ftable.file[i];
    struct fileinfo *fi = &si->files[n];

    memset(fi, 0, sizeof(*fi));

    // Snapshot the file's metadata under ftable.lock.
    acquire(&ftable.lock);
    if(f->ref == 0){
      release(&ftable.lock);
      continue;
    }
    fi->id = i;
    fi->type = f->type;
    fi->readable = f->readable;
    fi->writable = f->writable;
    fi->ref = f->ref;
    release(&ftable.lock);

    // Find processes that hold this file in their fd table.
    // Only the pointer value is compared; f is never dereferenced here.
    for(int j = 0; j < NPROC; j++){
      struct proc *p = &proc[j];
      int found = 0;
      int pid = 0;

      acquire(&p->lock);
      if(p->state != UNUSED){
        pid = p->pid;
        for(int fd = 0; fd < NOFILE; fd++){
          if(p->ofile[fd] == f){
            found = 1;
            break;
          }
        }
      }
      release(&p->lock);

      if(found){
        if(fi->npids < SYSINFO_MAX_PIDS)
          fi->pids[fi->npids] = pid;
        fi->npids++;   // keep counting even if the list is full
      }
    }
    n++;
  }

  si->nfiles = n;
  return n;
}