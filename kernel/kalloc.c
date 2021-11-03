// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

int ref_count[PHYSTOP/PGSIZE]; //Number of references to a physical page.

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

void
kinit()
{
  initlock(&kmem.lock, "kmem");
  freerange(end, (void*)PHYSTOP);
}

void freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char *)PGROUNDUP((uint64)pa_start);
  for (; p + PGSIZE <= (char *)pa_end; p += PGSIZE)
  {
    // Initialise number of references to all pages as 1.
    ref_count[(uint64)p/PGSIZE] = 1;
    kfree(p);
  }
}

void increment_page_ref(uint64 pa)
{ 
  acquire(&kmem.lock);
  int page_number = pa/PGSIZE;
  if(pa>PHYSTOP || ref_count[page_number]<1){
    panic("increment_page_ref: invalid reference count");
  }
  ref_count[page_number] += 1;
  release(&kmem.lock);
}

// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;
  int page_number;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

	// Decraese the reference count to a page when kfree() is called 
  acquire(&kmem.lock);
  page_number = (uint64)pa/PGSIZE;
  if(ref_count[page_number]<1){
    panic("kfree: page should have a reference to it before freeing");
  }
  ref_count[page_number] -= 1;
  int current_ref_count = ref_count[page_number];
  release(&kmem.lock);

  if (current_ref_count > 0)
    return;

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  acquire(&kmem.lock);
  r->next = kmem.freelist;
  kmem.freelist = r;
  release(&kmem.lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;
  int page_number;

  acquire(&kmem.lock);
  r = kmem.freelist;
  if(r){
    page_number = (uint64)r/PGSIZE;
    if(ref_count[page_number]!=0){
      panic("kalloc: Newly allocated page cannot have any initial reference to it");
    }
    // set to 1.
    ref_count[page_number] = 1;
    kmem.freelist = r->next;
  }
  release(&kmem.lock);

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}
