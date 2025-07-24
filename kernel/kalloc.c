// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

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
    snprintf(name, sizeof(name), "kmem%d", i);
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
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE){
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

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  push_off();
  cpu = cpuid();
  pop_off();

  if(cpu < 0 || cpu >= NCPU)
    panic("kfree: invalid cpu id");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  // Add the page to the end of the freelist for this CPU.
  acquire(&kmem[cpu].lock);
  r->next = kmem[cpu].freelist;
  kmem[cpu].freelist = r;
  kmem[cpu].count++;
  release(&kmem[cpu].lock);
}

// Steal memory from another CPU's freelist.
// Returns the number of pages stolen.
// Returns 0 if no pages were stolen.
int
ksteal(int stealer)
{
  struct run *start, *end;  // Start and end of stolen freelist.
  int cpu, total_stolen, stolen;

  total_stolen = 0;

  for(cpu = (stealer + 1) % NCPU; cpu != stealer; cpu = (cpu + 1) % NCPU){
    acquire(&kmem[cpu].lock);
    start = kmem[cpu].freelist;
    end = start;
    if(!start){
      release(&kmem[cpu].lock);
      continue; // No pages to steal from this CPU.
    }

    // Find the end of the freelist to steal.
    stolen = 1;
    while(end->next && stolen + total_stolen < STEAL_AMOUNT){
      stolen++;
      end = end->next;
    }

    // Remove the stolen pages from the original CPU's freelist.
    kmem[cpu].freelist = end->next;
    kmem[cpu].count -= stolen;
    release(&kmem[cpu].lock);

    total_stolen += stolen;

    // Add the stolen pages to the stealer's freelist.
    acquire(&kmem[stealer].lock);
    if(!kmem[stealer].freelist){
      // Set the stealer's freelist to the stolen freelist.
      kmem[stealer].freelist = start;
      end->next = 0;
    } else {
      // Link the stolen pages to the end of the stealer's freelist.
      end->next = kmem[stealer].freelist;
      kmem[stealer].freelist = start;
    }
    kmem[stealer].count += stolen;
    release(&kmem[stealer].lock);

    if(total_stolen >= STEAL_AMOUNT)
      break; // Enough pages stolen.
  }

  return total_stolen;
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
  r = kmem[cpu].freelist;

  if(!r){
    // No free pages available, try to steal from another CPU.
    release(&kmem[cpu].lock);

    if(!ksteal(cpu))
      return 0; // No pages available after stealing.
    acquire(&kmem[cpu].lock);
  }

  // Remove the page from the freelist.
  r = kmem[cpu].freelist;
  kmem[cpu].freelist = r->next;
  kmem[cpu].count--;

  release(&kmem[cpu].lock); 
  memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}
