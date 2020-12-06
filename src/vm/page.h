#ifndef VM_PAGE_H
#define VM_PAGE_H

#include <stdbool.h>
#include <stddef.h>
#include <hash.h>
#include "filesys/off_t.h"

/* How to load user virtual pages? */
enum page_type
  {
    PG_FILE = 1,            /* Load from file. */
    PG_SWAP = 2,            /* Load from swap slot. */
    PG_ZERO = 3,            /* Zero page contents. */
    PG_UNKNOWN = 4          /* Unknown (for debugging purposes). */
  };

struct frame;               /* Defined in vm/frame.h. */
struct file;                /* Defined in filesys/file.c. */

/* A supplemental page table entry (SPTE). */
struct page
  {
    void *upage;            /* User virtual page. */
    struct frame *frame;    /* A FTE assigned to this SPTE. */

    struct thread *owner;   /* An owner thread. */

    enum page_type type;    /* How to load this page? */
    bool writable;          /* Is page writable? */

    /* PG_FILE. */
    struct file *file;      /* File. */
    off_t file_ofs;         /* Offset. */
    size_t read_bytes;      /* File read amount. */
    size_t zero_bytes;      /* PGSIZE - read_bytes. */

    struct hash_elem hash_elem;
  };

struct hash *page_create_spt (void);
void page_destroy_spt (struct hash *);

struct page *page_make_entry (void *);
void page_remove_entry (struct page *);

bool page_load (void *);
struct page *page_lookup (void *);


#endif /* vm/page.h */