#include "vm/frame.h"
#include <debug.h>
#include <stdio.h>
#include "threads/thread.h"// debug purpose?
#include "threads/malloc.h"
#include "threads/palloc.h"
#include "threads/synch.h"

static struct lock frame_lock;   /* Mutual exclusion. */
static struct list frame_list;   /* Mapped frames (list). */

void
frame_init (void)
{
  lock_init (&frame_lock);
  list_init (&frame_list);

  //printf ("##### (frame_init) init complete.\n");
}

void *
frame_alloc (enum palloc_flags flags)
{
  struct thread *cur = thread_current ();
  struct frame *f;
  ASSERT (flags & PAL_USER);

  if ((f = malloc (sizeof (struct frame))) == NULL)
    PANIC ("cannot allocate frame table entry.");
  
  f->kpage = palloc_get_page (flags);
  if (f->kpage != NULL)
    {
      //printf ("##### (frame_alloc) kpage=%p is palloced to thread %d\n", f->kpage, cur->tid);
      f->owner = cur;
      list_push_back (&frame_list, &f->list_elem);
      return f->kpage;
    }
  else
    PANIC ("eviction required.");
}

void
frame_free (void *kpage)
{
  struct frame *f;
  ASSERT (kpage != NULL);

  if ((f = frame_lookup (kpage)) == NULL)
    return;

  list_remove (&f->list_elem);
  palloc_free_page (f->kpage);
  free (f);
}

struct frame *
frame_lookup (void *kpage)
{
  ASSERT (lock_held_by_current_thread (&frame_lock));
  struct list_elem *e;
  for (e = list_begin (&frame_list); e != list_end (&frame_list);
       e = list_next (e))
    {
      struct frame *f
        = list_entry (e, struct frame, list_elem);
      if (f->kpage == kpage)
        return f;
    }
  return NULL;
}

struct frame *
frame_get_victim (void)
{
  ASSERT (!list_empty (&frame_list));
  return list_entry (list_front (&frame_list),
                     struct frame, list_elem);
}