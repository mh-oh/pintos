#ifndef VM_SWAP_H
#define VM_SWAP_H

#include <stddef.h>
#include <bitmap.h>

void swap_init (void);
size_t swap_out (void *);
void swap_in (void *, size_t);

#endif /* vm/swap.h */