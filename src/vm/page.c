#include "vm/page.h"
#include "vm/frame.h"
#include "vm/swap.h"
#include <debug.h>
#include <string.h>
#include <hash.h>
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "threads/malloc.h"
#include "userprog/pagedir.h"
#include "filesys/file.h"

static unsigned page_hash_func (const struct hash_elem *, void *);
static bool page_hash_less (const struct hash_elem *, const struct hash_elem *, void *);
static void page_hash_free (struct hash_elem *, void *);

/* Creates and initializes a supplemental page table (SPT).
   This table stores SPTEs using their UPAGE as a key. */
struct hash *
page_create_spt (void)
{
  struct hash *spt = malloc (sizeof (struct hash));
  if (!spt)
    PANIC ("cannot create a supplemental page table.");
  hash_init (spt, page_hash_func, page_hash_less, NULL);
  return spt;
}

/* Destroys supplemental page table SPT, freeing all the
   entries.
   If SPTE holds a FTE created by frame_alloc(), it is freed too.
   
   Importantly, the actual physical frame corresponding to the FTE
   is not freed, because this frame should be deallocated by
   pagedir_destroy() when a process exits. */
void
page_destroy_spt (struct hash *spt)
{
  ASSERT (spt != NULL);
  hash_destroy (spt, page_hash_free);
  free (spt);
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
  if (p->frame)
    frame_free (p->frame);
  free (p);
}

/* Creates a SPTE to load a user virtual page at UPAGE,
   stores it in the current process's SPT, and returns a pointer
   to the created SPTE.
   A request to create an already existing SPTE is denied.
   After created, the SPTE's TYPE and some necessary information
   must be initialized. */
struct page *
page_make_entry (void *upage)
{
  struct thread *cur = thread_current ();
  struct page *p;

  ASSERT (is_user_vaddr (upage));
  ASSERT (pg_ofs (upage) == 0);

  if (page_lookup (upage))
    return NULL;
  
  p = malloc (sizeof (struct page));
  if (!p)
    PANIC ("cannot create supplemental page table entry.");
  
  p->upage = upage;
  p->frame = NULL;
  p->owner = cur;

  p->type = PG_UNKNOWN;
  p->file = NULL;
  p->slot = BITMAP_ERROR;

  p->dirty = false;

  hash_insert (cur->spt, &p->hash_elem);
  return p;
}

/* Removes a SPTE given the pointer P.
   If SPTE holds a FTE created by frame_alloc(), it is freed too.
   
   Importantly, the actual physical frame corresponding to the FTE
   is not freed, because this frame should be deallocated by
   pagedir_destroy() when a process exits. */
void
page_remove_entry (struct page *p)
{
  ASSERT (p->owner == thread_current ());
  ASSERT (p != NULL);
  if (p->frame)
    frame_free (p->frame);
  hash_delete (p->owner->spt, &p->hash_elem);
  free (p);
}

static bool install_page (void *upage, void *kpage, bool writable);

/* Loads a user virtual page at UPAGE.

   If the current process's SPT does not contain any SPTE
   corresponding to the virtual page UPAGE, page_load()
   returns false.
   
   Otherwise, it allocates a frame for the SPTE and loads the
   contents of the page from file or swap slot, or fills with
   zeros.  Finally, a user virtual mapping is added to the
   current process. */
bool
page_load (void *upage)
{
  ASSERT (is_user_vaddr (upage));
  ASSERT (pg_ofs (upage) == 0);

  struct page *p = page_lookup (upage);
  if (!p)
    return false;

  struct frame *f = frame_alloc (p);
  switch (p->type)
    {
    case PG_FILE:
      {
        size_t read_bytes
          = file_read_at (p->file, f->kpage, p->read_bytes, p->file_ofs);
        if (read_bytes != p->read_bytes)
          goto fail;
        memset (f->kpage + p->read_bytes, 0, p->zero_bytes);
        break;
      }
    
    case PG_SWAP:
      swap_in (f->kpage, p->slot);
      p->slot = BITMAP_ERROR;
      break;
    
    case PG_ZERO:
      memset (f->kpage, 0, PGSIZE);
      break;

    case PG_UNKNOWN:
      PANIC ("unknown page type.");

    default:
      NOT_REACHED ();
    }

  if (!install_page (upage, f->kpage, p->writable)) 
    goto fail;

  return true;

 fail:
  frame_free (f);
  return false;
}

/* Adds a mapping from user virtual address UPAGE to kernel
   virtual address KPAGE to the page table.
   If WRITABLE is true, the user process may modify the page;
   otherwise, it is read-only.
   UPAGE must not already be mapped.
   KPAGE should probably be a page obtained from the user pool
   with palloc_get_page().
   Returns true on success, false if UPAGE is already mapped or
   if memory allocation fails. */
static bool
install_page (void *upage, void *kpage, bool writable)
{
  struct thread *t = thread_current ();

  /* Verify that there's not already a page at that virtual
     address, then map our page there. */
  return (pagedir_get_page (t->pagedir, upage) == NULL
          && pagedir_set_page (t->pagedir, upage, kpage, writable));
}

/* Finds a SPTE corresponding to the given UPAGE.
   If not found, returns NULL. */
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

/* Returns true, only if the PTE for user virtual page UPAGE
   corresponding to the given SPTE P in the page directory of P's
   owner process has been accessed recently, that is, between
   the time the PTE was installed and the last time it was cleared;
   otherwise it returns false.
   This function also resets the accessed bit to false in the PTE. */
bool
page_was_accessed (struct page *p)
{
  ASSERT (p != NULL);

  uint32_t *pd = p->owner->pagedir;
  void *upage = p->upage;
  bool accessed = pagedir_is_accessed (pd, upage);
  pagedir_set_accessed (pd, upage, false);

  return accessed;
}