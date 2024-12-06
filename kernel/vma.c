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
    vma_free_pages(vma, i, vma->addr[i], vma->end_addr[i], p->pagetable);
    vma->addr[i] = 0;
    vma->length[i] = 0;
    vma->end_addr[i] = 0;
    vma->prot[i] = 0;
    vma->flags[i] = 0;
    vma->fd[i] = 0;
    vma->offset[i] = 0;
    if (vma->file[i] != 0)
      fileclose(vma->file[i]);
    vma->file[i] = 0;
  }
  vma->bottom_addr = INITIAL_BOTTOM_ADDR;
}

int
vma_find(struct vma *vma, uint64 addr){
  for (int i = 0; i < MAXVMA; i++)
    if (addr >= vma->addr[i] && addr < vma->end_addr[i])
      return i;
  return -1;
}

int
vma_find_free(struct vma *vma){
  for (int i = 0; i < MAXVMA; i++)
    if (vma->length[i] == 0)
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
  if (end_va > vma->end_addr[index])
    end_va = vma->end_addr[index];
  for (uint64 page = init_va; page < end_va; page += PGSIZE){
    unmapped_length += PGSIZE;
    uint64 pa = walkaddr(pagetable, page);
    if (pa == 0)
      continue;
    // If there is a physical address and the mapping is MAP_SHARED
    // we need to write the content on the file (only when the page was written)
    pte_t pa_pte = *walk(pagetable, page, 0);
    int modified = (pa_pte & PTE_D);
    if (modified != 0 && vma->flags[index] == MAP_SHARED){
        struct file * f = vma->file[index];
        begin_op();
        ilock(f->ip);
        writei(f->ip, 1, page, vma->offset[index] + (page - init_va), PGSIZE);
        iunlock(f->ip);
        end_op();
    }
    // We need to decrease the references of the properly
    int references = getref((void *)pa);
    if (references > 1)
      decref((void * )pa);
    // If there is only our reference, we need to free the page
    uvmunmap(pagetable, page, 1, references == 1);
  }
  return unmapped_length;
}

int
vma_fill_vma(struct vma *vma, int vma_index, uint64 addr, int length, uint64 end_addr, int prot, int flags, int fd, int offset, struct file *file){
 if (vma_index < 0 || vma_index >= MAXVMA)
    return -1;
  vma->addr[vma_index] = addr;
  vma->length[vma_index] = length;
  vma->end_addr[vma_index] = end_addr; 
  vma->prot[vma_index] = prot;
  vma->flags[vma_index] = flags;
  vma->fd[vma_index] = fd;
  vma->offset[vma_index] = offset;
  vma->file[vma_index] = file;
 return 0;
}

void 
vma_copy(struct proc *proc_src, struct proc *proc_dst){
  struct vma* vma_src = &(proc_src->vma_list);
  struct vma* vma_dst = &(proc_dst->vma_list);
  for (int i = 0; i < MAXVMA; i++){
    // Check if it is an empty vma 
    if (vma_src->length[i] <= 0)
      continue;
    // If the mapping is private, we need to remove the write page permission 
    if(vma_src->flags[i] == MAP_PRIVATE){
      for (uint64 page = vma_src->addr[i]; page < vma_src->addr[i] + vma_src->length[i]; page += PGSIZE)
        if(walkaddr(proc_src->pagetable, page) != 0)
          uvmunwrite(proc_src->pagetable, page);
    }
    // Copy the physical pages mapped to the virtual addresses
    uvmcopypages(vma_src->addr[i], vma_src->addr[i] + vma_src->length[i], proc_src->pagetable, proc_dst->pagetable); 
    vma_dst->addr[i] = vma_src->addr[i];
    vma_dst->length[i] = vma_src->length[i];
    vma_dst->end_addr[i] = vma_src->end_addr[i];
    vma_dst->prot[i] = vma_src->prot[i];
    vma_dst->flags[i] = vma_src->flags[i];
    vma_dst->fd[i] = vma_src->fd[i];
    vma_dst->offset[i] = vma_src->offset[i];
    if (vma_src->file[i] != 0)
      filedup(vma_src->file[i]);
    vma_dst->file[i] = vma_src->file[i];
  }
  vma_dst->bottom_addr = vma_src->bottom_addr;
}

void
vma_clear(struct vma * vma){
  for (int i = 0; i < MAXVMA; i++){
    vma->addr[i] = 0;
    vma->length[i] = 0;
    vma->end_addr[i] = 0;
    vma->prot[i] = 0;
    vma->flags[i] = 0;
    vma->fd[i] = 0;
    vma->offset[i] = 0;
    vma->file[i] = 0;
  }
  vma->bottom_addr = INITIAL_BOTTOM_ADDR;
}

void
vma_superficial_copy(struct vma * vma_src, struct vma *vma_dst){
  for (int i = 0; i < MAXVMA; i++){
    vma_dst->addr[i] = vma_src->addr[i];
    vma_dst->length[i] = vma_src->length[i];
    vma_dst->end_addr[i] = vma_src->end_addr[i];
    vma_dst->prot[i] = vma_src->prot[i];
    vma_dst->flags[i] = vma_src->flags[i];
    vma_dst->fd[i] = vma_src->fd[i];
    vma_dst->offset[i] = vma_src->offset[i];
    vma_dst->file[i] = vma_src->file[i];
  }
  vma_dst->bottom_addr = vma_src->bottom_addr;
}
