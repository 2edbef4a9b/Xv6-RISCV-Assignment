// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

#define STEAL_THRESHOLD 16
#define STEAL_AMOUNT 4

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
  int count;
} kmem[NCPU];

void
kinit()
{
  char name[16];
  for(int i = 0; i < NCPU; i++){
    snprintf(name, sizeof(name), "kmem_%d", i);
    initlock(&kmem[i].lock, name);
    kmem[i].freelist = 0;
    kmem[i].count = 0;
  }
  freerange(end, (void*)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  int cpu;
  p = (char*)PGROUNDUP((uint64)pa_start);

  push_off();
  cpu = cpuid();
  pop_off();

  if(cpu < 0 || cpu >= NCPU)
    panic("freerange: invalid cpu id");

  acquire(&kmem[cpu].lock);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE) {
    struct run *r = (struct run*)p;
    if(((uint64)r % PGSIZE) != 0 || (char*)r < end || (uint64)r >= PHYSTOP)
      panic("freerange");

    r->next = kmem[cpu].freelist;
    kmem[cpu].freelist = r;
    kmem[cpu].count++;
  }
  release(&kmem[cpu].lock);
}

// Free the page of physical memory pointed at by pa,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;
  int cpu;

  push_off();
  cpu = cpuid();
  pop_off();
  if(cpu < 0 || cpu >= NCPU)
    panic("kfree: invalid cpu id");

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  acquire(&kmem[cpu].lock);
  r->next = kmem[cpu].freelist;
  kmem[cpu].freelist = r;
  kmem[cpu].count++;
  release(&kmem[cpu].lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;
  int cpu;

  push_off();
  cpu = cpuid();
  pop_off();

  acquire(&kmem[cpu].lock);

  // Check if we need to steal memory from another CPU.
  if(kmem[cpu].count < STEAL_THRESHOLD){
    int stolen = ksteal(cpu);
    if(stolen == 0 && kmem[cpu].count == 0){
      // If no memory was stolen and current CPU has no pages,
      // we cannot allocate any more pages.
      printf("kalloc: CPU %d has no free pages\n", cpu);
      release(&kmem[cpu].lock);
      return 0;
    }
  }

  r = kmem[cpu].freelist;
  if(r){
    kmem[cpu].freelist = r->next;
    kmem[cpu].count--;
  }
  release(&kmem[cpu].lock);

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}

// Steal memory from another CPU's freelist.
// Returns the number of pages stolen.
// Returns 0 if no pages were stolen.
int
ksteal(int stealer)
{
  struct run *curr, *next;
  int cpu, count;

  count = 0;

  for(cpu = 0; cpu < NCPU; cpu++) {
    if(cpu == stealer)
      continue;

    acquire(&kmem[cpu].lock);

    curr = kmem[cpu].freelist;
    while(curr && count < STEAL_AMOUNT){
      next = curr->next;
      // Remove the page from the freelist.
      kmem[cpu].freelist = next;
      kmem[cpu].count--;
      
      // Add the page to the current CPU's freelist.
      curr->next = kmem[stealer].freelist;
      kmem[stealer].freelist = curr;
      kmem[stealer].count++;
      count++;
      curr = next;
    }
    release(&kmem[cpu].lock);

    if(count >= STEAL_AMOUNT)
      break; // Stop stealing if we reached the desired amount.
  }

  // printf("ksteal: CPU %d stole %d pages from Other CPUs\n", stealer, count);
  return count;
}
