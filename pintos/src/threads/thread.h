#ifndef THREADS_THREAD_H
#define THREADS_THREAD_H

#include <debug.h>
#include <list.h>
#include <stdint.h>
#include <stdlib.h>
#include "threads/synch.h"
#include "threads/fixed-point.h"

/* States in a thread's life cycle. */
enum thread_status {
  THREAD_RUNNING, /* Running tr ead. */
  THREAD_READY,   /* Not run ing but ready to run. */
  THREAD_BLOCKED, /* Waiting for an event to trigger. */
  THREAD_DYING    /* About to be destroyed. */
};

/* Thread identifier type.
   You can redefine this to whatever type you like. */
typedef int tid_t;
#define TID_ERROR ((tid_t)-1) /* Error value for tid_t. */

/* Thread priorities. */
#define PRI_MIN 0      /* Lowest priority. */
#define PRI_DEFAULT 31 /* Default priority. */
#define PRI_MAX 63     /* Highest priority. */

/* A  kernel thread or user process.
     
     
   Ech thread structure is stored in its own 4 kB page.  The
   thread structure itself sits at the very bottom of the page
   (at offset 0).  The rest of the page is reserved for the
   thread's kernel stack, which grows downward from the top of
        4 kB +---------------------------------+
             |          kernel stack           |
             |                |                |
             |                |                |
             |                V                |
             |         grows downward          |
             |                                     |                              |                                 |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             +---------------------------------+
             |              magic              |
             |                :                |
             |                :                |
             |               name              |
             |              status             |
        0 kB +---------------------------------+
   The upshot of this is twofold:
      1. First, `struct thread' must not be allowed to grow too
         big.  If it does, then there will not be enough room for
         the kernel stack.  Our base `struct thread' is only a
         few bytes in size.  I t probably should stay well under 1
         kB.
      2. Second, kernel stacks must not be allowed to grow too
         large.  If a stack overflows, it will corrupt the thread
         state.  Thus, kernel functions should not allocate large
         structures or arrays as non-static local variables.  Use
         dynamic allocation with malloc() or palloc_get_page()
         instead.
   The first symptom of either of these problems will probably be
   an assertion failure in thread_current(), which checks that
   the `magic' member of the running thread's `struct thread' is
   set to THREAD_MAGIC.  Stack  overflow will normally change this
   value, triggering the assertion. */
/* The `elem' member has  dual purpose.  It can be an element in
   the run queue (thread.c), or it can be an element in a
   semaphore wait list (synch.c).  It can be used these two ways
    only because they are mutually exclusive: only a thread in the
   ready state is on the run queue, whereas only a thread in the
   blocked state is on a semaphore wait list. */
struct thread {
  /* Owned by thread.c. */
  tid_t tid;                 /* Thread identifier. */
  enum thread_status status; /* Thread state. */
  char name[16];             /* Name (for debugging purposes). */
  uint8_t* stack;            /* Saved stack pointer. */
  int priority;              /* Priority. */
  struct list_elem allelem;  /* List element for all threads list. */
  struct thread_data* thread_data;
  struct list children_data;
  /* Shared between thread.c and synch.c. */
  struct list_elem elem; /* List elemt. */
#ifdef USERPROG
  /* Owned by userprog/process.c. */
  struct file** file_d;
  uint32_t* pagedir; /* Page directory. */
  struct file* executable;
#endif

  /* Owned bythread.c. */
  unsigned magic; /* Detects stack overflow. */
};

struct thread_data {
  struct semaphore sema;
  struct lock lock;
  int pid;
  int ref_cnt;
  bool loaded;
  bool waited;
  int exit_status;
  struct list_elem elem;
};

/* If false (default), use round-robin scheduler.
   If true, use multi-level feed*bck queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
extern bool thread_mlfqs;

void thread_init(void);
void thread_start(void);
void thread_tick(void);
void thread_print_stats(void);

typedef void thread_func(void* aux);
tid_t thread_create(const char* name, int priority, thread_func*, void*);

void thread_block(void);
void thread_unblock(struct thread*);

struct thread* thread_current(void);
tid_t thread_tid(void);
const char* thread_name(void);

void thread_exit(void) NO_RETURN;
void thread_yield(void);

/* Perfo rms some operation on thread t, given auxiliary data AUX. */
typedef void thread_action_func(struct thread* t, void* aux);
void thread_foreach(thread_action_func*, void*);

int thread_get_priority(void);
void thread_set_priority(int);

int thread_get_nice(void);
void thread_set_nice(int);
int thread_get_recent_cpu(void);
int thread_get_load_avg(void);

//Helper Functions for file descriptor array
bool init_file_d(
    struct thread*
        t); //Initialize file descriptor array with 0 and 1 set to dummy values and rest with value null
void remove_file_d(int fd, struct thread* t); //Remove pointer to file* struct at given fd
int add_file_d(
    struct file* file,
    struct thread*
        t); //Add file descriptor for file at first available index, iterating through indices to find the first available.

#endif /* threads/thread.h */