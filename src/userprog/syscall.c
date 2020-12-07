#include "userprog/syscall.h"
#include "lib/user/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "lib/stdio.h"
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "threads/synch.h"
#include "threads/malloc.h"
#include "userprog/process.h"
#include "devices/shutdown.h"
#include "devices/input.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "vm/page.h"
#include "vm/frame.h"

static void syscall_handler (struct intr_frame *);

/* Number of system calls. */
#ifdef VM
#define SYSCALL_CNT 15
#else
#define SYSCALL_CNT 13
#endif

/* Wrapper functions for each system call.
   Each of them safely reads sycall arguments and invokes system
   call service. */

/* Projects 2 and later. */
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

#ifdef VM
/* Project 3 and optionally project 4. */
static void sys_mmap_wrapper     (struct intr_frame *);
static void sys_munmap_wrapper   (struct intr_frame *);
#endif

/* Prototypes. */
void     sys_halt (void);
void     sys_exit (int);
pid_t    sys_exec (const char *);
int      sys_wait (pid_t);
bool     sys_create (const char *, unsigned);
bool     sys_remove (const char *);
int      sys_open (const char *);
int      sys_filesize (int);
int      sys_read (int, void *, unsigned);
int      sys_write (int, const void *, unsigned);
void     sys_seek (int, unsigned);
unsigned sys_tell (int);
void     sys_close (int);
#ifdef VM
mapid_t  sys_mmap (int, void *);
void     sys_munmap (mapid_t);
#endif

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
static long copy_to_user (void *, const void *, size_t);
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

/* It is not safe to call into the file system code
   provided in the `filesys' directory from multiple threads
   at once.
   The file system code is treated as a critical section. */
struct lock fs_lock;

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");

  lock_init (&fs_lock);

  /* Projects 2 and later. */
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

#ifdef VM
  /* Project 3 and optionally project 4. */
  sys_wrap_funcs[SYS_MMAP]     = sys_mmap_wrapper;
  sys_wrap_funcs[SYS_MUNMAP]   = sys_munmap_wrapper;
#endif
}

static void
syscall_handler (struct intr_frame *f) 
{
  void *esp = f->esp;
  int no;
  SYSCALL_GET_NUMBER (esp, &no);

#ifdef VM
  /* Saves ESP into struct thread on the initial transition from
     user to kernel mode.
     
     It is needed when a page fault occurs in the kernel.
     Since the processor only saves the stack pointer when an
     exception causes a switch from user to kernel mode, reading
     ESP out of the struct intr_frame passed to page_fault()
     would yield an undefined value. */
  thread_current ()->saved_esp = f->esp;
#endif

  /* Invokes system call wrapper function. */
  if (no < 0 || no >= SYSCALL_CNT)
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
copy_from_user (void *kdst_, const void *usrc_, size_t size)
{
  uint8_t* kdst = kdst_;
  const uint8_t* usrc = usrc_;
  long res = 0;
  int byte;

  ASSERT (kdst != NULL || size == 0);
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
      
      kdst[res] = (uint8_t) byte;
    }
  return res;
}

/* Writes SIZE bytes from kernel virtual address KSRC to UDST.
   If UDST points to kernel memory or causes page fault,
   returns -1, otherwise returns the number of bytes written. */
static long
copy_to_user (void *udst_, const void *ksrc_, size_t size)
{
  uint8_t* udst = udst_;
  const uint8_t* ksrc = ksrc_;
  long res = 0;

  ASSERT (udst != NULL || size == 0);
  ASSERT (ksrc != NULL || size == 0);

  for (; size > 0; size--, res++)
    {
      /* Assumes that the user address has already been verified
         to be below PHYS_BASE. */
      if (!is_user_vaddr (udst + res))
        bad_user_access ();
      /* A memory access causes page fault. */
      if (!put_user (udst + res, ksrc[res]))
        bad_user_access ();
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
strncpy_from_user (char *kdst, const char *usrc_, size_t size)
{
  const uint8_t *usrc = (uint8_t *) usrc_;
	long res = 0;
  int byte;

  ASSERT (kdst != NULL);
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
      
		  kdst[res] = (char) byte;
		  if (!byte)
		  	return res;
    }

  kdst[--res] = '\0';
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
  char kstr[256];

  if (cmdline == NULL)
    bad_user_access ();

  strncpy_from_user (kstr, cmdline, 256);
  return process_execute (kstr);
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
  char kstr[256];
  bool res;

  if (file == NULL)
    bad_user_access ();

  strncpy_from_user (kstr, file, 256);

  lock_acquire (&fs_lock);
  res = filesys_create (kstr, initial_size);
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
  char kstr[256];
  bool res;

  if (file == NULL)
    bad_user_access ();

  strncpy_from_user (kstr, file, 256);

  lock_acquire (&fs_lock);
  res = filesys_remove (kstr);
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
lookup_fd (int fd_no)
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
  char kstr[256];

  if (file == NULL)
    return -1;

  strncpy_from_user (kstr, file, 256);

  if ((fd = malloc (sizeof (struct file_desc))) == NULL)
    return -1;

  lock_acquire (&fs_lock);
  if ((f = filesys_open (kstr)) == NULL)
    {
      lock_release (&fs_lock);
      free (fd);
      return -1;
    }
  lock_release (&fs_lock);

  fd->file = f;
  fd->no = cur->next_fd_no++;
  list_push_back (&cur->fd_list, &fd->fd_list_elem);

  return fd->no;
}

/* Returns the size, in bytes, of the open file FD_NO */
int
sys_filesize (int fd_no)
{
  struct file_desc *fd;
  int res;

  if ((fd = lookup_fd (fd_no)) == NULL)
    return -1;
  
  lock_acquire (&fs_lock);
  res = file_length (fd->file);
  lock_release (&fs_lock);

  return res;
}

/* Reads the data from opened file.  It returns the number of bytes
   actually read if FD_NO exists, or -­1 otherwise.  The UBUF is a
   destination address from which the SIZE-byte file contents are saved.
   When FD_NO is 0, it will read the data from the keyboard and save
   it into the UBUF by using input_getc(). */
int
sys_read (int fd_no, void *ubuf, unsigned size)
{
  struct file_desc *fd;
  int res = 0;

  if (ubuf == NULL)
    return -1;
  if (fd_no != STDIN_FILENO
      && (fd = lookup_fd (fd_no)) == NULL)
    return -1;
  
  if (fd_no != STDIN_FILENO)
    {
      char kbuf[256];
      int read_amount, bytes_read;
      
      /* Breaks up the BUFFER and reads up to 256-bytes at once. */
      while (size > 0)
        {
          /* Read is possible up to 256 bytes at once. */
          read_amount = (size > 256) ? 256 : size;

          lock_acquire (&fs_lock);
          bytes_read = file_read (fd->file, kbuf, read_amount);
          lock_release (&fs_lock); 

          /* Data read is saved in KBUF. */
          copy_to_user (ubuf + res, kbuf, bytes_read);

          /* If 0 bytes read, terminates the loop. */
          if (bytes_read == 0)
            break;

          /* Adjusts remaining size and next read position. */
          res += bytes_read;
          size -= bytes_read;
        }
    }
  else
    {
      for (; size > 0; size--, res++)
        {
          /* Assumes that the user address has already been verified
             to be below PHYS_BASE. */
          if (!is_user_vaddr (ubuf + res))
            bad_user_access ();
          /* A memory access causes page fault. */
          if (!put_user (ubuf + res, input_getc ()))
            bad_user_access ();
        }
    }
  return res;
}

/* Writes the data from UBUF to the open file FD_NO.  It returns
   the the number of bytes actually recoreded into the file if
   succeeds, or -­1 otherwise.  The SIZE is the number of bytes to be
   written.  When FD_NO is 1, it will write SIZE bytes from UBUF
   to the console. */
int
sys_write (int fd_no, const void *ubuf, unsigned size)
{
  struct file_desc *fd;
  int res = 0;

  if (ubuf == NULL)
    return -1;
  if (fd_no != STDOUT_FILENO
      && (fd = lookup_fd (fd_no)) == NULL)
    return -1;
  
  if (fd_no != STDOUT_FILENO)
    {
      char kbuf[256];
      int write_amount, bytes_written;
      
      /* Breaks up the BUFFER and writes up to 256-bytes at once. */
      while (size > 0)
        {
          /* Write is possible up to 256 bytes at once. */
          write_amount = (size > 256) ? 256 : size;
          
          /* Temporarily copies write data into kernel space. */
          copy_from_user (kbuf, ubuf + res, write_amount);

          lock_acquire (&fs_lock);
          bytes_written = file_write (fd->file, kbuf, write_amount);
          lock_release (&fs_lock);

          /* If 0 bytes written, terminates the loop. */
          if (bytes_written == 0)
            break;

          /* Adjusts remaining size and next read position. */
          res += bytes_written;
          size -= bytes_written;
        }
    }
  else
    {
      putbuf (ubuf, size);
      return size;
    }
  return res;
}

/* Changes the next byte to be read or written in open
   file FD_NO to POSITION. */
void
sys_seek (int fd_no, unsigned position)
{
  struct file_desc *fd;
  if ((fd = lookup_fd (fd_no)) == NULL)
    return;
  
  lock_acquire (&fs_lock);
  file_seek (fd->file, position);
  lock_release (&fs_lock);
}

/* Returns the position, in byte offset, of the file if
   FD exists.  If it fails, returns ­-1. */
unsigned
sys_tell (int fd_no)
{
  struct file_desc *fd;
  unsigned res;
  
  if ((fd = lookup_fd (fd_no)) == NULL)
    return -1;
  
  lock_acquire (&fs_lock);
  res = file_tell (fd->file);
  lock_release (&fs_lock);
  
  return res;
}

/* Closes the opened file with the given file descriptor FD_NO. */
void
sys_close (int fd_no)
{
  struct file_desc *fd;
  
  if ((fd = lookup_fd (fd_no)) == NULL)
    return;
  
  lock_acquire(&fs_lock);
  file_close (fd->file);
  lock_release (&fs_lock);

  list_remove (&fd->fd_list_elem);
  free (fd);
}

#ifdef VM
/* A mmap mapping. */
struct mmap
  {
    struct list_elem mmap_list_elem;   /* List element. */
    struct file *file;                 /* File. */
    mapid_t mapid;                     /* Mmap id. */
    
    /* A user virtual address from which mapping starts. */
    void *addr;
    /* Number of pages mmap'ed. */
    size_t pages;
  };

/* Finds a file descriptor with the given FD_NO.
   If not found, returns NULL. */
static struct mmap *
lookup_mmap (mapid_t mapid)
{
  struct thread *cur = thread_current ();
  struct list *mmap_list = &cur->mmap_list;
  struct list_elem *e;
  for (e = list_begin (mmap_list); e != list_end (mmap_list);
       e = list_next (e))
    {
      struct mmap *m
        = list_entry (e, struct mmap, mmap_list_elem);
      if (m->mapid == mapid)
        return m;
    }
  return NULL;
}

static void do_munmap (struct mmap *, bool);

/* Maps the file open as FD_NO into the process's virtual
   address space.  The entire file is mapped into consecutive
   virtual pages starting at ADDR.  If the file's length is
   not a multiple of PGSIZE, then some bytes in a page is
   filled with zeros.
   
   If successful, this function returns a mapping id that
   uniquely indentifies the mmap mapping within the process.
   On failure, it returns -1 and the process's mappings are
   unchanged.

   A call to mmap() may fail if the file open as FD_NO has a
   length of zero bytes.  It must fail if ADDR is not
   "page-aligned" or if the range of pages mapped overlaps any
   existing set of mapped pages, including the stack or pages
   mapped at executable load time.  It must also fail if
   ADDR is 0.  Finally, file descriptors 0 and 1, representing
   console input and output, are not mappable.

   Closing or removing a file does not unmap any of its mappings.
   Once created, a mapping is valid until munmap() is called
   or the process exits, following the Unix convention.
   We used the file_reopen() function to obtain a separate and
   independent reference to the file for each of its mappings. */
mapid_t
sys_mmap (int fd_no, void *addr)
{
  struct thread *cur = thread_current ();
  struct file_desc *fd;
  struct file *f;
  struct mmap *m;
  size_t size;
  off_t ofs = 0;

  if (fd_no == STDIN_FILENO || fd_no == STDOUT_FILENO)
    return -1;
  if (addr == NULL || pg_ofs (addr) != 0)
    return -1;
  if ((fd = lookup_fd (fd_no)) == NULL)
    return -1;
  if ((m = malloc (sizeof (struct mmap))) == NULL)
    return -1;
  
  lock_acquire (&fs_lock);
  if ((f = file_reopen (fd->file)) == NULL)
    {
      lock_release (&fs_lock);
      free (m);
      return -1;
    }
  lock_release (&fs_lock);

  m->file = f;
  m->mapid = cur->next_mapid++;
  m->addr = addr;
  m->pages = 0;
  list_push_back (&cur->mmap_list, &m->mmap_list_elem);

  lock_acquire (&fs_lock);
  size = file_length (m->file);
  lock_release (&fs_lock);

  if (size == 0)
    return -1;

  while (size > 0)
    {
      /* Calculate how to fill this page.
         We will read PAGE_READ_BYTES bytes from the file F
         and zero the final PAGE_ZERO_BYTES bytes. */
      size_t page_read_bytes = (size > PGSIZE) ? PGSIZE : size;
      size_t page_zero_bytes = PGSIZE - page_read_bytes;
      struct page *p;

      /* If the range of pages mapped overlaps any existing set
         of user virtual pages, mmap() fails. */
      if ((p = page_make_entry (addr)) == NULL)
        goto munmap;
      m->pages++;

      p->type = PG_FILE;
      p->writable = true;

      p->file = f;
      p->read_bytes = page_read_bytes;
      p->zero_bytes = page_zero_bytes;
      p->file_ofs = ofs;

      size -= page_read_bytes;
      addr += PGSIZE;
      ofs  += page_read_bytes;
    }  

  return m->mapid;

 munmap:
  do_munmap (m, false);
  return -1;
}

/* Unmaps the mapping designated by MAPID, which must be a
   mapping id returned by a previous call to mmap() by the
   same process that has not yet been unmapped. */
void
sys_munmap (mapid_t mapid)
{
  struct mmap *m;
  if ((m = lookup_mmap (mapid)) == NULL)
    return;
  do_munmap (m, true);
}

/* Performs a core functionality of munmap().  It first closes
   the open file and removes mmap entry M from process's mapping
   list.  Then, it writes back every user virtual page to the
   mmapped file only if WRITE is true "and" the user page is dirty.
   
   Every SPTE and physical frame allocated to this SPTE, if any,
   are removed and freed.  */
static void
do_munmap (struct mmap* m, bool write)
{
  struct thread *cur = thread_current ();
  struct page *p;
  void *upage;

  ASSERT (m != NULL);

  /* For each mmap'ed page, */
  for (upage = m->addr; upage < m->addr + PGSIZE * m->pages;
       upage += PGSIZE)
    {
      p = page_lookup (upage);

      ASSERT (p != NULL);
      ASSERT (p->file == m->file);

      /* Here, UPAGE could have been evicted; the current process
         does not have any virtual mapping for UPAGE.  The function
         pagedir_is_dirty() returns false, if a page is dirty or
         "there is no mapping" between UPAGE and a physical frame. */
      p->dirty |= pagedir_is_dirty (cur->pagedir, p->upage);

      if (write && p->dirty) 
        {
          /* Write back the page's contents. */
          lock_acquire (&fs_lock);
          file_write_at (p->file, p->upage, p->read_bytes, p->file_ofs);
          lock_release (&fs_lock);
        }
      page_remove_entry (p);
    }
  
  lock_acquire (&fs_lock);
  file_close (m->file);
  lock_release (&fs_lock);
  
  list_remove (&m->mmap_list_elem);
  free (m);
}
#endif

/* Closes all opened files of the current process. */
void
sys_fd_exit (void)
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

#ifdef VM
/* Unmaps all mmap mappings.
   All mappings are implicitly unmapped when a process exits,
   whether via exit or by any other means.  When a mapping is
   unmapped, whether implicitly or explicitly, all pages written
   to by the process are written back to the file, and pages
   not written must not be.  The pages are then removed from
   the process's list of virtual pages. */
void
sys_mmap_exit (void)
{
  struct thread *cur = thread_current ();
  struct list *mmap_list = &cur->mmap_list;
  while (!list_empty (mmap_list))
    {
      struct mmap *m
        = list_entry (list_front (mmap_list), struct mmap,
                      mmap_list_elem);
      do_munmap (m, true);
    }
}
#endif

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

#ifdef VM
static void
sys_mmap_wrapper (struct intr_frame *f)
{
  sys_param_type ARG0, ARG1;
  SYSCALL_GET_ARGS2 (f->esp, &ARG0, &ARG1);
  f->eax = sys_mmap ((int) ARG0, (void *) ARG1);
}

static void
sys_munmap_wrapper (struct intr_frame *f)
{
  sys_param_type ARG0;
  SYSCALL_GET_ARGS1 (f->esp, &ARG0);
  sys_munmap ((mapid_t) ARG0);
}
#endif

/* Handles invalid user-provided pointer access. */
static void
bad_user_access (void)
{
  sys_exit (-1);
}
