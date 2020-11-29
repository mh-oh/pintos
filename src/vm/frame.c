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

void
frame_init (void)
{
  lock_init (&table_lock);
  list_init (&frame_list);

  //printf ("##### [%d] (frame_init) init complete.\n", thread_tid ());
}

static struct frame *frame_get_victim (void);
static void frame_do_eviction (struct frame *v, struct page *p);

struct frame *
frame_alloc (enum palloc_flags flags, struct page *p)
{
  struct frame *f;

  ASSERT (flags & PAL_USER);
  ASSERT (p != NULL);
  ASSERT (p->owner == thread_current ());

  lock_acquire (&table_lock);
  //printf ("##### [%d] (frame_alloc) table_lock is locked by thread %d\n", thread_tid (), thread_tid ());

  if ((f = malloc (sizeof (struct frame))) == NULL)
    PANIC ("cannot allocate frame table entry.");
  lock_init (&f->lock);

  f->kpage = palloc_get_page (flags);
  if (f->kpage != NULL)
    {
      //printf ("##### [%d] (frame_alloc) kpage=%p is palloced to thread %d\n", thread_tid (), f->kpage, p->owner->tid);
      //printf ("##### [%d] (frame_alloc) malloced ft entry %p for kpage=%p\n", thread_tid (), f, f->kpage);
      lock_acquire (&f->lock);
      f->owner = p->owner;
      f->suppl = p;
      //printf ("##### [%d] (frame_alloc) locked ft entry %p for kpage=%p\n", thread_tid (), f, f->kpage);

      list_push_back (&frame_list, &f->list_elem);
      //printf ("##### [%d] (frame_alloc) table_lock is unlocked by thread %d\n", thread_tid (), thread_tid ());
      lock_release (&table_lock);

      return f;
    }
  else
    {
      /* Gets a FTE associated with victim physical frame. */
      free (f);
      f = frame_get_victim ();
      ASSERT (f->suppl != NULL);
      //printf ("##### [%d] (frame_do_eviction) victim %p: f->owner=%d, f->suppl=%p, f->suppl->owner=%d\n", thread_tid (), f, f->owner->tid, f->suppl, f->suppl->owner->tid);
      ASSERT (f->owner == f->suppl->owner);
      
      /* Performs eviction.
         Gives an existing frame f to the new SPTE p. */
      frame_do_eviction (f, p);
      //printf ("##### [%d] (frame_alloc) table_lock is unlocked by thread %d\n", thread_tid (), thread_tid ());
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
      if (f->owner->tid == cur->tid)
        {
          if (lock_held_by_current_thread (&f->lock))
            continue;
        }
      if (!lock_try_acquire (&f->lock))
        continue;
      return f;
    }
  NOT_REACHED ();
}

static void
frame_do_eviction (struct frame *v, struct page *p)
{
  ASSERT (v != NULL && p != NULL);
  ASSERT (p->owner == thread_current ());
  ASSERT (lock_held_by_current_thread (&table_lock));

  struct page *p_from = v->suppl;
  bool dirty;

  //printf ("##### [%d] (frame_do_eviction) evicting a frame...\n", thread_tid ());
  /* Removes a victim FTE from FT. */
  list_remove (&v->list_elem);

  /* Checks whether the victim RAM page is dirty and then
     removes virtual mapping. */
  pagedir_clear_page (p_from->owner->pagedir, p_from->upage);
  dirty = pagedir_is_dirty (p_from->owner->pagedir, p_from->upage);

  /* The victim frame has been modified. */
  //if (dirty)
  //  {
      /* Saves the previous contents to the swap slot and
         Re-initializes supplemental information for later
         page fault handling. */
      v->suppl->slot = swap_out (v->kpage);
      v->suppl->type = PG_SWAP;
      v->suppl->file = NULL;
      //printf ("##### [%d] (frame_do_eviction) swapped out: kpage=%p of thread %d into slot=%d\n", thread_tid (), v->kpage, v->owner->tid, v->suppl->slot);
  //  }

  v->suppl->frame = NULL;

  //printf ("##### [%d] (frame_do_eviction) changed owner of kpage=%p: thread %d to %d\n", thread_tid (), v->kpage, v->owner->tid, thread_tid ());

  /* Changes owner. */
  v->owner = p->owner;
  v->suppl = p;

  list_push_back (&frame_list, &v->list_elem);
}





void
frame_free (struct frame *f)
{
  ASSERT (f != NULL);
  ASSERT (lock_held_by_current_thread (&f->lock));
  list_remove (&f->list_elem);
  free (f);
}

void
frame_free_all (void)
{
  struct thread *cur = thread_current ();
  struct list_elem *e;
  lock_acquire (&table_lock);
  //printf ("##### [%d] (frame_free_all)\n", cur->tid);
  //for (e = list_begin (&frame_list); e != list_end (&frame_list);
  //     /**/)
  //  {
  //    struct frame *f
  //      = list_entry (e, struct frame, list_elem);
  //    if (f->owner == cur)
  //      {
  //        //printf ("##### [%d] (frame_free_all) ft entry %p for kpage=%p is freed\n", thread_tid (), f, f->kpage);
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
  ASSERT (lock_held_by_current_thread (&f->lock));

  lock_acquire (&table_lock);
  //printf ("##### [%d] (frame_unlock) f=%p, f->owner=%d, f->suppl=%p, f->suppl->owner=%d\n", thread_tid (), f, f->owner->tid, f->suppl, f->owner->tid);
  //printf ("##### [%d] (frame_unlock) unlocked ft entry %p for kpage=%p\n", thread_tid (), f, f->kpage);
  lock_release (&f->lock);
  lock_release (&table_lock);
}