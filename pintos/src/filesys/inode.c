#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "filesys/cache.h"
#include "threads/malloc.h"
/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44

/* On-disk inode.
   Must be exactly BLOCK_SECTOR_SIZE bytes long. */
// struct inode_disk {
//   block_sector_t start; /* First data sector. */
//   off_t length;         /* File size in bytes. */
//   unsigned magic;       /* Magic number. */
//   uint32_t unused[125]; /* Not used. */
// };
struct inode_disk {
  off_t length;
  block_sector_t parent;
  block_sector_t direct[121];
  block_sector_t indirect;
  block_sector_t doubly_indirect;
  bool isdir;
  unsigned magic;
  char unused[3];
};

/* Returns the number of sectors to allocate for an inode SIZE
   bytes long. */
static inline size_t bytes_to_sectors(off_t size) { return DIV_ROUND_UP(size, BLOCK_SECTOR_SIZE); }

/* In-memory inode. */
struct inode {
  struct list_elem elem; /* Element in inode list. */
  block_sector_t sector; /* Sector number of disk location. */
  int open_cnt;          /* Number of openers. */
  bool removed;          /* True if deleted, false otherwise. */
  int deny_write_cnt;    /* 0: writes ok, >0: deny writes. */
  struct lock resize_lock;
  bool isdir;
};

/* List of open inodes, so that opening a single inode twice
   returns the same `struct inode'. */
struct list open_inodes;
struct lock open_inodes_lock;

bool inode_resize(struct inode_disk* id, int size);
bool handle_direct(block_sector_t* buffer, off_t size, int i, off_t offset);
bool handle_indirect(block_sector_t* buffer_id, off_t size, off_t offset);
bool handle_doubly_indirect(block_sector_t* buffer_id, off_t size, off_t offset);

/* Initializes the inode module. */
void inode_init(void) {
  list_init(&open_inodes);
  lock_init(&open_inodes_lock);
}

/* Initializes an inode with LENGTH bytes of data and
   writes the new inode to sector SECTOR on the file system
   device.
   Returns true if successful.
   Returns false if memory or disk allocation fails. */
bool inode_create(block_sector_t sector, off_t length, bool isdir) {
  struct inode_disk* disk_inode = (struct inode_disk*)calloc(1, sizeof(struct inode_disk));
  if (disk_inode == NULL) {
    return false;
  }
  bool success = false;

  ASSERT(length >= 0);

  /* If this assertion fails, the inode structure is not exactly
     one sector in size, and you should fix that. */
  ASSERT(sizeof *disk_inode == BLOCK_SECTOR_SIZE);
  disk_inode->magic = INODE_MAGIC;
  success = inode_resize(disk_inode, length);
  if (!success) {
    free(disk_inode);
    return false;
  }
  disk_inode->length = length;
  disk_inode->isdir = isdir;
  cache_write(sector, disk_inode, 0, BLOCK_SECTOR_SIZE);
  free(disk_inode);
  return success;
}

/* Reads an inode from SECTOR
   and returns a `struct inode' that contains it.
   Returns a null pointer if memory allocation fails. */
struct inode* inode_open(block_sector_t sector) {
  struct list_elem* e;
  struct inode* inode;

  lock_acquire(&open_inodes_lock);
  /* Check whether this inode is already open. */
  for (e = list_begin(&open_inodes); e != list_end(&open_inodes); e = list_next(e)) {
    inode = list_entry(e, struct inode, elem);
    if (inode->sector == sector) {
      inode_reopen(inode);
      lock_release(&open_inodes_lock);
      return inode;
    }
  }

  /* Allocate memory. */
  inode = malloc(sizeof *inode);
  if (inode == NULL)
    return NULL;

  /* Initialize. */
  list_push_front(&open_inodes, &inode->elem);
  inode->sector = sector;
  inode->open_cnt = 1;
  inode->deny_write_cnt = 0;
  inode->removed = false;
  lock_init(&inode->resize_lock);
  lock_release(&open_inodes_lock);

  struct inode_disk* id = (struct inode_disk*)malloc(sizeof(struct inode_disk));
  cache_read(inode->sector, id, 0, BLOCK_SECTOR_SIZE);
  inode->isdir = id->isdir;
  free(id);

  return inode;
}

/* Reopens and returns INODE. */
struct inode* inode_reopen(struct inode* inode) {
  if (inode != NULL)
    inode->open_cnt++;
  return inode;
}

/* Returns INODE's inode number. */
block_sector_t inode_get_inumber(const struct inode* inode) { return inode->sector; }

/* Closes INODE and writes it to disk.
   If this was the last reference to INODE, frees its memory.
   If INODE was also a removed inode, frees its blocks. */
void inode_close(struct inode* inode) {
  /* Ignore null pointer. */
  if (inode == NULL)
    return;

  /* Release resources if this was the last opener. */
  if (--inode->open_cnt == 0) {
    /* Remove from inode list and release lock. */
    list_remove(&inode->elem);
    /* Deallocate blocks if removed. */
    if (inode->removed) {
      struct inode_disk* id = (struct inode_disk*)malloc(sizeof(struct inode_disk));
      cache_read(inode->sector, id, 0, BLOCK_SECTOR_SIZE);
      lock_acquire(&inode->resize_lock);
      inode_resize(id, 0);
      lock_release(&inode->resize_lock);
      free(id);
    }
    free(inode);
  }
}

/* Marks INODE to be deleted when it is closed by the last caller who
   has it open. */
void inode_remove(struct inode* inode) {
  ASSERT(inode != NULL);
  inode->removed = true;
}

/* Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
   Returns the number of bytes actually read, which may be less
   than SIZE if an error occurs or end of file is reached. */
off_t inode_read_at(struct inode* inode, void* buffer_, off_t size, off_t offset) {
  struct inode_disk* id = (struct inode_disk*)malloc(sizeof(struct inode_disk));
  cache_read(inode->sector, id, 0, BLOCK_SECTOR_SIZE);
  size = (size <= id->length - offset) ? size : id->length - offset;
  if (size <= 0) {
    return 0;
  }
  uint8_t* buffer = buffer_;
  off_t bytes_read = 0;

  int start = offset / BLOCK_SECTOR_SIZE;
  int end = DIV_ROUND_UP(size + offset, BLOCK_SECTOR_SIZE);
  offset = offset % BLOCK_SECTOR_SIZE;
  int delim;
  int num_bytes;
  if (start >= 0 && start < 121) {
    delim = (end < 121) ? end : 121;
    for (int i = start; i < delim; i++) {
      num_bytes = (size < BLOCK_SECTOR_SIZE - offset) ? size : BLOCK_SECTOR_SIZE - offset;
      cache_read(id->direct[i], &buffer[bytes_read], offset, num_bytes);
      bytes_read += num_bytes;
      size -= num_bytes;
      offset = 0;
    }
    start = delim;
  }

  if (start >= 121 && start < 249) {
    delim = (end < 249) ? end : 249;
    block_sector_t indirect[128];
    cache_read(id->indirect, indirect, 0, BLOCK_SECTOR_SIZE);
    for (int i = start; i < delim; i++) {
      num_bytes = (size < BLOCK_SECTOR_SIZE - offset) ? size : BLOCK_SECTOR_SIZE - offset;
      cache_read(indirect[i - 121], &buffer[bytes_read], offset, num_bytes);
      bytes_read += num_bytes;
      size -= num_bytes;
      offset = 0;
    }
    start = delim;
  }

  if (start >= 249) {
    block_sector_t doubly_indirect[128];
    block_sector_t indirect[128];
    cache_read(id->doubly_indirect, doubly_indirect, 0, BLOCK_SECTOR_SIZE);
    for (int i = (start - 249) / 128; i < DIV_ROUND_UP(end - 249, 128); i++) {
      cache_read(doubly_indirect[i], indirect, 0, BLOCK_SECTOR_SIZE);
      delim = (end < start + 128) ? end : start + 128;
      for (int j = start; j < delim; j++) {
        num_bytes = (size < BLOCK_SECTOR_SIZE - offset) ? size : BLOCK_SECTOR_SIZE - offset;
        cache_read(indirect[j - 249 - (128 * i)], &buffer[bytes_read], offset, num_bytes);
        bytes_read += num_bytes;
        size -= num_bytes;
        offset = 0;
      }
      start = delim;
    }
  }
  free(id);
  return bytes_read;
}

/* Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
   Returns the number of bytes actually written, which may be
   less than SIZE if end of file is reached or an error occurs.
   (Normally a write at end of file would extend the inode, but
   growth is not yet implemented.) */
off_t inode_write_at(struct inode* inode, const void* buffer_, off_t size, off_t offset) {
  const uint8_t* buffer = buffer_;
  off_t bytes_written = 0;

  if (inode->deny_write_cnt)
    return 0;

  struct inode_disk* id = (struct inode_disk*)malloc(sizeof(struct inode_disk));
  cache_read(inode->sector, id, 0, BLOCK_SECTOR_SIZE);
  int new_size = (id->length >= size + offset) ? id->length : size + offset;
  // printf("NEW SIZE %d LENGTH %d\n", new_size, id->length);
  if (new_size != id->length) {
    lock_acquire(&inode->resize_lock);
    bool success = inode_resize(id, new_size);
    lock_release(&inode->resize_lock);
    if (!success) {
      return 0;
    }
    id->length = new_size;
    cache_write(inode->sector, id, 0, BLOCK_SECTOR_SIZE);
  }

  int start = offset / BLOCK_SECTOR_SIZE;
  int end = DIV_ROUND_UP(size + offset, BLOCK_SECTOR_SIZE);
  offset = offset % BLOCK_SECTOR_SIZE;
  int delim;
  int num_bytes;
  if (start >= 0 && start < 121) {
    delim = (end < 121) ? end : 121;
    for (int i = start; i < delim; i++) {
      num_bytes = (size < BLOCK_SECTOR_SIZE - offset) ? size : BLOCK_SECTOR_SIZE - offset;
      cache_write(id->direct[i], &buffer[bytes_written], offset, num_bytes);
      bytes_written += num_bytes;
      size -= num_bytes;
      offset = 0;
    }
    start = delim;
  }

  if (start >= 121 && start < 249) {
    delim = (end < 249) ? end : 249;
    block_sector_t indirect[128];
    cache_read(id->indirect, indirect, 0, BLOCK_SECTOR_SIZE);
    for (int i = start; i < delim; i++) {
      num_bytes = (size < BLOCK_SECTOR_SIZE - offset) ? size : BLOCK_SECTOR_SIZE - offset;
      cache_write(indirect[i - 121], &buffer[bytes_written], offset, num_bytes);
      bytes_written += num_bytes;
      size -= num_bytes;
      offset = 0;
    }
    start = delim;
  }

  if (start >= 249) {
    block_sector_t doubly_indirect[128];
    block_sector_t indirect[128];
    cache_read(id->doubly_indirect, doubly_indirect, 0, BLOCK_SECTOR_SIZE);
    for (int i = (start - 249) / 128; i < DIV_ROUND_UP(end - 249, 128); i++) {
      cache_read(doubly_indirect[i], indirect, 0, BLOCK_SECTOR_SIZE);
      delim = (end < start + 128) ? end : start + 128;
      for (int j = start; j < delim; j++) {
        num_bytes = (BLOCK_SECTOR_SIZE - offset) ? size : BLOCK_SECTOR_SIZE - offset;
        cache_write(indirect[j - 249 - (128 * i)], &buffer[bytes_written], offset, num_bytes);
        bytes_written += num_bytes;
        size -= num_bytes;
        offset = 0;
      }
      start = delim;
    }
  }
  free(id);
  return bytes_written;
}

/* Disables writes to INODE.
   May be called at most once per inode opener. */
void inode_deny_write(struct inode* inode) {
  inode->deny_write_cnt++;
  ASSERT(inode->deny_write_cnt <= inode->open_cnt);
}

/* Re-enables writes to INODE.
   Must be called once by each inode opener who has called
   inode_deny_write() on the inode, before closing the inode. */
void inode_allow_write(struct inode* inode) {
  ASSERT(inode->deny_write_cnt > 0);
  ASSERT(inode->deny_write_cnt <= inode->open_cnt);
  inode->deny_write_cnt--;
}

/* Returns the length, in bytes, of INODE's data. */
off_t inode_length(const struct inode* inode) {
  struct inode_disk* id = (struct inode_disk*)malloc(sizeof(struct inode_disk));
  cache_read(inode->sector, id, 0, BLOCK_SECTOR_SIZE);
  off_t size = id->length;
  free(id);
  return size;
}

bool handle_direct(block_sector_t* buffer, off_t size, int i, off_t offset) {
  bool success;
  // if (buffer == -1072580080) {
  //   for (int i = 0; i < 121; i++) {
  //     printf("%d\n", buffer[i]);
  //   }
  // }
  if (size <= BLOCK_SECTOR_SIZE * (i + offset) && buffer[i] != 0) {
    // printf("BEFORE 340\n");
    // printf("FUCK %d \n", buffer[i]);
    // printf("Size: %d, Offset: %d, i: %d", size, offset, i);
    free_map_release(buffer[i], 1);
    // printf("AFTER 340\n");
    buffer[i] = 0;
  }
  if (size > BLOCK_SECTOR_SIZE * (i + offset) && buffer[i] == 0) {
    success = free_map_allocate((size_t)1, &buffer[i]);
    if (!success) {
      return false;
    }
  }
  return true;
}

bool handle_indirect(block_sector_t* buffer_id, off_t size, off_t offset) {
  block_sector_t sector;
  block_sector_t* buffer = calloc(128, sizeof(block_sector_t));
  bool success;

  //Allocating sector for indirect pointer
  if (*buffer_id == 0) {
    memset(buffer, 0, BLOCK_SECTOR_SIZE);
    success = free_map_allocate(1, &sector);
    if (!success) {
      free(buffer);
      return false;
    }
    *buffer_id = sector;
  } else {
    cache_read(*buffer_id, (void*)buffer, 0, BLOCK_SECTOR_SIZE);
  }

  //Allocating sectors for direct blocks
  for (int i = 0; i < 128; i++) {
    // printf("B 375\n");
    success = handle_direct(buffer, size, i, offset);
    // printf("A 375\n");
    if (!success) {
      free(buffer);
      return false;
    }
  }
  if (*buffer_id != 0 && size <= offset * BLOCK_SECTOR_SIZE) {
    free_map_release(*buffer_id, 1);
    *buffer_id = 0;
  } else {
    cache_write(*buffer_id, buffer, 0, BLOCK_SECTOR_SIZE);
  }
  free(buffer);
  return true;
}

bool handle_doubly_indirect(block_sector_t* buffer_id, off_t size, off_t offset) {
  block_sector_t sector;
  block_sector_t buffer[128];
  bool success;
  if (*buffer_id == 0) {
    memset(buffer, 0, BLOCK_SECTOR_SIZE);
    success = free_map_allocate(1, &sector);
    if (!success) {
      return false;
    }
    *buffer_id = sector;
  } else {
    cache_read(*buffer_id, buffer, 0, BLOCK_SECTOR_SIZE);
  }

  for (int i = 0; i < 128; i++) {
    success = handle_indirect(&buffer[i], size, offset + (128 * i));
    if (!success) {
      return false;
    }
  }

  if (*buffer_id != 0 && size <= offset * BLOCK_SECTOR_SIZE) {
    free_map_release(*buffer_id, 1);
    *buffer_id = 0;
  } else {
    cache_write(*buffer_id, buffer, 0, BLOCK_SECTOR_SIZE);
  }
  return true;
}

bool inode_resize(struct inode_disk* id, int size) {
  bool success;
  // Add resize lock
  // Handle all direct pointers
  for (int i = 0; i < 121; i++) {
    // printf("BEFORE 427\n");
    success = handle_direct(id->direct, size, i, 0);
    // printf("AFTER 427\n");
    if (!success) {
      inode_resize(id, id->length);
      return false;
    }
  }
  if (id->indirect == 0 && size <= 121 * BLOCK_SECTOR_SIZE) {
    id->length = size;
    return true;
  }

  //Handle Indirect Pointer
  // printf("BEFORE 440 %d\n", id->indirect);
  success = handle_indirect(&id->indirect, size, 121);
  if (!success) {
    inode_resize(id, id->length);
    return false;
  }
  // printf("AFTER 440 %d\n", id->indirect);

  if (id->doubly_indirect == 0 && size <= (128 * BLOCK_SECTOR_SIZE + 121 * BLOCK_SECTOR_SIZE)) {
    id->length = size;
    return true;
  }

  //Handle Doubly-Indirect Pointer
  success = handle_doubly_indirect(&id->doubly_indirect, size, 121 + 128);
  if (!success) {
    inode_resize(id, id->length);
    return false;
  }
  id->length = size;
  return true;
}
