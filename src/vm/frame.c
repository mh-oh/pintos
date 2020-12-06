#include "vm/frame.h"
#include <list.h>
#include <debug.h>
#include "threads/palloc.h"
#include "threads/synch.h"

/* Mutual exclusion. */
static struct lock table_lock;

/* List of allocated frames, frame table (FT). */
static struct list frame_list;

/* Initializes the frame allocatior.
   All allocated frames are stored in the FRAME_LIST and
   managed globally. */
void
frame_init (void)
{
  lock_init (&table_lock);
  list_init (&frame_list);
}

/* Obtains a single free physical frame and returns a frame
   table entry which is associated with the kernel virtual address
   identifying the frame.
   All frames are obtained from the user pool.
   If too few pages are available, "NOT IMPLEMENTED YET".*/
struct frame *
frame_alloc (void)
{
  struct frame *f = malloc (sizeof (struct frame));
  if (!f)
    PANIC ("cannot allocate a frame table entry.");

  f->kpage = palloc_get_page (PAL_USER);
  if (f->kpage != NULL)
    {
      lock_acquire (&table_lock);
      list_push_back (&frame_list, &f->list_elem);
      lock_release (&table_lock);
      return f;
    }
  else
    PANIC ("not implemented yet. eviction.");
}

/* Removes a frame table entry F from the table and frees it.
   Importantly, this function does not free the actual
   physical frame associated with F because this frame should be
   deallocated by pagedir_destroy() when a process exits. */
void
frame_free (struct frame *f)
{
  ASSERT (f != NULL);
  list_remove (&f->list_elem);
  free (f);
}