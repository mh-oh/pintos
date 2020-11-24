#include "vm/frame.h"
#include <debug.h>
#include <stdio.h>
#include "threads/thread.h"// debug purpose?
#include "threads/malloc.h"
#include "threads/palloc.h"
#include "threads/synch.h"

static struct lock table_lock;   /* Mutual exclusion. */
static struct list frame_list;   /* Mapped frames (list). */

void
frame_init (void)
{
  lock_init (&table_lock);
  list_init (&frame_list);

  printf ("##### [%d] (frame_init) init complete.\n", thread_tid ());
}

void *
frame_alloc (enum palloc_flags flags)
{
  struct thread *cur = thread_current ();
  struct frame *f;
  ASSERT (flags & PAL_USER);

  lock_acquire (&table_lock);

  if ((f = malloc (sizeof (struct frame))) == NULL)
    PANIC ("cannot allocate frame table entry.");
  lock_init (&f->lock);

  f->kpage = palloc_get_page (flags);
  if (f->kpage != NULL)
    {
      printf ("##### [%d] (frame_alloc) kpage=%p is palloced to thread %d\n", thread_tid (), f->kpage, cur->tid);
      printf ("##### [%d] (frame_alloc) malloced ft entry %p for kpage=%p\n", thread_tid (), f, f->kpage);
      f->owner = cur;
      lock_acquire (&f->lock);
      printf ("##### [%d] (frame_alloc) locked ft entry %p for kpage=%p\n", thread_tid (), f, f->kpage);

      list_push_back (&frame_list, &f->list_elem);
      lock_release (&table_lock);

      return f->kpage;
    }
  else
    {
      printf ("##### [%d] (frame_alloc) evicting a frame...\n", thread_tid ());
      free (f);
      f = frame_get_victim ();
      list_remove (&f->list_elem);
      f->owner = cur;
      list_push_back (&frame_list, &f->list_elem);
      lock_release (&table_lock);
      PANIC ("not yet.");
      return f->kpage;
    }
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

void
frame_free_all (void)
{
  struct thread *cur = thread_current ();
  struct list_elem *e;
  lock_acquire (&table_lock);
  for (e = list_begin (&frame_list); e != list_end (&frame_list);
       /**/)
    {
      struct frame *f
        = list_entry (e, struct frame, list_elem);
      if (f->owner == cur)
        {
          //printf ("##### [%d] (frame_free_all) ft entry %p for kpage=%p is freed\n", thread_tid (), f, f->kpage);
          e = list_remove (e);
          free (f);
        }
      else
        e = list_next (e);
    }
  lock_release (&table_lock);
}

struct frame *
frame_lookup (void *kpage)
{
  ASSERT (lock_held_by_current_thread (&table_lock));
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
  ASSERT (lock_held_by_current_thread (&table_lock));
  ASSERT (!list_empty (&frame_list));

  struct thread *cur = thread_current ();
  struct list_elem *e;
  for (e = list_begin (&frame_list); e != list_end (&frame_list);
       e = list_next (e))
    {
      struct frame *f
        = list_entry (e, struct frame, list_elem);
      if (f->owner->tid == cur->tid)
        continue;
      if (!lock_try_acquire (&f->lock))
        continue;
      return f;
    }
  NOT_REACHED ();
}

void
frame_table_lock (void)
{
  lock_acquire (&table_lock);
}

void
frame_table_unlock (void)
{
  lock_release (&table_lock);
}

void
frame_unlock (void *kpage)
{
  struct frame *f;
  lock_acquire (&table_lock);
  f = frame_lookup (kpage);
  ASSERT (lock_held_by_current_thread (&f->lock));
  printf ("##### [%d] (frame_unlock) unlocked ft entry %p for kpage=%p\n", thread_tid (), f, f->kpage);
  lock_release (&f->lock);
  lock_release (&table_lock);
}