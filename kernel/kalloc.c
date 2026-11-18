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

// Número de usos de cada página física entre KERNBASE y PHYSTOP: cada
// tabla de páginas que la mapea cuenta uno, y el kernel cuenta uno por
// las páginas que usa directamente. Con Copy-on-Write una página puede
// estar en varias tablas a la vez, y solo vuelve a la lista libre cuando
// el último uso la suelta. fork(), exit() y los page faults pueden tocar
// el mismo contador desde CPUs distintas, por eso lo protege un lock.
struct {
  struct spinlock lock;
  int refs[(PHYSTOP - KERNBASE) / PGSIZE];
} kref;

#define PAGINA(pa) (((uint64)(pa) - KERNBASE) / PGSIZE)

static void
validar(void *pa, char *quien)
{
  if (((uint64)pa % PGSIZE) != 0 || (char *)pa < end || (uint64)pa >= PHYSTOP)
    panic(quien);
}

void
kinit()
{
  initlock(&kmem.lock, "kmem");
  initlock(&kref.lock, "kref");
  freerange(end, (void *)PHYSTOP);
}

// Las páginas iniciales nunca pasaron por kalloc(): se les da un uso
// para que el kfree() que las lleva a la lista libre lo deje en 0.
void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char *)PGROUNDUP((uint64)pa_start);
  for (; p + PGSIZE <= (char *)pa_end; p += PGSIZE) {
    kref.refs[PAGINA(p)] = 1;
    kfree(p);
  }
}

// Suelta un uso de la página pa. La página vuelve a la lista libre solo
// cuando nadie más la usa.
void
kfree(void *pa)
{
  struct run *r;

  validar(pa, "kfree");

  acquire(&kref.lock);
  if (kref.refs[PAGINA(pa)] < 1)
    panic("kfree: page not in use");
  int quedan = --kref.refs[PAGINA(pa)];
  release(&kref.lock);
  if (quedan > 0)
    return;

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run *)pa;

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

  acquire(&kmem.lock);
  r = kmem.freelist;
  if (r)
    kmem.freelist = r->next;
  release(&kmem.lock);

  if (r) {
    acquire(&kref.lock);
    kref.refs[PAGINA(r)] = 1;
    release(&kref.lock);
    memset((char *)r, 5, PGSIZE); // fill with junk
  }
  return (void *)r;
}

// Agrega un uso a una página que ya está en uso (fork() con CoW).
void
krefinc(void *pa)
{
  validar(pa, "krefinc");
  acquire(&kref.lock);
  if (kref.refs[PAGINA(pa)] < 1)
    panic("krefinc: page not in use");
  kref.refs[PAGINA(pa)]++;
  release(&kref.lock);
}

int
krefcount(void *pa)
{
  validar(pa, "krefcount");
  acquire(&kref.lock);
  int n = kref.refs[PAGINA(pa)];
  release(&kref.lock);
  return n;
}

uint64
kfreepages(void)
{
  uint64 n = 0;
  acquire(&kmem.lock);
  for (struct run *r = kmem.freelist; r; r = r->next)
    n++;
  release(&kmem.lock);
  return n;
}

uint64
ksharedpages(void)
{
  uint64 n = 0;
  acquire(&kref.lock);
  for (int i = 0; i < NELEM(kref.refs); i++)
    if (kref.refs[i] > 1)
      n++;
  release(&kref.lock);
  return n;
}
