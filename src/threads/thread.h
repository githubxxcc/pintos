#ifndef THREADS_THREAD_H
#define THREADS_THREAD_H

#include <debug.h>
#include <list.h>
#include "synch.h"
#include <stdint.h>

/* States in a thread's life cycle. */
enum thread_status
  {
    THREAD_RUNNING,     /* Running thread. */
    THREAD_READY,       /* Not running but ready to run. */
    THREAD_BLOCKED,     /* Waiting for an event to trigger. */
    THREAD_DYING        /* About to be destroyed. */
  };


/* Thread list types */
enum list_type
{
    ELEM,               /* For ready list scheduling in struct thread */
    DONOR,              /* For donor list in struct thread*/
    WAITER              /* For waiter list in struct semaphore */
};

/* Thread identifier type.
   You can redefine this to whatever type you like. */
typedef int tid_t;
#define TID_ERROR ((tid_t) -1)          /* Error value for tid_t. */

/* Thread priorities. */
#define PRI_MIN 0                       /* Lowest priority. */
#define PRI_DEFAULT 31                  /* Default priority. */
#define PRI_MAX 63                      /* Highest priority. */

/* Thread niceness. */
#define NICE_DEFAULT 0                  /* Default nice. */
#define NICE_MIN -20                    /* Lowest nice. */
#define NICE_MAX 20                     /* Highest nice. */

/* A kernel thread or user process.

   Each thread structure is stored in its own 4 kB page.  The
   thread structure itself sits at the very bottom of the page
   (at offset 0).  The rest of the page is reserved for the
   thread's kernel stack, which grows downward from the top of
   the page (at offset 4 kB).  Here's an illustration:

        4 kB +---------------------------------+
             |          kernel stack           |
             |                |                |
             |                |                |
             |                V                |
             |         grows downward          |
             |                                 |
             |                                 |
             |                                 |
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
         few bytes in size.  It probably should stay well under 1
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
   set to THREAD_MAGIC.  Stack overflow will normally change this
   value, triggering the assertion. */
/* The `elem' member has a dual purpose.  It can be an element in
   the run queue (thread.c), or it can be an element in a
   semaphore wait list (synch.c).  It can be used these two ways
   only because they are mutually exclusive: only a thread in the
   ready state is on the run queue, whereas only a thread in the
   blocked state is on a semaphore wait list. */
struct thread
  {
    /* Owned by thread.c. */
    tid_t tid;                          /* Thread identifier. */
    enum thread_status status;          /* Thread state. */
    char name[16];                      /* Name (for debugging purposes). */
    uint8_t *stack;                     /* Saved stack pointer. */
    int priority;                       /* Priority. */
    struct list_elem allelem;           /* List element for all threads list. */
    
    /* Shared between thread.c and synch.c. */
    struct list_elem elem;              /* List element. */

    //P1-2
    struct lock *waiting_lock;          /* Lock which the thread is waiting for */
    struct list donors;                 /* doners list wiht priority higher than t. */
    struct list_elem donor_elem;        /* List element for donors list, one list only*/
    struct list_elem waiter_elem;       /* List element for waiter list,one list only*/ 
    int static_priority;                /* Priority not affected by donation */
    
    //P1-3
    int32_t recent_cpu;                 /* Per-thread recent_cpu data */
    int nice;                           /* What a good guy */
    bool recent_cpu_dirty;              /* Whether recent_cpu has changed */
    /* Enforce preemption. */
    
    //P1-1
    int64_t wake_up_time;               /* Wake up time from sleep */
    struct list_elem sleep_elem;        /* Sleep List element */
#ifdef USERPROG
    /* Owned by userprog/process.c. */
    uint32_t *pagedir;                  /* Page directory. */
    //P2
    struct thread * parent;              /* Pointer to the parent process */
    struct list children;               /* List of children */
    struct list_elem parent_elem;       /* Its parent's childrent list elem */
    int err;                            /* For Error code */
    int exit_status;                    /* Exit status */
    unsigned int flags;                 /* Flags, details defined below */ 
    struct semaphore exiting;         /* Signal to parent that the thread is done */
    struct semaphore loading;         /* Signal to parent that the thread is loaded */
    struct file_struct * files;        /* Pointer to open files */
    struct file * exe;                  /* Executable file pointer, owned by process.c:load*/
    void * aux;                         /* For storing the pointer to aux data*/

#endif
    
    /* Owned by thread.c. */
    unsigned magic;                     /* Detects stack overflow. */
  };

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
extern bool thread_mlfqs;

void thread_init (void);
void thread_start (void);

void thread_tick (void);
void thread_print_stats (void);

typedef void thread_func (void *aux);
tid_t thread_create (const char *name, int priority, thread_func *, void *);

void thread_block (void);
void thread_unblock (struct thread *);

struct thread *thread_current (void);
tid_t thread_tid (void);
const char *thread_name (void);

void thread_exit (void) NO_RETURN;
void thread_yield (void);
void thread_sleep(int64_t);

/* Performs some operation on thread t, given auxiliary data AUX. */
typedef void thread_action_func (struct thread *t, void *aux);
void thread_foreach (thread_action_func *, void *);

//P1
int thread_get_priority (void);
void thread_set_priority (int);
bool thread_has_highest_priority(void);

bool thread_less_priority(const struct list_elem*, const struct list_elem*, void *);

struct thread* thread_dequeue_ready_list (struct thread *);
void thread_queue_ready_list (struct thread *);

int thread_get_nice (void);
void thread_set_nice (int);
int thread_get_recent_cpu (void);
int thread_get_load_avg (void);

//P2
struct thread * thread_child_tid(struct thread *, tid_t);

#define PF_EXITING      0x00000002      /* Thread exiting */
#define PF_KILLED       0x00000004      /* Killed by Kernel */

#endif /* threads/thread.h */
