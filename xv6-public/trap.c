#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "x86.h"
#include "traps.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "file.h"

static const int prio_to_weight[40] = {
  /* 0 */  88761, 71755, 56483, 46273, 36291,
  /* 5 */  29154, 23254, 18705, 14949, 11916,
  /* 10 */  9548,  7620,  6100,  4904,  3906,
  /* 15 */  3121,  2501,  1991,  1586,  1277,
  /* 20 */  1024,   820,   655,   526,   423,
  /* 25 */   335,   272,   215,   172,   137,
  /* 30 */   110,    87,    70,    56,    45,
  /* 35 */    36,    29,    23,    18,    15,
};

extern struct mmap_area mmap_areas[64];

int pgfault_handler(struct trapframe*);

// Interrupt descriptor table (shared by all CPUs).
struct gatedesc idt[256];
extern uint vectors[];  // in vectors.S: array of 256 entry pointers
struct spinlock tickslock;
uint ticks;

void
tvinit(void)
{
  int i;

  for(i = 0; i < 256; i++)
    SETGATE(idt[i], 0, SEG_KCODE<<3, vectors[i], 0);
  SETGATE(idt[T_SYSCALL], 1, SEG_KCODE<<3, vectors[T_SYSCALL], DPL_USER);

  initlock(&tickslock, "time");
}

void
idtinit(void)
{
  lidt(idt, sizeof(idt));
}

//PAGEBREAK: 41
void
trap(struct trapframe *tf)
{
  if(tf->trapno == T_SYSCALL){
    if(myproc()->killed)
      exit();
    myproc()->tf = tf;
    syscall();
    if(myproc()->killed)
      exit();
    return;
  }

  switch(tf->trapno){
  case T_IRQ0 + IRQ_TIMER:
    if(cpuid() == 0){
      acquire(&tickslock);
      ticks++;
      wakeup(&ticks);
      release(&tickslock);
    }
    lapiceoi();
    break;
  case T_IRQ0 + IRQ_IDE:
    ideintr();
    lapiceoi();
    break;
  case T_IRQ0 + IRQ_IDE+1:
    // Bochs generates spurious IDE1 interrupts.
    break;
  case T_IRQ0 + IRQ_KBD:
    kbdintr();
    lapiceoi();
    break;
  case T_IRQ0 + IRQ_COM1:
    uartintr();
    lapiceoi();
    break;
  case T_IRQ0 + 7:
  case T_IRQ0 + IRQ_SPURIOUS:
    cprintf("cpu%d: spurious interrupt at %x:%x\n",
            cpuid(), tf->cs, tf->eip);
    lapiceoi();
    break;
  case T_PGFLT:
  if (myproc() == 0 || (tf->cs&3) == 0) {
    // In kernel, it must be our mistake.
    cprintf("unexpected page fault from cpu %d eip %x (cr2=0x%x)\n",
            cpuid(), tf->eip, rcr2());
    if(pgfault_handler(tf) < 0){ // page fault handling failed
      panic("page fault");
    }
  }
  break;

  //PAGEBREAK: 13
  default:
    if(myproc() == 0 || (tf->cs&3) == 0){
      // In kernel, it must be our mistake.
      cprintf("unexpected trap %d from cpu %d eip %x (cr2=0x%x)\n",
              tf->trapno, cpuid(), tf->eip, rcr2());
      panic("trap");
    }
    // In user space, assume process misbehaved.
    cprintf("pid %d %s: trap %d err %d on cpu %d "
            "eip 0x%x addr 0x%x--kill proc\n",
            myproc()->pid, myproc()->name, tf->trapno,
            tf->err, cpuid(), tf->eip, rcr2());
    myproc()->killed = 1;
  }

  // Force process exit if it has been killed and is in user space.
  // (If it is still executing in the kernel, let it keep running
  // until it gets to the regular system call return.)
  if(myproc() && myproc()->killed && (tf->cs&3) == DPL_USER)
    exit();

  // Force process to give up CPU on clock tick.
  // If interrupts were on while locks held, would need to check nlock.
  if(myproc() && myproc()->state == RUNNING && tf->trapno == T_IRQ0+IRQ_TIMER){
    myproc()->runtime += 1000;
    myproc()->weight = prio_to_weight[myproc()->nice];
    myproc()->vruntime = myproc()->runtime * 1024 / myproc()->weight;
    myproc()->current_tick += 1000;
    if(myproc()->current_tick >= myproc()->time_slice)
      yield();
  }

  // Check if the process has been killed since we yielded
  if(myproc() && myproc()->killed && (tf->cs&3) == DPL_USER)
    exit();
}

// Page fault handler
int
pgfault_handler(struct trapframe *tf)
{
  int i;
  uint fault_va = rcr2(); // get fault address
  // Determine whether the access was a read or a write
  int read = (tf->err & 2) == 0;
  int write = (tf->err & 2) == 1;

  for(i=0;i<64;i++){
    // Find 
    if(mmap_areas[i].addr == fault_va){
      if((mmap_areas[i].prot & PROT_READ) != read || (mmap_areas[i].prot & PROT_WRITE) != write)
        break;
      // Do some action for page according to faulted address
      // 1. Allocate new physical page
      char *pa = kalloc();
      if (pa == 0) { // memory cannot be allocated
        cprintf("memory cannot be allocated\n");
        return -1;
      }
      // 2. Fill new page with 0
      memset(pa, 0, PGSIZE);

      // 3. If it is file mapping, read file into physical page with offset
      if(!(mmap_areas[i].flags & MAP_ANONYMOUS)){
        struct file *f = mmap_areas[i].f;
        f->off = mmap_areas[i].offset;
        if(fileread(f, pa, PGSIZE) < 0){
          cprintf("file read failed\n");
          return -1;
        }
        // map page & fill it properly
        if(pgfault_mappages(mmap_areas[i].p->pgdir, (char*)fault_va, PGSIZE, pa, mmap_areas[i].prot) < 0){
          cprintf("failed to map page\n"); // failed to map page
          return -1;
        }
      }
      return 0; // success
    }
  }
  return -1; // cannot find corresponding mmap_area
}