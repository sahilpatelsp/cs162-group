#include "filesys/cache.h"
#include "filesys/filesys.h"
#include "lib/kernel/list.h"
#include <stdint.h>
#include <stdlib.h>
#include "threads/synch.h"
#include <stdbool.h>

//Linked list of nodes that contain address of cache entry, lock for cache entry
// Block_cache with room for 64 blocks
// Individual cache entry locks

// 64 blocks * 512 block size bigly

void cache_init(void);
void cache_read(block_sector_t sector, void* buffer, int sector_ofs, int num_bytes);
void cache_write(block_sector_t sector, void* buffer, int sector_ofs, int num_bytes);
void cache_flush(void);
struct entry* get_entry(block_sector_t sector);
struct entry* sector_to_entry(block_sector_t sector);

static char data[32768];
struct lock lru_lock;
struct list lru;

#define MAXSIZE 64

void cache_init(void) {
  list_init(&lru);
  lock_init(&lru_lock);
  memset(data, 0, 32768);
}

void cache_read(block_sector_t sector, void* buffer, int sector_ofs, int num_bytes) {
  // 1. Call get_entry(sector) -> Will always return a entry. get_entry(sector) will ensure atomicity and update cache as necessary
  // 2. Acquire entry's lock
  // 3. Read from entry
  // 4. Release entry's lock
  // 3. Return

  //acquire list lock, try to find entry
  //if don't find entry, (if list full pick entry to evict) set new entry
  // move entry to the front, release list lock
  //acquire entry lock, read, release entry lock
  struct entry* entry = get_entry(sector);
  lock_acquire(&entry->lock);
  memcpy(buffer, data + (BLOCK_SECTOR_SIZE * entry->data_index) + sector_ofs, num_bytes);
  lock_release(&entry->lock);
}

void cache_write(block_sector_t sector, void* buffer, int sector_ofs, int num_bytes) {
  struct entry* entry = get_entry(sector);
  lock_acquire(&entry->lock);
  memcpy(data + (BLOCK_SECTOR_SIZE * entry->data_index) + sector_ofs, buffer, num_bytes);
  entry->dirty = 1;
  lock_release(&entry->lock);
}

void write_back(struct block* block, block_sector_t sector) {
  block_write(fs_device, sector, block);
}

void cache_flush(void) {
  lock_acquire(&lru_lock);
  struct list_elem* e;
  struct entry* entry;
  while (!list_empty(&lru)) {
    e = list_pop_front(&lru);
    entry = list_entry(e, struct entry, elem);
    if (entry->dirty == 1) {
      write_back(data + entry->data_index * BLOCK_SECTOR_SIZE, entry->sector);
    }
    free(entry);
  }
  lock_release(&lru_lock);
}

struct entry* sector_to_entry(block_sector_t sector) {
  struct entry* entry;
  struct list_elem* e;
  for (e = list_begin(&lru); e != list_end(&lru); e = list_next(e)) {
    entry = list_entry(e, struct entry, elem);
    if (entry->sector == sector) {
      return entry;
    }
  }
  return NULL;
}

struct entry* get_entry(block_sector_t sector) {
  struct entry* entry;
  lock_acquire(&lru_lock);
  entry = sector_to_entry(sector);
  if (entry != NULL) {
    list_remove(&(entry->elem));
  } else {
    //cache miss
    if (list_size(&lru) < MAXSIZE) {
      entry = (struct entry*)malloc(sizeof(struct entry));
      entry->sector = sector;
      entry->dirty = 0;
      lock_init(&(entry->lock));
      entry->data_index = list_size(&lru);
    } else {
      entry = list_entry(list_pop_back(&lru), struct entry, elem);
      if (entry->dirty == 1) {
        write_back(data + entry->data_index * BLOCK_SECTOR_SIZE, entry->sector);
      }
      entry->sector = sector;
      entry->dirty = 0;
    }
    block_read(fs_device, sector, data + entry->data_index * BLOCK_SECTOR_SIZE);
  }
  list_push_front(&lru, &(entry->elem));
  lock_release(&lru_lock);
  return entry;
}