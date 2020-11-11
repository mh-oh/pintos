#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include <list.h>
#include "threads/thread.h"
#include "threads/synch.h"

/* A process control block to maintain process hierarchy
   and exec/wait infrastructure.
   Each thread has a reference to this process control block and
   has a list of child processes. */
struct process
  {
    /* Basic information. */
    tid_t tid;                          /* Process identifier. */
    struct list_elem child_list_elem;   /* List element of child list in `struct thread'. */

    /* Shared data between `exec' and `wait'. */
    int exit_status;                    /* Process's exit status. */
    struct semaphore exit_wait;         /* Parent thread waits until child exits. */

    /* Shared memory control.  If both are false, this block
       should be `free'ed.  It is similar to the shared_ptr provided
       by standard C++ since 2011.  This is one form of shared_ptr
       where use_count is limited to a maximum of "2". */
    bool owned;                         /* The thread that executes this process does not terminate yet. */
    bool ref_by_parent;                 /* The parent thread does not terminate yet. */
    struct lock shared_mem_lock;        /* Chaning the boolean variables is performed atomically. */

    /* Prevent waiting more than once for the same tid. */
    bool wait_done;                     /* If true, wait call to this process must be ignored. */
  };

tid_t process_execute (const char *cmdline);
int process_wait (tid_t);
void process_exit (void);
void process_activate (void);

struct process *process_child (tid_t);

#endif /* userprog/process.h */
