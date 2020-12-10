
#ifndef FILESYS_CACHE_H
#define FILESYS_CACHE_H

#include "devices/block.h"
#include "threads/synch.h"
#include "lib/kernel/list.h"

void cache_init(void);
void cache_read(block_sector_t sector, void* buffer, int sector_ofs, int num_bytes);
void cache_write(block_sector_t sector, void* buffer, int sector_ofs, int num_bytes);
void cache_flush(void);

struct entry {
  block_sector_t sector;
  struct lock lock;
  int data_index;
  int dirty;
  struct list_elem elem;
};

#endif /* filesys/cache.h */