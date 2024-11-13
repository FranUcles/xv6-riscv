#include "defs.h"
#include "vma.h"
#include "riscv.h"
#include "fcntl.h"
#include "proc.h"
#include "file.h"
#include "fs.h"

void
vma_free(struct vma * vma, struct proc * p){
  for (int i = 0; i < MAXVMA; i++){
    vma_free_pages(vma, i, vma->addr[i], vma->addr[i] + vma->length[i], p->pagetable);
    vma->addr[i] = 0;
    vma->length[i] = 0;
    vma->prot[i] = 0;
    vma->flags[i] = 0;
    vma->fd[i] = 0;
    vma->offset[i] = 0;
    if (vma->file[i] != 0){
      fileclose(vma->file[i]);
    }
    vma->file[i] = 0;
  }
  vma->bottom_addr = INITIAL_BOTTOM_ADDR;
}

int
vma_find(struct vma *vma, uint64 addr){
  for (int i = 0; i < MAXVMA; i++)
    if (addr >= vma->addr[i] && addr < vma->addr[i] + vma->length[i])
      return i;
  return -1;
}

int
vma_find_free(struct vma *vma){
  for (int i = 0; i < MAXVMA; i++)
    if (vma->addr[i] == 0)
      return i;
  return -1;
}

uint64
vma_get_new_addr(struct vma *vma, int length){
  // Calculate the new address 
  uint64 new_addr = PGROUNDDOWN(vma->bottom_addr - length);
  // Update the new bottom_addr
  vma->bottom_addr = new_addr - 1;
  return new_addr;
}

int
vma_free_pages(struct vma *vma, int index, uint64 init_va, uint64 end_va, pagetable_t pagetable){
  int unmapped_length = 0;
  if (end_va > vma->addr[index] + vma->length[index])
    end_va = vma->addr[index] + vma->length[index];
  for (uint64 page = init_va; page < end_va; page += PGSIZE){
    unmapped_length += PGSIZE;
    uint64 pa = walkaddr(pagetable, page);
    if (pa == 0)
      continue;
    // If there is a physical address and the mapping is MAP_SHARED
    // we need to write the content on the file 
    if (vma->flags[index] == MAP_SHARED){
        struct file * f = vma->file[index];
        begin_op();
        ilock(f->ip);
        writei(f->ip, 1, page, vma->offset[index] + (page - init_va), PGSIZE);
        iunlock(f->ip);
        end_op();
    }
    uvmunmap(pagetable, page, 1, 1);
  }
  return unmapped_length;
}

int
vma_fill_vma(struct vma *vma, int vma_index, uint64 addr, int length, int prot, int flags, int fd, int offset, struct file *file){
 if (vma_index < 0 || vma_index >= MAXVMA)
    return -1;
  vma->addr[vma_index] = addr;
  vma->length[vma_index] = length;
  vma->prot[vma_index] = prot;
  vma->flags[vma_index] = flags;
  vma->fd[vma_index] = fd;
  vma->offset[vma_index] = offset;
  vma->file[vma_index] = file;
 return 0;
}

void 
vma_copy(struct vma *vma_src, struct vma *vma_dst){
  for (int i = 0; i < MAXVMA; i++){
    vma_dst->addr[i] = vma_src->addr[i];
    vma_dst->length[i] = vma_src->length[i];
    vma_dst->prot[i] = vma_src->prot[i];
    vma_dst->flags[i] = vma_src->flags[i];
    vma_dst->fd[i] = vma_src->fd[i];
    vma_dst->offset[i] = vma_src->offset[i];
    if (vma_src->file[i] != 0){
      filedup(vma_src->file[i]);
    }
    vma_dst->file[i] = vma_src->file[i];
  }
  vma_dst->bottom_addr = vma_src->bottom_addr;
}
