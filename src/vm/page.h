#ifndef VM_PAGE_H
#define VM_PAGE_H

#include <stdbool.h>
#include <stddef.h>
#include <hash.h>
#include "threads/thread.h"
#include "filesys/off_t.h"

/* How to load user virtual pages? */
enum page_type
  {
    PG_FILE = 1,                        /* Load from file. */
    PG_SWAP = 2,                        /* Load from swap slot. */
    PG_ZERO = 3,                        /* Zero page contents. */
    PG_UNKNOWN = 4                      /* Unknown (for debugging purposes). */
  };

/* A supplemental page table entry (SPTE) which provides
   supplemental information about a user virtual page required
   to execute the process.  That is, there is a one-to-one
   correspondence between a process's SPTEs and its user pages.

   All SPTEs are stored locally, because user virtual pages are
   process-specific.
   However, SPTEs could be accessed globally, due to the eviction.

   The supplemental information explains how to load the
   corresponding user virtual page: it should be loaded from file
   or swap slot, or its contents should be filled with zeros.
   
   FTE and SPTE are doubly linked. That is, if a physical frame
   is allocated and mapped to a user virtual page, a FTE
   corresponding to the frame and a SPTE corresponding to the user
   page point to each other. */
struct page
  {
    /* UPAGE identidies each corresponding SPTE, and it is used
       as hash key of OWNER's supplemental page table. */
    void *upage;
    struct thread *owner;

    /* If a physical frame is allocated and mapped to the UPAGE,
       the FTE corresponding to the pyhsical frame is recorded in
       the FRAME member.
       
       If FRAME is not NULL, then some physical frame was allocated
       to the UPAGE, and this SPTE was also recorded in the FRAME's
       PAGE member.  See frame.h.
       
       If FRAME is NULL, then the OWNER's page directory does not
       have any virtual-physical mapping for UPAGE. */
    struct frame *frame;

    /* If WRITABLE is false, the UPAGE is read-only; otherwise it
       will be writable as well. */
    bool writable;

    /* If DIRTY is false, the contents of UPAGE has not beend
       modified; otherwise the contents has been changed at least
       once.
       
       Notice that the contents reside in the corresponding physical
       frame, and this frame could be evicted.  When a physical frame
       is evicted and the owner of the frame changes from this SPTE
       to another one, its contents must be backed up in the swap slot,
       if DIRTY is true. */
    bool dirty;

    /* How to load this page? */
    enum page_type type;

    /* Used if TYPE is PG_FILE. */
    struct file *file;                  /* File. */
    off_t file_ofs;                     /* Offset. */
    size_t read_bytes;                  /* File read amount. */
    size_t zero_bytes;                  /* PGSIZE - READ_BYTES. */

    /* Used if TYPE is PG_SWAP. */
    size_t slot;                        /* Index of swap slot. */

    struct hash_elem hash_elem;         /* Hash element. */
  };

struct hash *page_create_spt (void);
void page_destroy_spt (struct hash *);

struct page *page_make_entry (void *);
void page_remove_entry (struct page *);

bool page_load (void *);
struct page *page_lookup (void *);


#endif /* vm/page.h */