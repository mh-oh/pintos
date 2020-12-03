#include "vm/page.h"
#include "vm/frame.h"
#include "vm/swap.h"
#include <string.h>
#include <debug.h>
#include <stdio.h>
#include <bitmap.h>
#include "threads/thread.h"
#include "threads/malloc.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"

static unsigned page_hash_func (const struct hash_elem *, void *);
static bool page_hash_less (const struct hash_elem *, const struct hash_elem *, void *);
static void page_hash_free (struct hash_elem *, void *);

struct hash *
page_create_spt (void)
{
  struct hash *spt = malloc (sizeof (struct hash));
  if (!spt)
    PANIC ("cannot create supplemental page table.");
  hash_init (spt, page_hash_func, page_hash_less, NULL);
  printf ("##### [%d] (page_create_spt) spt=%p is malloced.\n", thread_tid (), spt);
  return spt;
}

void
page_hash_action (struct hash_elem *e, void *aux)
{
  struct page *p = hash_entry (e, struct page, hash_elem);
  printf ("p=%p, p->upage=%p\n", p, p->upage);
}

void
page_destroy_spt (struct hash *spt)
{
  ASSERT (spt != NULL);
  printf ("##### [%d] (page_destroy_spt) spt size is %d\n", thread_tid (), hash_size (spt));
  //hash_apply (spt, page_hash_action);
  hash_destroy (spt, page_hash_free);
  //free (spt);
  printf ("##### [%d] (page_destroy_spt) spt=%p is freed.\n", thread_tid (), spt);
}

static unsigned
page_hash_func (const struct hash_elem *e, void *aux UNUSED)
{
  struct page *p = hash_entry (e, struct page, hash_elem);
  return hash_int ((int) p->upage);
}

static bool
page_hash_less (const struct hash_elem *a_,
                const struct hash_elem *b_,
                void *aux UNUSED)
{
  struct page *a = hash_entry (a_, struct page, hash_elem);
  struct page *b = hash_entry (b_, struct page, hash_elem);
  return a->upage < b->upage;
}

static void
page_hash_free (struct hash_elem *e, void *aux UNUSED)
{
  struct page *p = hash_entry (e, struct page, hash_elem);
  struct frame *f = p->frame;
  if (f != NULL)
    {
      lock_acquire (&f->lock);
      if (f == p->frame)
        frame_free (f);
      else
        {
          ASSERT (p->frame == NULL);
          lock_release (&f->lock);
        }
    }

  //free (p);
  
  printf ("##### [%d] (page_hash_free) p=%p is freed.\n", thread_tid (), p);
}

struct page *
page_make_entry (void *upage)
{
  struct thread *cur = thread_current ();
  struct page *p;

  ASSERT (is_user_vaddr (upage));
  ASSERT (pg_ofs (upage) == 0);

  if (page_lookup (upage))
    return NULL;
  if ((p = malloc (sizeof (struct page))) == NULL)
    PANIC ("cannot create supplemental page table entry.");
  
  p->upage = upage;
  p->frame = NULL;
  p->owner = cur;
  p->type = PG_UNKNOWN;
  p->file = NULL;
  p->slot = BITMAP_ERROR;
  p->dirty = false;

  hash_insert (cur->spt, &p->hash_elem);
  printf ("##### [%d] (page_make_entry) p=%p is malloced to load upage=%p. spt size is %d\n", thread_tid (), p, p->upage, hash_size (cur->spt));
  return p;
}

void
page_remove_entry (struct page *p)
{
  ASSERT (p->owner == thread_current ());
  ASSERT (p != NULL);
  struct frame *f = p->frame;
  if (f != NULL)
    {
      lock_acquire (&f->lock);
      if (f == p->frame)
        frame_free (f);
      else
        {
          ASSERT (p->frame == NULL);
          lock_release (&f->lock);
        }
    }
  hash_delete (p->owner->spt, &p->hash_elem);
  free (p);
}

static bool install_page (void *upage, void *kpage, bool writable);

bool
page_load (void *upage)
{
  ASSERT (is_user_vaddr (upage));
  ASSERT (pg_ofs (upage) == 0);

  struct page *p = page_lookup (upage);
  if (!p)
    return false;
  ASSERT (p->type != PG_UNKNOWN);

  //ASSERT (p->frame == NULL);

  printf ("##### [%d] (page_load) loading... p=%p, p->type=%d, p->upage=%p\n", thread_tid (), p, p->type, p->upage);
  struct frame *f = p->frame = frame_alloc (PAL_USER, p);
  switch (p->type)
    {
    case PG_FILE:
      ASSERT (p->type == PG_FILE && p->file != NULL);
      ASSERT (p->type == PG_FILE && p->slot == BITMAP_ERROR);
      size_t read_bytes
        = file_read_at (p->file, f->kpage, p->read_bytes, p->file_ofs);
      if (read_bytes != p->read_bytes)
        goto fail;
      memset (f->kpage + p->read_bytes, 0, p->zero_bytes);
      break;
    
    case PG_SWAP:
      //ASSERT (p->type == PG_SWAP && p->file == NULL);
      ASSERT (p->type == PG_SWAP && p->slot != BITMAP_ERROR);
      swap_in (f->kpage, p->slot);
      //p->type = p->prev;
      p->slot = BITMAP_ERROR;
      break;
    
    case PG_ZERO:
      ASSERT (p->type == PG_ZERO && p->file == NULL);
      ASSERT (p->type == PG_ZERO && p->slot == BITMAP_ERROR);
      memset (f->kpage, 0, PGSIZE);
      break;

    case PG_UNKNOWN:
      PANIC ("unknown page type.");

    default:
      NOT_REACHED ();
    }

  printf ("##### [%d] (page_load) complete... p=%p, p->frame->page=%p, p->type=%d, p->frame=%p, f->frame->kpage=%p\n", thread_tid (), p, p->frame->page, p->type, p->frame, p->frame->kpage);

  if (!install_page (upage, f->kpage, p->writable)) 
    goto fail;

  //frame_unpin (f);
  frame_unlock (f);
  return true;

 fail:
  frame_free (f);
  return false;
}

static bool
install_page (void *upage, void *kpage, bool writable)
{
  struct thread *t = thread_current ();

  /* Verify that there's not already a page at that virtual
     address, then map our page there. */
  return (pagedir_get_page (t->pagedir, upage) == NULL
          && pagedir_set_page (t->pagedir, upage, kpage, writable));
}

struct page *
page_lookup (void *upage)
{
  struct thread *cur = thread_current ();
  struct page key;
  struct hash_elem *e;

  ASSERT (cur->spt != NULL);
  ASSERT (is_user_vaddr (upage));
  ASSERT (pg_ofs (upage) == 0);

  key.upage = upage;
  e = hash_find (cur->spt, &key.hash_elem);

  return e != NULL ? hash_entry (e, struct page, hash_elem) : NULL;
}