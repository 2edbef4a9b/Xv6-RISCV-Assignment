// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

#define SUPERPGCOUNTS ((PHYSTOP - KERNBASE) / SUPERPGSIZE)
#define USAGEIDX(pa) (((uint64)(pa) - KERNBASE) / SUPERPGSIZE)

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

// Free page count for each super page.
struct {
  uint count;
  struct spinlock lock;
} freecount[SUPERPGCOUNTS];

// Double linked list.
struct run {
  struct run *prev, *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
  struct run *superlist;
} kmem;

void
kinit()
{
  initlock(&kmem.lock, "kmem");
  freerange(end, (void*)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  struct run *r;

  p = (char*)PGROUNDUP((uint64)pa_start);

  // Add normal pages to the freelist until we reach the super page boundary.
  for(; p + PGSIZE <= (char*)SUPERPGROUNDUP((uint64)pa_end); p += PGSIZE){
    // Fill with junk to catch dangling refs.
    memset(p, 1, PGSIZE);
    r = (struct run*)p;
    acquire(&kmem.lock);
    if(kmem.freelist)
      kmem.freelist->prev = r;
    r->next = kmem.freelist;
    r->prev = 0;
    kmem.freelist = r;
    release(&kmem.lock);

    // Update free page count for this super page.
    acquire(&freecount[USAGEIDX((uint64)p)].lock);
    freecount[USAGEIDX((uint64)p)].count++;
    release(&freecount[USAGEIDX((uint64)p)].lock);
  }

  // Add super pages to the superlist.
  for(; p + SUPERPGSIZE <= (char*)pa_end; p += SUPERPGSIZE){
    // Fill with junk to catch dangling refs.
    memset(p, 1, SUPERPGSIZE);
    r = (struct run*)p;
    acquire(&kmem.lock);
    if(kmem.superlist)
      kmem.superlist->prev = r;
    r->next = kmem.superlist;
    r->prev = 0;
    kmem.superlist = r;
    release(&kmem.lock);

    // Update free page count for this super page.
    acquire(&freecount[USAGEIDX((uint64)p)].lock);
    freecount[USAGEIDX((uint64)p)].count = 512;
    release(&freecount[USAGEIDX((uint64)p)].lock);
  }

  // Add remaining normal pages to the freelist.
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE){
    // Fill with junk to catch dangling refs.
    memset(p, 1, PGSIZE);
    r = (struct run*)p;
    acquire(&kmem.lock);
    if(kmem.freelist)
      kmem.freelist->prev = r;
    r->next = kmem.freelist;
    r->prev = 0;
    kmem.freelist = r;
    release(&kmem.lock);

    // Update free page count for this super page.
    acquire(&freecount[USAGEIDX((uint64)p)].lock);
    freecount[USAGEIDX((uint64)p)].count++;
    release(&freecount[USAGEIDX((uint64)p)].lock);
  }
}

// Split a super page into normal pages.
void
ksplit()
{
  struct run *r, *new;
  uint64 pa;

  r = kmem.superlist;
  if(!r)
    return; // No super page to split.

  // Remove the super page from the superlist.
  kmem.superlist = r->next;
  if(kmem.superlist)
    kmem.superlist->prev = 0;

  // Add the normal pages to the freelist.
  for(pa = (uint64)r; pa < (uint64)r + SUPERPGSIZE; pa += PGSIZE){
    new = (struct run*)pa;

    if(kmem.freelist)
      kmem.freelist->prev = new;
    new->next = kmem.freelist;
    new->prev = 0;
    kmem.freelist = new;
  }
}

// Merge normal pages into a super page that contains pa.
void
kmerge(uint64 pa)
{
  struct run *r;
  uint64 start;

  start = SUPERPGROUNDDOWN(pa);
  acquire(&freecount[USAGEIDX(pa)].lock);
  if(freecount[USAGEIDX(pa)].count != 512){
    release(&freecount[USAGEIDX(pa)].lock);
    return; // Not enough free pages to merge.
  }
  release(&freecount[USAGEIDX(pa)].lock);

  acquire(&kmem.lock);
  for(pa = start; pa < start + SUPERPGSIZE; pa += PGSIZE){
    r = (struct run*)pa;
    if(r->prev)
      r->prev->next = r->next;
    else
      kmem.freelist = r->next;
    if(r->next)
      r->next->prev = r->prev;
  }

  // Add the super page to the superlist.
  r = (struct run*)start;
  if(kmem.superlist)
    kmem.superlist->prev = r;
  r->next = kmem.superlist;
  r->prev = 0;
  kmem.superlist = r;
  release(&kmem.lock);
}

// Free the page of physical memory pointed at by pa,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  acquire(&kmem.lock);
  if(kmem.freelist)
    kmem.freelist->prev = r;
  r->next = kmem.freelist;
  r->prev = 0;
  kmem.freelist = r;
  release(&kmem.lock);

  // Update free page count for this super page.
  acquire(&freecount[USAGEIDX((uint64)pa)].lock);
  freecount[USAGEIDX((uint64)pa)].count++;
  release(&freecount[USAGEIDX((uint64)pa)].lock);

  // Try merging with the super page.
  kmerge((uint64)pa);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;

  acquire(&kmem.lock);
  r = kmem.freelist;

  // Try splitting.
  if(!r){
    ksplit();
    r = kmem.freelist;
  }

  if(r){
    kmem.freelist = r->next;
    if(kmem.freelist)
      kmem.freelist->prev = 0;
  }
  release(&kmem.lock);

  if(r){
    acquire(&freecount[USAGEIDX((uint64)r)].lock);
    freecount[USAGEIDX((uint64)r)].count--;
    release(&freecount[USAGEIDX((uint64)r)].lock);
  }

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}
