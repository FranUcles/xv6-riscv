#ifndef _VMA_H_
#define _VMA_H_

#include "types.h"
#include "defs.h"
#include "param.h"
#include "riscv.h"

#define INITIAL_BOTTOM_ADDR ((MAXVA) - (2*PGSIZE))

// Virtual Memory Area
struct vma {
  uint64 addr[MAXVMA];
  uint64 length[MAXVMA];
  uint64 end_addr[MAXVMA];
  int prot[MAXVMA];
  int flags[MAXVMA];
  int fd[MAXVMA];
  int offset[MAXVMA];
  struct file * file[MAXVMA];
  uint64 bottom_addr;
};

#endif // _VMA_H_
