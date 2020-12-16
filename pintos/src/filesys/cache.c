#include "filesys/cache.h"
#include "filesys/filesys.h"
#include "lib/kernel/list.h"
#include <stdint.h>
#include <stdlib.h>
#include "threads/synch.h"
#include <stdbool.h>
#include "threads/malloc.h"
#include <string.h>

void cache_init(void);
void cache_read(block_sector_t sector, void* buffer, int sector_ofs, int num_bytes);
void cache_write(block_sector_t sector, const void* buffer, int sector_ofs, int num_bytes);
void cache_flush(void);
struct entry* get_entry(block_sector_t sector);
struct entry* sector_to_entry(block_sector_t sector);
int cache_hitrate(void);
int get_write_count(void);

struct lock lru_lock;
struct list lru;
static char* data;
int cache_hits;
int cache_misses;

#define MAXSIZE 64

void cache_init(void) {
  list_init(&lru);
  lock_init(&lru_lock);
  cache_hits = 0;
  cache_misses = 0;
  data = calloc(32768, sizeof(char));
}

//Fetches block from disk/cache and copies into buffer
void cache_read(block_sector_t sector, void* buffer, int sector_ofs, int num_bytes) {
  struct entry* entry = get_entry(sector);
  lock_acquire(&entry->lock);
  memcpy(buffer, data + (BLOCK_SECTOR_SIZE * entry->data_index) + sector_ofs, num_bytes);
  lock_release(&entry->lock);
}

// Fetches block from disk/cache and writes from buffer to cache
void cache_write(block_sector_t sector, const void* buffer, int sector_ofs, int num_bytes) {
  struct entry* entry = get_entry(sector);
  lock_acquire(&entry->lock);
  memcpy(data + (BLOCK_SECTOR_SIZE * entry->data_index) + sector_ofs, buffer, num_bytes);
  entry->dirty = 1;
  lock_release(&entry->lock);
}

// Writes block back to disk
void write_back(void* block, block_sector_t sector) { block_write(fs_device, sector, block); }

// Flushes cache, deleting corresponding entries from the lru list
void cache_flush(void) {
  lock_acquire(&lru_lock);
  cache_hits = 0;
  cache_misses = 0;
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

// Checks if cache contains given sector. If so, returns corresponding entry struct. If not, returns NULL
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

// Gets block from disk/cache
struct entry* get_entry(block_sector_t sector) {
  struct entry* entry;
  lock_acquire(&lru_lock);
  entry = sector_to_entry(sector);
  if (entry != NULL) {
    cache_hits++;
    list_remove(&(entry->elem));
  } else {
    //cache miss
    cache_misses++;
    if (list_size(&lru) < MAXSIZE) {
      entry = (struct entry*)malloc(sizeof(struct entry));
      entry->sector = sector;
      entry->dirty = 0;
      lock_init(&(entry->lock));
      entry->data_index = list_size(&lru);
    } else {
      entry = list_entry(list_pop_back(&lru), struct entry, elem);
      if (entry->dirty == 1) {
        write_back(data + (entry->data_index * BLOCK_SECTOR_SIZE), entry->sector);
      }
      entry->sector = sector;
      entry->dirty = 0;
    }
    block_read(fs_device, sector, data + (entry->data_index * BLOCK_SECTOR_SIZE));
  }
  list_push_front(&lru, &(entry->elem));
  lock_release(&lru_lock);
  return entry;
}

// Functions for buffer-cache tests
int cache_hitrate(void) { return cache_hits; }
int get_write_count(void) { return block_write_count(fs_device); }