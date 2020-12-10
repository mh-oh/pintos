#include "vm/swap.h"
#include <debug.h>
#include <bitmap.h>
#include "devices/block.h"
#include "threads/vaddr.h"
#include "threads/synch.h"

/* Number of sectors per page. */
#define PAGE_SECTOR_CNT (PGSIZE / BLOCK_SECTOR_SIZE)

/* Mutual exclusion. */
static struct lock swap_lock;

/* Block device for swapping. */
static struct block *swap_bdev;

/* Bitmap of free slots. */
static struct bitmap *used_map;

/* Number of swap slots. */
static size_t swap_slots;

/* Initializes the swap slot allocator.  At most SWAP_SLOTS
   slots are available. */
void
swap_init (void)
{
  if (!(swap_bdev = block_get_role (BLOCK_SWAP)))
    PANIC ("possible?");

  swap_slots = block_size (swap_bdev) / PAGE_SECTOR_CNT;
  used_map = bitmap_create (swap_slots);

  if (!used_map)
    PANIC ("bitmap allocation failed.");
  
  lock_init (&swap_lock);
}

/* Writes PGSIZE bytes to a free slot from KPAGE.  Returns the
   index of the free slot. 
   If too few slots are available, kernel panics. */
size_t
swap_out (void* kpage)
{
  size_t slot;
  block_sector_t sector;
  int i;

  ASSERT (kpage != NULL);

  lock_acquire (&swap_lock);
  slot = bitmap_scan_and_flip (used_map, 0, 1, false);
  lock_release (&swap_lock);

  if (slot != BITMAP_ERROR)
    {
      sector = slot * PAGE_SECTOR_CNT;
      for (i = 0; i < PAGE_SECTOR_CNT; i++)
        {
          block_write (swap_bdev, sector + i,
                       kpage + BLOCK_SECTOR_SIZE * i);
        }
      return slot;
    }
  else
    PANIC ("cannot find any free swap slot.");
}

/* Reads PGSIZE bytes from SLOT into KPAGE and frees SLOT. */
void
swap_in (void *kpage, size_t slot)
{
  block_sector_t sector;
  int i;

  ASSERT (kpage != NULL);
  ASSERT (slot != BITMAP_ERROR);

  sector = slot * PAGE_SECTOR_CNT;
  for (i = 0; i < PAGE_SECTOR_CNT; i++)
    {
      block_read (swap_bdev, sector + i,
                  kpage + BLOCK_SECTOR_SIZE * i);
    }

  ASSERT (bitmap_all (used_map, slot, 1));
  bitmap_set_multiple (used_map, slot, 1, false);
}

/* Just frees SLOT. */
void
swap_free (size_t slot)
{
  ASSERT (slot != BITMAP_ERROR);
  ASSERT (bitmap_all (used_map, slot, 1));
  
  bitmap_set_multiple (used_map, slot, 1, false);
}