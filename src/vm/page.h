#ifndef VM_PAGE_H
#define VM_PAGE_H

#include <stddef.h>
#include <stdbool.h>
#include <hash.h>
#include "threads/thread.h"
#include "filesys/file.h"

/* Defined in filesys/file.c. */
struct file;

/* How to initialize user virtual pages? */
enum page_type
  {
    PG_FILE = 1,     /* Load from file. */
    PG_SWAP = 2,     /* Load from swap slot. */
    PG_ZERO = 3,     /* Zero page contents. */
    PG_UNKNOWN = 4   /* Unknown. */
  };

/* Supplemental page table entry. */
struct page
  {
    void *upage;   /* User virtual page. */

    struct thread *owner;

    enum page_type type;
    bool writable;

    struct file *file;
    off_t file_ofs;
    size_t read_bytes;
    size_t zero_bytes;

    size_t slot;

    struct hash_elem hash_elem;
  };

struct hash *page_create_spt (void);
void page_destroy_spt (struct hash *);

struct page *page_make_entry (void *);
void page_remove_entry (void *);

bool page_load (void *);

struct page *page_lookup (void *);


#endif /* vm/page.h */