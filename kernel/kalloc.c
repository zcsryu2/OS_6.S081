// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
} kmem;

struct {
  struct spinlock lock;
  int count;
}ref[PHYSTOP / PGSIZE];

void
kinit()
{
  for(int i = 0; i < PHYSTOP/PGSIZE; ++i){
    initlock(&(ref[i].lock), "kmem_ref");
  }
  initlock(&kmem.lock, "kmem");
  freerange(end, (void*)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE){
    // init reference count of every page 
    ref[(uint64)p / PGSIZE].count = 1;  
    kfree(p);
  }
}

// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  uint64 page_index = (uint64)pa / PGSIZE;
  acquire(&ref[page_index].lock);
  ref[page_index].count--;
  // release memory only when reference count is 0
  if(ref[page_index].count == 0){
    release(&ref[page_index].lock);

    // Fill with junk to catch dangling refs.
    memset(pa, 1, PGSIZE);

    r = (struct run*)pa;

    acquire(&kmem.lock);
    r->next = kmem.freelist;
    kmem.freelist = r;
    release(&kmem.lock);
  }
  else{
    release(&ref[page_index].lock);
  }
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
  if(r){
    kmem.freelist = r->next;
    // init the ref of new page
    acquire(&ref[(uint64)r/ PGSIZE].lock);
    ref[(uint64)r / PGSIZE].count = 1;
    release(&ref[(uint64)r / PGSIZE].lock);
  }
  release(&kmem.lock);

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}


void ref_acquire(uint64 pa){
  acquire(&ref[pa / PGSIZE].lock);
}

void ref_release(uint64 pa){
  release(&ref[pa / PGSIZE].lock);
}

int ref_getcnt(uint64 pa){
  return ref[pa / PGSIZE].count;
}

void ref_addcnt(uint64 pa, int n){
  acquire(&ref[pa / PGSIZE].lock);
  ref[pa / PGSIZE].count += n;
  release(&ref[pa / PGSIZE].lock);
}
