// Buffer cache.
//
// The buffer cache is a linked list of buf structures holding
// cached copies of disk block contents.  Caching disk blocks
// in memory reduces the number of disk reads and also provides
// a synchronization point for disk blocks used by multiple processes.
//
// Interface:
// * To get a buffer for a particular disk block, call bread.
// * After changing buffer data, call bwrite to write it to disk.
// * When done with the buffer, call brelse.
// * Do not use the buffer after calling brelse.
// * Only one process at a time can use a buffer,
//     so do not keep them longer than necessary.


#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "buf.h"

#define NBUCKETS 37 

struct bucket {
  uint dev;
  uint blockno;
  uint bufidx;
  struct bucket *next;
};

struct {
  struct spinlock lock;
  struct buf buf[NBUF];
  struct bucket buckets[NBUCKETS];
  uint32 freelist; // Bitmask for free buffers.
} bcache;

static uint
hash(uint dev, uint blockno)
{
  return (blockno ^ (dev << 5)) % NBUCKETS;
}

// Operations on the hash table for buffer cache.
int
find(uint dev, uint blockno)
{
  uint hashval;
  struct bucket *bucket;

  hashval = hash(dev, blockno);
  bucket = &bcache.buckets[hashval];

  while(bucket->next){
    if(bucket->dev == dev && bucket->blockno == blockno)
      return bucket->bufidx;
    bucket = bucket->next;
  }
  return -1; // Not found.
}

int
insert(uint dev, uint blockno, int bufidx)
{
  uint hashval;
  struct bucket *bucket;

  hashval = hash(dev, blockno);
  bucket = &bcache.buckets[hashval];

  // Check if the bucket already exists.
  while(bucket->next){
    if(bucket->dev == dev && bucket->blockno == blockno)
      return -1; // Already exists.
    bucket = bucket->next;
  }

  // Insert new bucket at the end of the linked list.
  bucket->next = (struct bucket*)kalloc();
  if(!bucket->next)
    return -1; // Allocation failed.

  bucket = bucket->next;
  bucket->dev = dev;
  bucket->blockno = blockno;
  bucket->bufidx = bufidx;
  bucket->next = 0;

  return 0; // Success.
}

int
erase(uint dev, uint blockno)
{
  uint hashval;
  struct bucket *bucket, *to_free;

  hashval = hash(dev, blockno);
  bucket = &bcache.buckets[hashval];

  // Find and remove the bucket from the linked list.
  while(bucket->next){
    if(bucket->next->dev == dev && bucket->next->blockno == blockno){
      to_free = bucket->next;
      bucket->next = bucket->next->next;
      kfree(to_free);
      return 0; // Success.
    }
    bucket = bucket->next;
  }
  return -1; // Not found.
}

// Operations on the freelist bitmap.
int
isfull(void)
{
  return bcache.freelist == ((1U << NBUF) - 1); // All buffers are used.
}

int
isfree(int bufidx)
{
  if(bufidx < 0 || bufidx >= NBUF)
    return 0; // Invalid index.
  return (bcache.freelist & (1U << bufidx)) == 0;
}

int
allocbuf(void)
{
  int bufidx;

  if(isfull())
    return -1; // No free buffers.


  // Find the first free buffer.
  // uint32 inverted = ~(bcache.freelist);
  // bufidx = __builtin_ctz(inverted);
  // if(bufidx >= NBUF)
  //   return -1;
  for(bufidx = 0; bufidx < NBUF; bufidx++){
    if(~bcache.freelist & (1U << bufidx))
      break;
  }
  if(bufidx >= NBUF)
    return -1;

  bcache.freelist |= (1U << bufidx);
  return bufidx;
}

void
binit(void)
{
  initlock(&bcache.lock, "bcache");

  // Initialize all buffers.
  char name[16];
  for(int i = 0; i < NBUF; i++){
    snprintf(name, sizeof(name), "bcache%d", i);
    initsleeplock(&bcache.buf[i].lock, name);
  }
  bcache.freelist = 0;
  for(int i = 0; i < NBUCKETS; i++){
    bcache.buckets[i].next = 0;
    bcache.buckets[i].dev = 0;
    bcache.buckets[i].blockno = 0;
    bcache.buckets[i].bufidx = -1;
  }
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  int bufidx;
  struct buf *buf;

  acquire(&bcache.lock);

  // Is the block already cached?
  bufidx = find(dev, blockno);
  if(bufidx >= 0) {
    buf = &bcache.buf[bufidx];
    buf->refcnt++;
    release(&bcache.lock);
    acquiresleep(&buf->lock);
    return buf;
  }
 
  // Not cached, do we have a free buffer?
  bufidx = allocbuf();
  if(bufidx >= 0){
    buf = &bcache.buf[bufidx];
    buf->dev = dev;
    buf->blockno = blockno;
    buf->valid = 0;
    buf->refcnt = 1;
    insert(dev, blockno, bufidx);
    release(&bcache.lock);
    acquiresleep(&buf->lock);
    return buf;
  }

  // Not free buffer, find one to evict.
  for(bufidx = 0; bufidx < NBUF; bufidx++){
    buf = &bcache.buf[bufidx];
    if(buf->refcnt == 0){ 
      // Found a free buffer.
      printf("bget: evicting buffer %d for dev %d block %d\n", bufidx, dev, blockno);
      erase(dev, blockno); // Remove any existing entry.
      insert(dev, blockno, bufidx);

      buf->dev = dev;
      buf->blockno = blockno;
      buf->valid = 0;
      buf->refcnt = 1;
      release(&bcache.lock);
      acquiresleep(&buf->lock);
      return buf;
    } 
  }
  panic("bget: no buffers");
}

// Return a locked buf with the contents of the indicated block.
struct buf*
bread(uint dev, uint blockno)
{
  struct buf *b;

  b = bget(dev, blockno);
  if(!b->valid) {
    virtio_disk_rw(b, 0);
    b->valid = 1;
  }
  return b;
}

// Write b's contents to disk.  Must be locked.
void
bwrite(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("bwrite");
  virtio_disk_rw(b, 1);
}

// Release a locked buffer.
void
brelse(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);

  acquire(&bcache.lock);
  b->refcnt--;
  release(&bcache.lock);
}

void
bpin(struct buf *b) {
  acquire(&bcache.lock);
  b->refcnt++;
  release(&bcache.lock);
}

void
bunpin(struct buf *b) {
  acquire(&bcache.lock);
  b->refcnt--;
  release(&bcache.lock);
}


