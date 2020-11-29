#ifndef VM_FRAME_H
#define VM_FRAME_H

#include <list.h>
#include "threads/thread.h"
#include "threads/palloc.h"
#include "threads/synch.h"

struct page;

/* Frame table entry. */
struct frame
  {
    void *kpage;   /* Kernel virtual page mapped to a frame. */
    struct thread *__owner;
    struct page *page;
    struct lock lock;
    struct list_elem list_elem;
  };

void frame_init (void);
struct frame *frame_alloc (enum palloc_flags, struct page *);
void frame_free (struct frame *);
void frame_free_all (void);
struct frame *frame_lookup (void *);

void frame_table_lock (void);
void frame_table_unlock (void);
void frame_unlock (struct frame *);

#endif /* vm/frame.h */