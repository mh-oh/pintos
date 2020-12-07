#ifndef VM_FRAME_H
#define VM_FRAME_H

#include <list.h>
#include <stdbool.h>

/* A frame table entry (FTE) which holds a kernel virtual address
   identifying a physical frame palloc'ed from user pool.
   There is a one-to-one correspondence between FTEs and allocated
   user pool frames.

   All FTEs are managed globally, because all physical frames are
   distributed to multiple processes.
   
   FTE and SPTE are doubly linked. That is, if a physical frame
   is allocated and mapped to a user virtual page, a FTE
   corresponding to the frame and a SPTE corresponding to the user
   page point to each other. */
struct frame
  {
    /* Kernel virtual address which identifies a physical
       frame palloc'ed from user pool. */
    void *kpage;

    /* Every physical frame has been requested by some process to
       load a user virtual page supplemented by the corresponding
       SPTE.  Here, the SPTE is recorded in the PAGE member.
       
       Additionally, if physical frame identified by KPAGE is
       allocated and mapped to a user virtual page supplemented by
       the SPTE, this FTE is also recorded in the SPTE's FRAME member.
       See page.h. */
    struct page *page;

    struct list_elem list_elem;
  };

void frame_init (void);
struct frame *frame_alloc (struct page *);
void frame_free (struct frame *);

#endif /* vm/frame.h */