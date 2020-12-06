#ifndef VM_FRAME_H
#define VM_FRAME_H

#include <list.h>

/* A frame table entry (FTE). */
struct frame
  {
    /* Kernel virtual address which identifies a physical
       frame allocated from user pool. */
    void *kpage;

    struct list_elem list_elem;
  };

void frame_init (void);
struct frame *frame_alloc (void);
void frame_free (struct frame *);

#endif /* vm/frame.h */