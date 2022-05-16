#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"

struct cpu cpus[NCPU];

struct proc proc[NPROC];

struct proc *initproc;

int nextpid = 1;
struct spinlock pid_lock;

extern void forkret(void);
static void freeproc(struct proc *p);
extern uint64 cas(volatile void *addr, int expected, int newval);

int remove_all_sleeping_on_channel(void* channel);

extern char trampoline[]; // trampoline.S

// helps ensure that wakeups of wait()ing
// parents are not lost. helps obey the
// memory model when using p->parent.
// must be acquired before any p->lock.
struct spinlock wait_lock;

// our code:
struct proc_ll sleeping_procs;
struct proc_ll zombie_procs;
struct proc_ll unused_entrys; 

int get_the_least_used_cpu_and_increase_its_counter(){
  struct cpu* least_used_cpu = cpus;
  uint64 cur_least = least_used_cpu->proc_counter;

  for (struct cpu* c = cpus; c < &cpus[NCPU]; c++){
    if (c->proc_counter == 0){
      continue;
    }
    if (cur_least == -1 || c->proc_counter < cur_least){
      least_used_cpu = c;
      cur_least = c->proc_counter;
    }
  }
  uint64 prev;
  do{
    prev = least_used_cpu->proc_counter;
  }while(cas(&least_used_cpu->proc_counter, prev, prev + 1));

  return least_used_cpu - cpus;
}

// Allocate a page for each process's kernel stack.
// Map it high in memory, followed by an invalid
// guard page.
void
proc_mapstacks(pagetable_t kpgtbl) {
  struct proc *p;
  
  for(p = proc; p < &proc[NPROC]; p++) {
    char *pa = kalloc();
    if(pa == 0)
      panic("kalloc");
    uint64 va = KSTACK((int) (p - proc));
    kvmmap(kpgtbl, va, (uint64)pa, PGSIZE, PTE_R | PTE_W);
  }
}

void
initlist(struct proc_ll* lst, int index){
  lst->head = (index + 1) * -1;
  lst->tail = (index + 1) * -1;
  initlock(&lst->h_lock, "h_lock");
  initlock(&lst->t_lock, "t_lock");
}

int
printq(struct proc_ll* queue){
  int cur = queue->head;
  while (cur >= 0){
    printf("%d->", cur);
    cur = proc[cur].nextNodeIndex;
  }
  printf("\n");
  return 0;
}

// initialize the proc table at boot time.
void
procinit(void)
{
  struct proc *p;
  
  initlock(&pid_lock, "nextpid");
  initlock(&wait_lock, "wait_lock");

  initlist(&unused_entrys, UNUSED);
  initlist(&sleeping_procs, SLEEPING);
  initlist(&zombie_procs, ZOMBIE);
  // printf("%d\n", unused_entrys.head);
  for(p = proc; p < &proc[NPROC]; p++) {
      initlock(&p->lock, "proc");
      // char pname = (int)(p - proc) + '0';
      // char lock_name[17] = {'p','r','o','c','_','n','o','d','e','_','l','o','c','k',' ', pname, 0};
      // char* lock_name = "proc_node_lock ", pname;
      initlock(&p->node_lock, "proc_node_lock");
      p->kstack = KSTACK((int) (p - proc));
      
      // printf("0-%d\n", (p - proc) / sizeof(struct proc));
      // printf("1-%d\n", (p - proc));
      // printf("1.5-%d\n", p);
      // printf("2-%d\n", sizeof(struct proc));
      // printf("\n");
      enque(&unused_entrys, p - proc);
  }
  // printf("%d\n", unused_entrys.head);
  // printq(&unused_entrys);

  // printf("dec %d\n", remove(&unused_entrys, 42));
  // printq(&unused_entrys);

  struct cpu *c;
  for(c = cpus; c < &cpus[NCPU]; c++){
    initlist(&c->cpu_runnables, 10 + (c - cpus));
  }
}

// Must be called with interrupts disabled,
// to prevent race with process being moved
// to a different CPU.
int
cpuid()
{
  int id = r_tp();
  return id;
}

// Return this CPU's cpu struct.
// Interrupts must be disabled.
struct cpu*
mycpu(void) {
  int id = cpuid();
  struct cpu *c = &cpus[id];
  return c;
}

// Return the current struct proc *, or zero if none.
struct proc*
myproc(void) {
  push_off();
  struct cpu *c = mycpu();
  struct proc *p = c->proc;
  pop_off();
  return p;
}

// int
// allocpid() {
//   int pid;
  
//   acquire(&pid_lock);
//   pid = nextpid;
//   nextpid = nextpid + 1;
//   release(&pid_lock);

//   return pid;
// }

// int
// allocpid() {
//   int pid;
//   pid = nextpid;
//   for (int i = 0; i < 50000; i++);
//   nextpid++;
//   return pid;
// }

int
allocpid() {
  int pid;
  do{
    pid = nextpid;
  } while (cas(&nextpid, pid, pid+1));
  
  return pid;
}

// Look in the process table for an UNUSED proc.
// If found, initialize state required to run in the kernel,
// and return with p->lock held.
// If there are no free procs, or a memory allocation fails, return 0.
static struct proc*
allocproc(void)
{
  struct proc *p;
  int proc_table_unused_idx;
  if ((proc_table_unused_idx = deque(&unused_entrys)) < 0){
    return 0;
  }
  p = &proc[proc_table_unused_idx];
  acquire(&p->lock);

//   for(p = proc; p < &proc[NPROC]; p++) {
//     acquire(&p->lock);
//     if(p->state == UNUSED) {
//       goto found;
//     } else {
//       release(&p->lock);
//     }
//   }
//   return 0;

// found:
  p->pid = allocpid();
  p->state = USED;

  // Allocate a trapframe page.
  if((p->trapframe = (struct trapframe *)kalloc()) == 0){
    freeproc(p);
    release(&p->lock);
    return 0;
  }

  // An empty user page table.
  p->pagetable = proc_pagetable(p);
  if(p->pagetable == 0){
    freeproc(p);
    release(&p->lock);
    return 0;
  }

  // Set up new context to start executing at forkret,
  // which returns to user space.
  memset(&p->context, 0, sizeof(p->context));
  p->context.ra = (uint64)forkret;
  p->context.sp = p->kstack + PGSIZE;

  return p;
}

// free a proc structure and the data hanging from it,
// including user pages.
// p->lock must be held.
static void
freeproc(struct proc *p)
{
  if(p->trapframe)
    kfree((void*)p->trapframe);
  p->trapframe = 0;
  if(p->pagetable)
    proc_freepagetable(p->pagetable, p->sz);
  p->pagetable = 0;
  p->sz = 0;
  p->pid = 0;
  p->parent = 0;
  p->name[0] = 0;
  p->chan = 0;
  p->killed = 0;
  p->xstate = 0;
  p->state = UNUSED;
  if (remove(&zombie_procs, p - proc) < 0)
    panic("ahhhhhhhhh removeeeeee");
  enque(&unused_entrys, p - proc);

  
}

// Create a user page table for a given process,
// with no user memory, but with trampoline pages.
pagetable_t
proc_pagetable(struct proc *p)
{
  pagetable_t pagetable;

  // An empty page table.
  pagetable = uvmcreate();
  if(pagetable == 0)
    return 0;

  // map the trampoline code (for system call return)
  // at the highest user virtual address.
  // only the supervisor uses it, on the way
  // to/from user space, so not PTE_U.
  if(mappages(pagetable, TRAMPOLINE, PGSIZE,
              (uint64)trampoline, PTE_R | PTE_X) < 0){
    uvmfree(pagetable, 0);
    return 0;
  }

  // map the trapframe just below TRAMPOLINE, for trampoline.S.
  if(mappages(pagetable, TRAPFRAME, PGSIZE,
              (uint64)(p->trapframe), PTE_R | PTE_W) < 0){
    uvmunmap(pagetable, TRAMPOLINE, 1, 0);
    uvmfree(pagetable, 0);
    return 0;
  }

  return pagetable;
}

// Free a process's page table, and free the
// physical memory it refers to.
void
proc_freepagetable(pagetable_t pagetable, uint64 sz)
{
  uvmunmap(pagetable, TRAMPOLINE, 1, 0);
  uvmunmap(pagetable, TRAPFRAME, 1, 0);
  uvmfree(pagetable, sz);
}

// a user program that calls exec("/init")
// od -t xC initcode
uchar initcode[] = {
  0x17, 0x05, 0x00, 0x00, 0x13, 0x05, 0x45, 0x02,
  0x97, 0x05, 0x00, 0x00, 0x93, 0x85, 0x35, 0x02,
  0x93, 0x08, 0x70, 0x00, 0x73, 0x00, 0x00, 0x00,
  0x93, 0x08, 0x20, 0x00, 0x73, 0x00, 0x00, 0x00,
  0xef, 0xf0, 0x9f, 0xff, 0x2f, 0x69, 0x6e, 0x69,
  0x74, 0x00, 0x00, 0x24, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00
};

// Set up first user process.
void
userinit(void)
{
  struct proc *p;
  int proc_idx;

  p = allocproc();
  initproc = p;
  struct cpu* first_cpu = cpus;
  
  
  // allocate one user page and copy init's instructions
  // and data into it.
  uvminit(p->pagetable, initcode, sizeof(initcode));
  p->sz = PGSIZE;

  // prepare for the very first "return" from kernel to user.
  p->trapframe->epc = 0;      // user program counter
  p->trapframe->sp = PGSIZE;  // user stack pointer

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  p->state = RUNNABLE;
  proc_idx = p - proc;
  enque(&first_cpu->cpu_runnables, proc_idx);
  p->cpu_number = first_cpu - cpus;

  release(&p->lock);
}

// Grow or shrink user memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  uint sz;
  struct proc *p = myproc();

  sz = p->sz;
  if(n > 0){
    if((sz = uvmalloc(p->pagetable, sz, sz + n)) == 0) {
      return -1;
    }
  } else if(n < 0){
    sz = uvmdealloc(p->pagetable, sz, sz + n);
  }
  p->sz = sz;
  return 0;
}

// Create a new process, copying the parent.
// Sets up child kernel stack to return as if from fork() system call.
int
fork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *p = myproc();

  // Allocate process.
  if((np = allocproc()) == 0){
    return -1;
  }

  // Copy user memory from parent to child.
  if(uvmcopy(p->pagetable, np->pagetable, p->sz) < 0){
    freeproc(np);
    release(&np->lock);
    return -1;
  }
  np->sz = p->sz;

  // copy saved user registers.
  *(np->trapframe) = *(p->trapframe);

  // Cause fork to return 0 in the child.
  np->trapframe->a0 = 0;

  // increment reference counts on open file descriptors.
  for(i = 0; i < NOFILE; i++)
    if(p->ofile[i])
      np->ofile[i] = filedup(p->ofile[i]);
  np->cwd = idup(p->cwd);

  safestrcpy(np->name, p->name, sizeof(p->name));

  pid = np->pid;

  release(&np->lock);

  acquire(&wait_lock);
  np->parent = p;
  release(&wait_lock);

  acquire(&np->lock);
  np->cpu_number = get_the_least_used_cpu_and_increase_its_counter();
  np->state = RUNNABLE;
  enque(&cpus[np->cpu_number].cpu_runnables, np - proc);
  release(&np->lock);

  return pid;
}

// Pass p's abandoned children to init.
// Caller must hold wait_lock.
void
reparent(struct proc *p)
{
  struct proc *pp;

  for(pp = proc; pp < &proc[NPROC]; pp++){
    if(pp->parent == p){
      pp->parent = initproc;
      wakeup(initproc);
    }
  }
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait().
void
exit(int status)
{
  struct proc *p = myproc();

  if(p == initproc)
    panic("init exiting");

  // Close all open files.
  for(int fd = 0; fd < NOFILE; fd++){
    if(p->ofile[fd]){
      struct file *f = p->ofile[fd];
      fileclose(f);
      p->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(p->cwd);
  end_op();
  p->cwd = 0;

  acquire(&wait_lock);

  // Give any children to init.
  reparent(p);

  // Parent might be sleeping in wait().
  wakeup(p->parent);
  
  acquire(&p->lock);

  p->xstate = status;
  p->state = ZOMBIE;
  enque(&zombie_procs, p - proc);

  release(&wait_lock);

  // Jump into the scheduler, never to return.
  sched();
  panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
wait(uint64 addr)
{
  struct proc *np;
  int havekids, pid;
  struct proc *p = myproc();

  acquire(&wait_lock);

  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    for(np = proc; np < &proc[NPROC]; np++){
      if(np->parent == p){
        // make sure the child isn't still in exit() or swtch().
        acquire(&np->lock);

        havekids = 1;
        if(np->state == ZOMBIE){
          // Found one.
          pid = np->pid;
          if(addr != 0 && copyout(p->pagetable, addr, (char *)&np->xstate,
                                  sizeof(np->xstate)) < 0) {
            release(&np->lock);
            release(&wait_lock);
            return -1;
          }
          freeproc(np);
          release(&np->lock);
          release(&wait_lock);
          return pid;
        }
        release(&np->lock);
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || p->killed){
      release(&wait_lock);
      return -1;
    }
    
    // Wait for a child to exit.
    sleep(p, &wait_lock);  //DOC: wait-sleep
  }
}

// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run.
//  - swtch to start running that process.
//  - eventually that process transfers control
//    via swtch back to the scheduler.
void
scheduler(void)
{
  struct proc *p;
  struct cpu *c = mycpu();
  if (c->proc_counter == 0){
    c->proc_counter = 1;
  }
  
  c->proc = 0;
  for(;;){
    // Avoid deadlock by ensuring that devices can interrupt.
    intr_on();
    int next_to_run;
    if ((next_to_run = deque(&c->cpu_runnables)) >= 0){
      p = &proc[next_to_run];
    

    // for(p = proc; p < &proc[NPROC]; p++) {
      acquire(&p->lock);
      // if(p->state == RUNNABLE) {
        // Switch to chosen process.  It is the process's job
        // to release its lock and then reacquire it
        // before jumping back to us.
      p->state = RUNNING;
      c->proc = p;
      swtch(&c->context, &p->context);

        // Process is done running for now.
        // It should have changed its p->state before coming back.
      c->proc = 0;
      // }
      release(&p->lock);
    }
  }
}

// Switch to scheduler.  Must hold only p->lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->noff, but that would
// break in the few places where a lock is held but
// there's no process.
void
sched(void)
{
  int intena;
  struct proc *p = myproc();

  if(!holding(&p->lock))
    panic("sched p->lock");
  if(mycpu()->noff != 1)
    panic("sched locks");
  if(p->state == RUNNING)
    panic("sched running");
  if(intr_get())
    panic("sched interruptible");

  intena = mycpu()->intena;
  swtch(&p->context, &mycpu()->context);
  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void
yield(void)
{
  struct proc *p = myproc();
  acquire(&p->lock);
  p->state = RUNNABLE;
  enque(&cpus[p->cpu_number].cpu_runnables, p - proc);
  sched();
  release(&p->lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch to forkret.
void
forkret(void)
{
  static int first = 1;

  // Still holding p->lock from scheduler.
  release(&myproc()->lock);

  if (first) {
    // File system initialization must be run in the context of a
    // regular process (e.g., because it calls sleep), and thus cannot
    // be run from main().
    first = 0;
    fsinit(ROOTDEV);
  }

  usertrapret();
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void
sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();
  
  // Must acquire p->lock in order to
  // change p->state and then call sched.
  // Once we hold p->lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup locks p->lock),
  // so it's okay to release lk.

  acquire(&p->lock);  //DOC: sleeplock1
  release(lk);

  // Go to sleep.
  p->chan = chan;
  p->state = SLEEPING;
  enque(&sleeping_procs, p - proc);

  sched();

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  release(&p->lock);
  acquire(lk);
}

// Wake up all processes sleeping on chan.
// Must be called without any p->lock.
void
wakeup(void *chan)
{
  // printf("w%d\n", cpuid());
  // struct proc *p;

  // for(p = proc; p < &proc[NPROC]; p++) {
  //   if(p != myproc()){
  //     acquire(&p->lock);
  //     if(p->state == SLEEPING && p->chan == chan) {
  //       p->state = RUNNABLE;
  //     }
  //     release(&p->lock);
  //   }
  // }

  remove_all_sleeping_on_channel(chan);
  
}

// Kill the process with the given pid.
// The victim won't exit until it tries to return
// to user space (see usertrap() in trap.c).
int
kill(int pid)
{
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++){
    acquire(&p->lock);
    if(p->pid == pid){
      p->killed = 1;
      if(p->state == SLEEPING){
        // Wake process from sleep().
        p->state = RUNNABLE;
      }
      release(&p->lock);
      return 0;
    }
    release(&p->lock);
  }
  return -1;
}

// Copy to either a user address, or kernel address,
// depending on usr_dst.
// Returns 0 on success, -1 on error.
int
either_copyout(int user_dst, uint64 dst, void *src, uint64 len)
{
  struct proc *p = myproc();
  if(user_dst){
    return copyout(p->pagetable, dst, src, len);
  } else {
    memmove((char *)dst, src, len);
    return 0;
  }
}

// Copy from either a user address, or kernel address,
// depending on usr_src.
// Returns 0 on success, -1 on error.
int
either_copyin(void *dst, int user_src, uint64 src, uint64 len)
{
  struct proc *p = myproc();
  if(user_src){
    return copyin(p->pagetable, dst, src, len);
  } else {
    memmove(dst, (char*)src, len);
    return 0;
  }
}

// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void
procdump(void)
{
  static char *states[] = {
  [UNUSED]    "unused",
  [SLEEPING]  "sleep ",
  [RUNNABLE]  "runble",
  [RUNNING]   "run   ",
  [ZOMBIE]    "zombie"
  };
  struct proc *p;
  char *state;

  printf("\n");
  for(p = proc; p < &proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    printf("%d %s %s", p->pid, state, p->name);
    printf("\n");
  }
}

// // bad case: insertion = 1, head = tail = 2
// // right after the cas:
// // head = 1 -> 2 = tail
// // then another process deque: take 2 and set tail to -1
// // head = 1 = tail but prev_head = 2

// int enque(struct proc_ll* queue, int insertion){
//   int previous_head;                                            
//   do{                                                           // q.head = x -> ... -> -1
//     previous_head = queue->head;                                // previous_head = x
//   } while (cas(&queue->head, previous_head, insertion));        // q.head = insertion -> -1

//   proc[insertion].nextNodeIndex = previous_head;                // insertion -> x -> ... -> -1

//   if (previous_head == -1){                                     // only the first inserted head apply
//     if (!cas(&queue->tail, -1, insertion)){                     // if q is empty: tail = insertion; return
//       return 0;
//     }
//   }

//   acquire(&proc[previous_head].lock);
//   proc[previous_head].prevNodeIndex = insertion;                // insertion <- x <- ... <- tail
//   release(&proc[previous_head].lock);

//   return 0;
// }

// int deque(struct proc_ll* queue){
//   int previous_tail;
//   int current_tail;
//   do{
//     previous_tail = queue->tail;
//   } while (cas(&queue->tail, previous_tail, proc[previous_tail].prevNodeIndex));

//   if (previous_tail == -1){
//     return -1;
//   }

//   current_tail = proc[previous_tail].prevNodeIndex;
//   proc[current_tail].nextNodeIndex = -1;
//   proc[previous_tail].prevNodeIndex = -1;
//   proc[previous_tail].nextNodeIndex = -1;
//   return previous_tail;
// }


int enque(struct proc_ll* queue, int insertion){
  // printf("e%d\n", queue->tail);
  acquire(&queue->h_lock);
  acquire(&proc[insertion].node_lock);
  proc[insertion].nextNodeIndex = queue->head;
  queue->head = insertion;
  release(&proc[insertion].node_lock);
  release(&queue->h_lock);
  return 0;
}

int deque(struct proc_ll* queue){
  // printf("d%d\n", queue->tail);
  int prev;
  int cur;
  struct spinlock* prevlock;
  struct spinlock* curlock;
  prevlock = &queue->h_lock;
  acquire(prevlock);
  if (queue->head < 0){
    release(prevlock);
    return -1;
  }
  cur = queue->head;
  curlock = &proc[cur].node_lock;
  prev = -1;
  acquire(curlock);
  while(proc[cur].nextNodeIndex >= 0){
    release(prevlock);
    prevlock = curlock;
    prev = cur;
    cur = proc[cur].nextNodeIndex;
    curlock = &proc[cur].node_lock;
    acquire(curlock);  
  }
  if (prev < 0){ // then only 1 element in the list
    queue->head = proc[cur].nextNodeIndex;
  }else{
    proc[prev].nextNodeIndex = proc[cur].nextNodeIndex;
  }
  proc[cur].nextNodeIndex = -1000;
  release(prevlock);
  release(curlock);
  return cur;
}

int remove(struct proc_ll* queue, int node){
  int prev;
  // printf("r%d\n", queue->tail);
  int cur;
  struct spinlock* lastlock;
  lastlock = &queue->h_lock;
  acquire(lastlock);
  prev = -1;
  cur = queue->head;
  while(cur >= 0){
    acquire(&proc[cur].node_lock);
    if (cur == node){
      if (prev < 0){ // then remove in the head
        queue->head = proc[cur].nextNodeIndex;
      }else{
        proc[prev].nextNodeIndex = proc[cur].nextNodeIndex;
      }
      proc[cur].nextNodeIndex = -1000;
      release(&proc[cur].node_lock);
      release(lastlock);
      return node;
    }
    release(lastlock);
    lastlock = &proc[cur].node_lock;
    prev = cur;
    cur = proc[cur].nextNodeIndex;    
  }
  release(lastlock);
  return -1;
}

int remove_all_sleeping_on_channel(void* channel){
  int prev;
  int cur;
  struct spinlock* lastlock;
  struct proc_ll* queue = &sleeping_procs;
  lastlock = &queue->h_lock;
  acquire(lastlock);
  prev = -1;
  cur = queue->head;
  while(cur >= 0){
    acquire(&proc[cur].node_lock);
    if (proc[cur].chan == channel){
      int* prev_pointer;
      if (prev < 0){ // then remove in the head
        prev_pointer = &queue->head;
        // queue->head = proc[cur].nextNodeIndex;
      }else{
        prev_pointer = &proc[prev].nextNodeIndex;
        // proc[prev].nextNodeIndex = proc[cur].nextNodeIndex;
      }
      *prev_pointer = proc[cur].nextNodeIndex;
      proc[cur].nextNodeIndex = -1000;
      release(&proc[cur].node_lock);

      // handle wakeup
      acquire(&proc[cur].lock);
      proc[cur].state = RUNNABLE;
      proc[cur].cpu_number = get_the_least_used_cpu_and_increase_its_counter();
      enque(&cpus[proc[cur].cpu_number].cpu_runnables, cur);
      release(&proc[cur].lock);
      //

      cur = *prev_pointer;

    }else{
      release(lastlock);
      lastlock = &proc[cur].node_lock;
      prev = cur;
      cur = proc[cur].nextNodeIndex;
    }
  }
  release(lastlock);
  return -1;
}

int
set_cpu(int cpu_num)
{
  myproc()->cpu_number = cpu_num;
  return myproc() - proc;
}

int
get_cpu()
{
  return myproc()->cpu_number;
}

int
cpu_process_count(int cpu_num)
{
  return cpus[cpu_num].proc_counter;
}