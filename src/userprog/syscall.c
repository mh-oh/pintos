#include "userprog/syscall.h"
#include "lib/user/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "threads/synch.h"
#include "threads/malloc.h"
#include "userprog/process.h"
#include "devices/shutdown.h"
#include "filesys/file.h"
#include "filesys/filesys.h"

static void syscall_handler (struct intr_frame *);

/* Number of system calls. */
#define SYSCALL_CNT 13

/* Wrapper functions for each system call.
   Each of them safely reads sycall arguments and invokes system
   call service. */
static void sys_halt_wrapper     (struct intr_frame *);
static void sys_exit_wrapper     (struct intr_frame *);
static void sys_exec_wrapper     (struct intr_frame *);
static void sys_wait_wrapper     (struct intr_frame *);
static void sys_create_wrapper   (struct intr_frame *);
static void sys_remove_wrapper   (struct intr_frame *);
static void sys_open_wrapper     (struct intr_frame *);
static void sys_filesize_wrapper (struct intr_frame *);
static void sys_read_wrapper     (struct intr_frame *);
static void sys_write_wrapper    (struct intr_frame *);
static void sys_seek_wrapper     (struct intr_frame *);
static void sys_tell_wrapper     (struct intr_frame *);
static void sys_close_wrapper    (struct intr_frame *);

/* In Pintos, system call number and arguments are all 32-bit
   values.  See lib/user/syscall.c */
typedef uint32_t sys_param_type;
typedef void sys_wrapper_func (struct intr_frame *);

/* System call wrapper functions for each system call. */
static sys_wrapper_func *sys_wrap_funcs[SYSCALL_CNT];

/* User memory read/write helpers.
   Every user memory access required by system call must be done
   using these helper functions. */
static long copy_from_user (void *, const void *, size_t);
static void copy_to_user (void *, const void *, size_t);
static long strncpy_from_user (char *, const char *, size_t);

static void bad_user_access (void);

/* Makes an address of IDXth syscall argument from
   stack top pointer ESP which is passed in interrupt frame.
   In Pintos, each system call pushes its number and
   several arguments into stack, and it invokes `syscall_handler'.
   Here, IDX 0 indicates the first argument stored in stack,
   and therefore IDX -1 indicates the syscall's number. */
#define SYSCALL_ARG_ADDR(ESP, IDX) \
        ((const void *) (((uintptr_t) (ESP)) \
                          + ((IDX) + 1) * sizeof (sys_param_type)))

/* Helper routine.  It reads IDXth syscall argument from
   the stack and stores into DST. */
#define SYSCALL_GET_ARG(ESP, IDX, DST) \
        if (SYSCALL_ARG_ADDR (ESP, IDX) != NULL) { \
          copy_from_user (DST, \
                          SYSCALL_ARG_ADDR (ESP, IDX), \
                          sizeof (sys_param_type)); \
        } else { \
          bad_user_access (); \
        }

/* Safely retrieves -1th syscall argument, a syscall number. */
#define SYSCALL_GET_NUMBER(ESP, DST) \
        SYSCALL_GET_ARG(ESP, -1, DST);

/* Safely retrieves "one" syscall argument. */
#define SYSCALL_GET_ARGS1(ESP, DST0) \
        SYSCALL_GET_ARG(ESP, 0, DST0);
/* Safely retrieves "two" syscall argument. */
#define SYSCALL_GET_ARGS2(ESP, DST0, DST1) \
        SYSCALL_GET_ARGS1(ESP, DST0); \
        SYSCALL_GET_ARG(ESP, 1, DST1);
/* Safely retrieves "three" syscall argument. */
#define SYSCALL_GET_ARGS3(ESP, DST0, DST1, DST2) \
        SYSCALL_GET_ARGS2(ESP, DST0, DST1); \
        SYSCALL_GET_ARG(ESP, 2, DST2);

/*  */
static struct lock fs_lock;

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");

  lock_init (&fs_lock);

  /* Initialize system call wrappers. */
  sys_wrap_funcs[SYS_HALT]     = sys_halt_wrapper;
  sys_wrap_funcs[SYS_EXIT]     = sys_exit_wrapper;
  sys_wrap_funcs[SYS_EXEC]     = sys_exec_wrapper;
  sys_wrap_funcs[SYS_WAIT]     = sys_wait_wrapper;
  sys_wrap_funcs[SYS_CREATE]   = sys_create_wrapper;
  sys_wrap_funcs[SYS_REMOVE]   = sys_remove_wrapper;
  sys_wrap_funcs[SYS_OPEN]     = sys_open_wrapper;
  sys_wrap_funcs[SYS_FILESIZE] = sys_filesize_wrapper;
  sys_wrap_funcs[SYS_READ]     = sys_read_wrapper;
  sys_wrap_funcs[SYS_WRITE]    = sys_write_wrapper;
  sys_wrap_funcs[SYS_SEEK]     = sys_seek_wrapper;
  sys_wrap_funcs[SYS_TELL]     = sys_tell_wrapper;
  sys_wrap_funcs[SYS_CLOSE]    = sys_close_wrapper;
}

static void
syscall_handler (struct intr_frame *f) 
{
  void *esp = f->esp;
  int no;
  SYSCALL_GET_NUMBER (esp, &no);

  /* Invokes system call wrapper function. */
  if (no < 0 || no > SYSCALL_CNT)
    PANIC ("Unknown system call");
  else
    {
      sys_wrapper_func *wrap_func = sys_wrap_funcs[no];
      wrap_func (f);
    }
}

/* Reads a byte at user virtual address USRC.
   USRC must be below PHYS_BASE.
   Returns the byte value if successful, -1 if a segfault occurred. */
static int
get_user (const uint8_t *usrc)
{
  int result;
  asm ("movl $1f, %0; movzbl %1, %0; 1:"
       : "=&a" (result) : "m" (*usrc));
  return result;
}

/* Writes BYTE to user address UDST.
   UDST must be below PHYS_BASE.
   Returns true if successful, false if a segfault occurred. */
static bool
put_user (uint8_t *udst, uint8_t byte)
{
  int error_code;
  asm ("movl $1f, %0; movb %b2, %1; 1:"
       : "=&a" (error_code), "=m" (*udst) : "q" (byte));
  return error_code != SYS_BAD_ADDR;
}

/* Reads SIZE bytes from user virtual address USRC to UDST.
   If USRC points to kernel memory or causes page fault,
   returns -1, otherwise returns the number of bytes read. */
static long
copy_from_user (void *udst_, const void *usrc_, size_t size)
{
  uint8_t* udst = udst_;
  const uint8_t* usrc = usrc_;
  long res = 0;
  int byte;

  ASSERT (udst != NULL || size == 0);
  ASSERT (usrc != NULL || size == 0);

  for (; size > 0; size--, res++)
    {
      /* Assumes that the user address has already been verified
         to be below PHYS_BASE. */
      if (!is_user_vaddr (usrc + res))
        bad_user_access ();
      /* A memory access causes page fault. */
      if ((byte = get_user (usrc + res)) == SYS_BAD_ADDR)
        bad_user_access ();
      
      udst[res] = (uint8_t) byte;
    }
  return res;
}

/* Copies a string from USRC to UDST.  If USRC is longer than
   SIZE - 1 characters, only SIZE - 1 characters are copied.
   A null terminator is always written to DST, unless SIZE is 0.
   
   If USRC points to kernel memory or causes page fault,
   returns -1, otherwise returns the length of SRC, not including
   the null terminator. */
static long
strncpy_from_user (char *udst, const char *usrc_, size_t size)
{
  const uint8_t *usrc = usrc_;
	long res = 0;
  int byte;

  ASSERT (udst != NULL);
  ASSERT (usrc != NULL);

  if (!size)
    return 0;

  for (; size > 0; size--, res++)
    {
      /* Assumes that the user address has already been verified
         to be below PHYS_BASE. */
      if (!is_user_vaddr (usrc + res))
        bad_user_access ();
      /* A memory access causes page fault. */
      if ((byte = get_user (usrc + res)) == SYS_BAD_ADDR)
        bad_user_access ();
      
		  udst[res] = (char) byte;
		  if (!byte)
		  	return res;
    }

  udst[--res] = '\0';
  return res;
}

/* Core system call services.  Each of them provides kernel
   services requested by user program.
   
   For example, if a user program calls `exit' function, which is
   a user level syscall, it invokes `syscall_handler' through internal
   interrupt handling process.  The system call handler examines
   the user stack to extract a syscall number, and then calls
   `sys_exit_wrapper' which retrieves syscall arguments and finally
   calls `sys_exit'.  Here, `sys_exit' performs actual kernel
   functionality. */

/* Terminates Pintos by calling `shutdown_power_off()'. */
void
sys_halt (void)
{
  shutdown_power_off ();
}

/* Terminates the current user program.
   If the process's parent waits for it, this is the status
   that will be returned. */
void
sys_exit (int status)
{
  struct thread *cur = thread_current ();
  printf ("%s: exit(%d)\n", cur->name, status);
  cur->process->exit_status = status;
  thread_exit ();
}

/* Runs the executable whose name is given in CMDLINE,
   passing any given arguments, and returns the new process's
   program id.  Returns pid -1, which otherwise should not
   be a valid pid, if the program cannot load or run for
   any reason.  Thus, the parent process cannot return from
   the `exec' until it knows whether the child process successfully
   loaded its executable. */
pid_t
sys_exec (const char *cmdline)
{
  char buf[256];

  if (cmdline == NULL)
    bad_user_access ();

  strncpy_from_user (buf, cmdline, 256);
  return process_execute (buf);
}

/* Waits for a child process PID and retrieves the child's
   exit status.
   If PID is still alive, waits until it terminates.  Then,
   returns the status that PID passed to `exit'. If PID did not
   call `exit()', but was terminated by the kernel (e.g. killed
   due to an exception), should return -1. */
int
sys_wait (pid_t pid)
{
  return process_wait ((tid_t) pid);
}

/* Creates a new file given the path FILE initially
   INITIAL_SIZE bytes in size.
   It returns true if successful, or false otherwise.
   Notice that creating a file does not open the file: opening
   the new file is performed by `open' system call. */
bool
sys_create (const char *file, unsigned initial_size)
{
  char buf[256];
  bool res;

  if (file == NULL)
    bad_user_access ();

  strncpy_from_user (buf, file, 256);

  lock_acquire (&fs_lock);
  res = filesys_create (buf, initial_size);
  lock_release (&fs_lock);

  return res;
}

/* Deletes a file given the path FILE.
   It returns true if successful, or false otherwise.
   A file may be removed regardless of whether it is open or closed.
   Notice that removing a file does not close the file: closing
   a file is performed by close system call. */
bool
sys_remove (const char *file)
{
  char buf[256];
  bool res;

  if (file == NULL)
    bad_user_access ();

  strncpy_from_user (buf, file, 256);

  lock_acquire (&fs_lock);
  res = filesys_remove (buf);
  lock_release (&fs_lock);

  return res;
}

/* A file descriptor. */
struct file_desc
  {
    struct list_elem fd_list_elem;   /* List element. */
    struct file *file;               /* File. */
    int no;                          /* File descriptor number. */
  };

/* Finds a file descriptor with the given FD_NO.
   If not found, returns NULL. */
static struct file_desc *
find_file_desc (int fd_no)
{
  struct thread *cur = thread_current ();
  struct list *fd_list = &cur->fd_list;
  struct list_elem *e;
  for (e = list_begin (fd_list); e != list_end (fd_list);
       e = list_next (e))
    {
      struct file_desc *fd
        = list_entry (e, struct file_desc, fd_list_elem);
      if (fd->no == fd_no)
        return fd;
    }
  return NULL;
}

/* Opens the file given the path FILE.  It returns a file descriptor
   of the opend file, or -­1 if open failed.  Two file descriptors are
   reserved for the console; STDIN_FILENO for standard input and
   STDOUT_FILENO for standard output.

   Each process has an independent set of file descriptors which is not
   limited on the number, and these file descriptors are not inherited
   by child processes.

   It is possible for a single process or different processes to open
   the same file more than once, and each `open' system call returns a
   new file descriptor.  That is, different file descriptors can indicates
   a single open file.  These descriptors are closed independently in
   each call to close, and they do not share a file position. */
int
sys_open (const char *file)
{
  struct thread *cur = thread_current ();
  struct file *f;
  struct file_desc *fd;
  char buf[256];

  if (file == NULL)
    return -1;

  strncpy_from_user (buf, file, 256);

  if ((fd = malloc (sizeof (struct file_desc))) == NULL)
    return -1;

  lock_acquire (&fs_lock);
  if ((f = filesys_open (buf)) == NULL)
    {
      free (fd);
      lock_release (&fs_lock);
      return -1;
    }
  
  fd->file = f;
  fd->no = cur->next_fd_no++;
  list_push_back (&cur->fd_list, &fd->fd_list_elem);

  lock_release (&fs_lock);
  return fd->no;
}

int
sys_filesize (int fd)
{
  PANIC ("Not implemented yet");
}

int
sys_read (int fd, void *buffer, unsigned size)
{
  PANIC ("Not implemented yet");
}

int
sys_write (int fd, const void *buffer, unsigned size)
{
  if (fd == 1)
    {
      putbuf (buffer, size);
      return size;
    }
  PANIC ("Not implemented yet");
}

void
sys_seek (int fd, unsigned position)
{
  PANIC ("Not implemented yet");
}

unsigned
sys_tell (int fd)
{
  PANIC ("Not implemented yet");
}

/* Closes the opened file with the given file descriptor FD_NO. */
void
sys_close (int fd_no)
{
  struct file_desc *fd;
  if ((fd = find_file_desc (fd_no)) == NULL)
    return;
  lock_acquire(&fs_lock);
  file_close (fd->file);
  list_remove (&fd->fd_list_elem);
  free (fd);
  lock_release (&fs_lock);
}

/* Closes all opened files of the current process. */
void
sys_close_all (void)
{
  struct thread *cur = thread_current ();
  struct list *fd_list = &cur->fd_list;
  while (!list_empty (fd_list))
    {
      struct file_desc *fd
        = list_entry (list_pop_front (fd_list), struct file_desc,
                      fd_list_elem);
      lock_acquire (&fs_lock);
      file_close (fd->file);
      lock_release (&fs_lock);
      free (fd);
    }
}

/* System call wrapper function implementations.
   Each wrapper function, if needed, reads passed arguments
   and calls its corresponding system call. */

static void
sys_halt_wrapper (struct intr_frame *f UNUSED)
{
  sys_halt ();
}

static void
sys_exit_wrapper (struct intr_frame *f)
{
  sys_param_type ARG0;
  SYSCALL_GET_ARGS1 (f->esp, &ARG0);
  sys_exit ((int) ARG0);
}

static void
sys_exec_wrapper (struct intr_frame *f)
{
  sys_param_type ARG0;
  SYSCALL_GET_ARGS1 (f->esp, &ARG0);
  f->eax = sys_exec ((const char *) ARG0);
}

static void
sys_wait_wrapper (struct intr_frame *f)
{
  sys_param_type ARG0;
  SYSCALL_GET_ARGS1 (f->esp, &ARG0);
  f->eax = sys_wait ((pid_t) ARG0);
}

static void
sys_create_wrapper (struct intr_frame *f)
{
  sys_param_type ARG0, ARG1;
  SYSCALL_GET_ARGS2 (f->esp, &ARG0, &ARG1);
  f->eax = sys_create ((const char *) ARG0, (unsigned) ARG1);
}

static void
sys_remove_wrapper (struct intr_frame *f)
{
  sys_param_type ARG0;
  SYSCALL_GET_ARGS1 (f->esp, &ARG0);
  f->eax = sys_remove ((const char *) ARG0);
}

static void
sys_open_wrapper (struct intr_frame *f)
{
  sys_param_type ARG0;
  SYSCALL_GET_ARGS1 (f->esp, &ARG0);
  f->eax = sys_open ((const char *) ARG0);
}

static void
sys_filesize_wrapper (struct intr_frame *f)
{
  sys_param_type ARG0;
  SYSCALL_GET_ARGS1 (f->esp, &ARG0);
  f->eax = sys_filesize ((int) ARG0);
}

static void
sys_read_wrapper (struct intr_frame *f)
{
  sys_param_type ARG0, ARG1, ARG2;
  SYSCALL_GET_ARGS3 (f->esp, &ARG0, &ARG1, &ARG2);
  f->eax = sys_read ((int) ARG0, (void *) ARG1, (unsigned) ARG2);
}

static void
sys_write_wrapper (struct intr_frame *f)
{
  sys_param_type ARG0, ARG1, ARG2;
  SYSCALL_GET_ARGS3 (f->esp, &ARG0, &ARG1, &ARG2);
  f->eax = sys_write ((int) ARG0, (const void *) ARG1, (unsigned) ARG2);
}

static void
sys_seek_wrapper (struct intr_frame *f)
{
  sys_param_type ARG0, ARG1;
  SYSCALL_GET_ARGS2 (f->esp, &ARG0, &ARG1);
  sys_seek ((int) ARG0, (unsigned) ARG1);
}

static void
sys_tell_wrapper (struct intr_frame *f)
{
  sys_param_type ARG0;
  SYSCALL_GET_ARGS1 (f->esp, &ARG0);
  f->eax = sys_tell ((int) ARG0);
}

static void
sys_close_wrapper (struct intr_frame *f)
{
  sys_param_type ARG0;
  SYSCALL_GET_ARGS1 (f->esp, &ARG0);
  sys_close ((int) ARG0);
}

/* Handles invalid user-provided pointer access. */
static void
bad_user_access (void)
{
  sys_exit (-1);
}
