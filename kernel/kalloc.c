// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

void
freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
} kmem;

void
kinit()
{
  initlock(&kmem.lock, "kmem");
  freerange(end, (void *)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char *)PGROUNDUP((uint64)pa_start);
  for (; p + PGSIZE <= (char *)pa_end; p += PGSIZE)
    kfree(p);
}

// Free the page of physical memory pointed at by pa,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;

  if (((uint64)pa % PGSIZE) != 0 || (char *)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run *)pa;

  acquire(&kmem.lock);
  r->next = kmem.freelist;
  kmem.freelist = r;
  release(&kmem.lock);
}

// Free the 2-megabyte page of physical memory pointed at by pa,
// which normally should have been returned by a call to kalloc().
void
superfree(void *pa)
{
  printf("superfree: freeing 2MB page at %p\n", pa);
  struct run *curr, *next_page;

  if (((uint64)pa % (SUPERPGSIZE)) != 0 || (char *)pa < end || (uint64)pa >= PHYSTOP)
    panic("superfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, SUPERPGSIZE);

  curr = (struct run *)pa;

  acquire(&kmem.lock);

  // Traverse the pages to set the next pointers correctly.
  for (int i = 511; i >= 0; i--) {
    next_page = (struct run *)(curr + i * PGSIZE);
    next_page->next = kmem.freelist;
    kmem.freelist = next_page;
  }

  release(&kmem.lock);
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
  if (r)
    kmem.freelist = r->next;
  release(&kmem.lock);

  if (r)
    memset((char *)r, 5, PGSIZE); // fill with junk
  return (void *)r;
}

// Allocate one 2-megabyte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
superalloc(void)
{
  printf("superalloc: allocating 2MB page\n");
  struct run *curr, *prev, *start, *end;

  acquire(&kmem.lock);

  // Traverse the freelist to find a free 2MB page.
  prev = start = end = (void *)0;
  for (curr = kmem.freelist; curr; curr = curr->next) {
    if (((uint64)curr & ((1 << 21) - 1)) == 0) {
      start = curr;
      for (int i = 0; i < 512; i++) {
        if (!(curr->next == curr + PGSIZE)) {
          // The next page is not contiguous.
          break;
        }
        curr = curr->next;
      }
      if (curr - start == SUPERPGSIZE) {
        end = curr;
        break; // Found a contiguous block of 512 pages (2MB).
      }
    }
    prev = curr;
  }

  if (end) {
    // Remove the found block from the freelist.
    if (prev) {
      prev->next = end->next;
    } else {
      kmem.freelist = end->next;
    }
  }

  release(&kmem.lock);

  if (end) {
    // Fill with junk to catch dangling refs.
    memset((char *)start, 5, SUPERPGSIZE);
  }

  return (void *)start;
}
