#include "vm/page.h"
#include "vm/frame.h"
#include <debug.h>
#include <stdio.h>
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
  //printf ("##### (page_create_spt) created spt %p, plz free it.\n", spt);
  return spt;
}

void
page_destroy_spt (struct hash *spt)
{
  ASSERT (spt != NULL);
  hash_destroy (spt, page_hash_free);
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
  free (p);
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
  p->type = PG_UNKNOWN;
  p->file = NULL;

  hash_insert (cur->spt, &p->hash_elem);
  //printf ("##### (page_make_entry) created spt entry %p for upage=%p\n", p, p->upage);
  return p;
}

void
page_remove_entry (void *upage)
{
  struct thread *cur = thread_current ();
  struct page *p;

  ASSERT (cur->spt != NULL);
  ASSERT (is_user_vaddr (upage));
  ASSERT (pg_ofs (upage) == 0);

  p = page_lookup (upage);
  ASSERT (p != NULL);

  hash_delete (cur->spt, &p->hash_elem);
  free (p);
}

static bool install_page (void *upage, void *kpage, bool writable);

bool
page_load (void *upage)
{
  struct page *p;
  void *kpage;

  ASSERT (is_user_vaddr (upage));
  ASSERT (pg_ofs (upage) == 0);

  if ((p = page_lookup (upage)) == NULL)
    return false;
  ASSERT (p->type != PG_UNKNOWN);

  //ASSERT (p->frame == NULL);

  kpage = frame_alloc (PAL_USER);
  if (p->type == PG_FILE)
    {
      ASSERT (p->file != NULL);
      if (file_read_at (p->file, kpage, p->read_bytes, p->file_ofs)
          != (int) p->read_bytes)
        {
          frame_free (kpage);
          return false; 
        }
      memset (kpage + p->read_bytes, 0, p->zero_bytes);
    }
  else if (p->type == PG_SWAP)
    {
      PANIC ("not implemented yet: swap in.");
      //ASSERT (p->slot != BITMAP_ERROR);
      //swap_in (p);
    }
  else
    {
      ASSERT (p->type == PG_ZERO);
      memset (kpage, 0, PGSIZE);
    }

  //printf ("##### (page_load) kpage=%p is initialized: type=%d\n", kpage, p->type);

  if (!install_page (upage, kpage, p->writable)) 
    {
      frame_free (kpage);
      return false; 
    }

  return true;
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