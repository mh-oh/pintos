#include "vm/frame.h"
#include "vm/page.h"
#include "vm/swap.h"
#include <list.h>
#include <debug.h>
#include "threads/palloc.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "userprog/pagedir.h"

/* Mutual exclusion. */
static struct lock table_lock;

/* List of allocated frames, frame table (FT). */
static struct list frame_list;

/* An iterator pointing to the last examined FTE in the
   frame table. */
static struct list_elem *hand;

/* Mutual exclusion during pin or unpin. */
static struct lock pinning;

/* Initializes the frame allocatior.
   All allocated frames are stored in the FRAME_LIST and
   managed globally. */
void
frame_init (void)
{
  lock_init (&table_lock);
  list_init (&frame_list);
  hand = NULL;

  lock_init (&pinning);
}

static struct frame *frame_advance_hand (void);
static struct frame *frame_get_victim (void);
static void frame_do_eviction (struct page *src, struct page *dst);

/* Obtains a single free physical frame and returns a FTE
   corresponding to the kernel virtual address identifying the
   frame obtained from the user pool.
   If too few pages are available, some frame is evicted.
   
   P's FRAME member is also set to the returned FTE. */
struct frame *
frame_alloc (struct page *p)
{
  struct frame *f;
  void *kpage;

  lock_acquire (&table_lock);

  kpage = palloc_get_page (PAL_USER);
  if (kpage != NULL)
    {
      f = malloc (sizeof (struct frame));
      if (f == NULL)
        PANIC ("cannot allocate a frame table entry.");

      /* One-to-one correspondence. */
      f->kpage = kpage;

      /* Doubly linked. */
      f->page = p;
      f->page->frame = f;
      
      list_push_back (&frame_list, &f->list_elem);
    }
  else
    {
      f = frame_get_victim ();
      frame_do_eviction (f->page, p);
    }

  lock_release (&table_lock);
  return f;
}

/* Circularly advances the iterator HAND. */
static struct frame *
frame_advance_hand (void)
{
  if (hand == NULL)
    hand = list_begin (&frame_list);
  else
    {
      hand = list_next (hand);
      if (hand == list_end (&frame_list))
        hand = list_begin (&frame_list);
    }
  return list_entry (hand, struct frame, list_elem);
}

/* Selects a victim physical frame and returns the
   corresponding FTE. */
static struct frame *
frame_get_victim (void)
{
  ASSERT (lock_held_by_current_thread (&table_lock));
  ASSERT (!list_empty (&frame_list));
  /*
  struct frame *f;
  while ((f = frame_advance_hand ()))
    {
      if (!f->page)
        continue;
      if (page_was_accessed (f->page))
        continue;

      list_remove (&f->list_elem);
      return f;
    }
  NOT_REACHED ();
  */
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

/* Performs frame eviction.
   Deprives SRC of its FTE and pyhsical frame, and gives them
   to DST.
   The SPTE SRC must have FTE and physical frame allocated to
   it, and the FTE and SRC must point to each other.
   DST must not have FTE and physical frame allocated to it. */
static void
frame_do_eviction (struct page *src, struct page *dst)
{
  ASSERT (src != NULL);
  ASSERT (src->frame != NULL);
  ASSERT (src->frame->page == src);

  ASSERT (dst != NULL);
  ASSERT (dst->frame == NULL);
  ASSERT (dst->owner == thread_current ());

  ASSERT (lock_held_by_current_thread (&table_lock));

  struct frame *f = src->frame;

  /* Remove a victim FTE from the table.
     It will be pushed back to the table at the end of
     this procedure. */
  list_remove (&f->list_elem);

  /* Check if the contents of the page to which the victim FTE
     was allocated has been changed.  Then, remove the
     corresponding virtual mapping.
     
     If the contents has been changed at least once, it should
     be backed up to swap slot whenever future eviction occurs. */
  pagedir_clear_page (src->owner->pagedir, src->upage);
  src->dirty |= pagedir_is_dirty (src->owner->pagedir, src->upage);

  if (src->dirty)
    {
      /* Save the previous contents to the swap slot and
         re-initializes supplemental information for later
         page fault handling. */
      src->slot = swap_out (f->kpage);
      src->type = PG_SWAP;
    }
  
  /* Transfer the victim frame (doubly linked). */
  f->page = dst;
  f->page->frame = f;

  /* Remove the frame from SRC. */
  src->frame = NULL;

  list_push_back (&frame_list, &f->list_elem);
}

/* Removes a frame table entry F from the table and frees it.
   Importantly, this function does not free the actual
   physical frame corresponding to F, because this frame will
   be deallocated by pagedir_destroy() when a process exits. */
void
frame_free (struct frame *f)
{
  ASSERT (f != NULL);
  lock_acquire (&table_lock);
  list_remove (&f->list_elem);
  free (f);
  lock_release (&table_lock);
}

/* Atomically tries to pin F and returns true if successful or false
   on failure. */
bool
frame_try_pin (struct frame *f)
{
  ASSERT (f != NULL);
  bool success;

  lock_acquire (&pinning);
  success = !f->pinned;
  f->pinned = true;
  lock_release (&pinning);
  
  return success;
}

/* Atomically unpins F. */
void
frame_unpin (struct frame *f) 
{
  ASSERT (f != NULL);
  lock_acquire (&pinning);
  f->pinned = false;
  lock_release (&pinning);
}