#include "vm/frame.h"
#include "vm/page.h"
#include "vm/swap.h"
#include <debug.h>
#include <stdio.h>
#include <list.h>
#include "threads/thread.h"// debug purpose?
#include "threads/malloc.h"
#include "threads/palloc.h"
#include "threads/synch.h"
#include "userprog/pagedir.h"

static struct lock table_lock;   /* Mutual exclusion. */
static struct list frame_list;   /* Mapped frames (list). */

static struct lock mutex_lock;

void
frame_init (void)
{
  lock_init (&table_lock);
  list_init (&frame_list);

  lock_init (&mutex_lock);
}

static struct frame *frame_get_victim (void);
static void frame_do_eviction (struct page *src, struct page *dst);

struct frame *
frame_alloc (enum palloc_flags flags, struct page *p)
{
  struct frame *f;

  ASSERT (flags & PAL_USER);
  ASSERT (p != NULL);
  ASSERT (p->owner == thread_current ());

  lock_acquire (&table_lock);

  if ((f = malloc (sizeof (struct frame))) == NULL)
    PANIC ("cannot allocate frame table entry.");
  //lock_init (&f->lock);

  f->kpage = palloc_get_page (flags);
  if (f->kpage != NULL)
    {
      printf ("##### [%d] (frame_alloc) f=%p is malloced. f->kpage=%p\n", thread_tid (), f, f->kpage);
      //lock_acquire (&f->lock);
      frame_try_pin (f);

      f->__owner = p->owner;
      f->page = p;

      list_push_back (&frame_list, &f->list_elem);
      lock_release (&table_lock);

      return f;
    }
  else
    {
      /* Gets a FTE associated with a victim physical frame. */
      free (f);
      f = frame_get_victim ();
      ASSERT (f->page != NULL);
      ASSERT (f->__owner == f->page->owner);
      
      /* Performs eviction.
         Gives an existing frame `f' to the new SPTE `p'. */
      //frame_do_eviction (f, p);
      frame_do_eviction (f->page, p);
      printf ("##### [%d] (frame_alloc) table_lock is unlocked by thread %d\n", thread_tid (), thread_tid ());
      lock_release (&table_lock);
      return f;
    }
}

static struct frame *
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
      if (!frame_try_pin (f))
        continue;
      return f;
    }
  NOT_REACHED ();
}

static void
frame_do_eviction (struct page *src, struct page *dst)
{
  /**/
  ASSERT (src != NULL);
  ASSERT (src->frame != NULL);
  ASSERT (src->frame->page == src);

  /**/
  ASSERT (dst != NULL);
  ASSERT (dst->frame == NULL);
  ASSERT (dst->owner == thread_current ());

  ASSERT (lock_held_by_current_thread (&table_lock));

  struct frame *f = src->frame;

  printf ("##### [%d] (frame_do_eviction) victim f=%p: f->owner=%d, f->page=%p, f->page->owner=%d, f->page->type=%d\n", thread_tid (), f, f->__owner->tid, f->page, f->page->owner->tid, f->page->type);

  /* Removes a victim frame from the table.
     It will be pushed back to the table at the end of
     this procedure. */
  list_remove (&f->list_elem);

  /* Checks whether the victim page is dirty, and then
     removes the virtual mapping. */
  pagedir_clear_page (src->owner->pagedir, src->upage);
  src->dirty |= pagedir_is_dirty (src->owner->pagedir, src->upage);

  if (src->dirty)
    {
      /* Saves the previous contents to the swap slot and
         re-initializes supplemental information for later
         page fault handling. */
      src->slot = swap_out (f->kpage);
      src->type = PG_SWAP;
    }
  
  /* Transfer. */
  src->frame->page = dst;
  src->frame = NULL;

  /* Changes owner. */
  f->__owner = dst->owner;

  list_push_back (&frame_list, &f->list_elem);
}






void
frame_free (struct frame *f)
{
  ASSERT (f != NULL);
  //ASSERT (lock_held_by_current_thread (&f->lock));
  list_remove (&f->list_elem);
  //free (f);
  printf ("##### [%d] (frame_free) f=%p is freed.\n", thread_tid (), f);
}

void
frame_free_all (void)
{
  struct thread *cur = thread_current ();
  struct list_elem *e;
  lock_acquire (&table_lock);
  //for (e = list_begin (&frame_list); e != list_end (&frame_list);
  //     /**/)
  //  {
  //    struct frame *f
  //      = list_entry (e, struct frame, list_elem);
  //    if (f->__owner == cur)
  //      {
  //        printf ("##### [%d] (frame_free_all) ft entry %p for kpage=%p is freed\n", thread_tid (), f, f->kpage);
  //        e = list_remove (e);
  //        free (f);
  //      }
  //    else
  //      e = list_next (e);
  //  }
  lock_release (&table_lock);
}

void
frame_unlock (struct frame *f)
{
  ASSERT (f != NULL);
  //ASSERT (lock_held_by_current_thread (&f->lock));

  lock_acquire (&mutex_lock);
  printf ("##### [%d] (frame_unlock) f=%p, f->__owner=%d, f->page=%p, f->page->owner=%d\n", thread_tid (), f, f->__owner->tid, f->page, f->__owner->tid);
  printf ("##### [%d] (frame_unlock) unlocked ft entry %p for kpage=%p\n", thread_tid (), f, f->kpage);
  //lock_release (&f->lock);
  f->pinned = false;
  lock_release (&mutex_lock);
}

/*
bool
frame_test_and_pin (struct frame *f)
{
  ASSERT (f != NULL);
  bool res;
  lock_acquire (&mutex_lock);
  res = f->pinned;
  f->pinned = true;
  lock_release (&mutex_lock);
  return res;
}

bool
frame_test_and_unpin (struct frame *f) 
{
  ASSERT (f != NULL);
  bool res;
  lock_acquire (&mutex_lock);
  res = f->pinned;
  f->pinned = false;
  lock_release (&mutex_lock);
  return res;
}
*/

bool
frame_try_pin (struct frame *f)
{
  bool success;
  lock_acquire (&mutex_lock);
  success = f != NULL && !f->pinned;
  if (success)
    {
      printf ("##### [%d] (frame_try_pin) f=%p is pinned\n", thread_tid (), f);
      f->pinned = true;
    }
  else
    printf ("##### [%d] (frame_try_pin) f=%p was already pinned\n", thread_tid (), f);
  lock_release (&mutex_lock);
  return success;
}

void
frame_unpin (struct frame *f) 
{
  ASSERT (f != NULL);
  lock_acquire (&mutex_lock);
  f->pinned = false;
  printf ("##### [%d] (frame_unpin) f=%p is unpinned.\n", thread_tid (), f);
  lock_release (&mutex_lock);
}