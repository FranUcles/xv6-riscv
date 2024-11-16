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

uint64
mmap(uint64 addr, int length, int prot, int flags, int fd, struct file* file, int offset){
 struct proc *p = myproc();
 int free_vma_index = vma_find_free(&(p->vma_list));
 // In case there is no free VMA, we return error 
 if (free_vma_index == -1)
    return -1;
 if (addr == 0)
    // Select the new virtual address
    addr = vma_get_new_addr(&(p->vma_list), length); 
 // We fill the VMA 
 int correct_filled = vma_fill_vma(&(p->vma_list), free_vma_index, addr, length, prot, flags, fd, offset, file);
 if (correct_filled == -1)
    return -1;
 // Increase the references to the file
 filedup(file);
 return addr; 
}

int 
munmap(uint64 addr, int length){
  struct proc *p = myproc();
  // Search which is the VMA we want to unmap
  int vma_index = vma_find(&(p->vma_list), addr);
  // Find the page address we want to unmap
  uint64 page_addr = PGROUNDDOWN(addr);
  // Check if the unmap is at the beginning, the end or the middle
  if (p->vma_list.addr[vma_index] == page_addr){
    // We are at the beginning of the mapping
    int unmapped_length = vma_free_pages(&(p->vma_list), vma_index, page_addr, addr + length, p->pagetable);
    p->vma_list.addr[vma_index] += unmapped_length;
    p->vma_list.length[vma_index] -= unmapped_length;
    p->vma_list.offset[vma_index] += unmapped_length;
  } else if (p->vma_list.addr[vma_index] + p->vma_list.length[vma_index] ==
             page_addr + length){
    // We are at the end of the mapping 
    int unmapped_length = vma_free_pages(&(p->vma_list), vma_index, page_addr, page_addr + length, p->pagetable);
    p->vma_list.length[vma_index] -= unmapped_length;
  } else {
    // We are at the middle of the mapping
    // Search if there is another empty vma 
    int new_vma_index = vma_find_free(&(p->vma_list));
    if (new_vma_index == -1)
      return -1;
    // We calculate the length of the first part of the vma 
    int length_p1 = page_addr - p->vma_list.addr[vma_index];
    // We free the requested part
    int unmapped_length = vma_free_pages(&(p->vma_list), vma_index, page_addr, addr + length, p->pagetable);
    // Calculate the values of the second part of the vma 
    uint64 addr_p2 = page_addr + unmapped_length;
    int length_p2 = p->vma_list.addr[vma_index] - length_p1 - unmapped_length;
    int offset_p2 = p->vma_list.offset[vma_index] + length_p1 + unmapped_length;
    // Fill the second part of the vma 
    vma_fill_vma(&(p->vma_list), new_vma_index, addr_p2, length_p2, p->vma_list.prot[vma_index], 
                  p->vma_list.flags[vma_index], p->vma_list.fd[vma_index], offset_p2, p->vma_list.file[vma_index]);
    filedup(p->vma_list.file[vma_index]);
    // Update the first part of the vma 
    p->vma_list.length[vma_index] = length_p1;
  } 
  return 0;
}
