#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

/* [3.1.5] Accessing User Memory
   The provided code for `get_user' and `put_user' assumes that
   the page fault in the kernel returns -1. */
#define SYS_BAD_ADDR -1

void syscall_init (void);
void sys_exit (int);
void sys_close_all (void);

#endif /* userprog/syscall.h */
