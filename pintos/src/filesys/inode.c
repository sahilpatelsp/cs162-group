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
  block_sector_t direct[121];
  block_sector_t indirect;
  block_sector_t doubly_indirect;
  unsigned magic;
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
  // struct inode_disk data; /* Inode content. */
};

/* Returns the block device sector that contains byte offset POS
   within INODE.
   Returns -1 if INODE does not contain data for a byte at offset
   POS. */
static block_sector_t byte_to_sector(const struct inode* inode, off_t pos) {
  ASSERT(inode != NULL);
  if (pos < inode->data.length)
    return inode->data.start + pos / BLOCK_SECTOR_SIZE;
  else
    return -1;
}

/* List of open inodes, so that opening a single inode twice
   returns the same `struct inode'. */
static struct list open_inodes;

/* Initializes the inode module. */
void inode_init(void) { list_init(&open_inodes); }

/* Initializes an inode with LENGTH bytes of data and
   writes the new inode to sector SECTOR on the file system
   device.
   Returns true if successful.
   Returns false if memory or disk allocation fails. */
bool inode_create(block_sector_t sector, off_t length) {
  struct inode_disk* disk_inode = (struct inode_disk*)malloc(sizeof(struct inode_disk));
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

  /* Check whether this inode is already open. */
  for (e = list_begin(&open_inodes); e != list_end(&open_inodes); e = list_next(e)) {
    inode = list_entry(e, struct inode, elem);
    if (inode->sector == sector) {
      inode_reopen(inode);
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
      bool success =
      // free_map_release(inode->sector, 1);
      // free_map_release(inode->data.start, bytes_to_sectors(inode->data.length));
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

  /*
  1. We have to pull in size ceiling div 512 bytes
  2. Write those blocks onto an immediate buffer 
  3. Write the bytes necessary from the block to the buffer
  */
  uint8_t* buffer = buffer_;
  off_t bytes_read = 0;

  while (size > 0) {
    /* Disk sector to read, starting byte offset within sector. */
    block_sector_t sector_idx = byte_to_sector(inode, offset);
    int sector_ofs = offset % BLOCK_SECTOR_SIZE;

    /* Bytes left in inode, bytes left in sector, lesser of the two. */
    off_t inode_left = inode_length(inode) - offset;
    int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
    int min_left = inode_left < sector_left ? inode_left : sector_left;

    /* Number of bytes to actually copy out of this sector. */
    int chunk_size = size < min_left ? size : min_left;
    if (chunk_size <= 0)
      break;

    cache_read(sector_idx, buffer + bytes_read, sector_ofs, chunk_size);
    // if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE) {
    //   /* Read full sector directly into caller's buffer. */
    //   //block_read(fs_device, sector_idx, buffer + bytes_read);
    //   cache_read(sector_idx, buffer + bytes_read, BLOCK_SECTOR_SIZE);
    // } else {
    //   /* Read sector into bounce buffer, then partially copy
    //          into caller's buffer. */
    //   cache_read(sector_idx, buffer + bytes_read, chunk_size);
    //   memcpy(buffer + bytes_read, bounce + sector_ofs, chunk_size);
    // }

    /* Advance. */
    size -= chunk_size;
    offset += chunk_size;
    bytes_read += chunk_size;
  }
  // free(bounce);

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
  uint8_t* bounce = NULL;

  if (inode->deny_write_cnt)
    return 0;

  while (size > 0) {
    /* Sector to write, starting byte offset within sector. */
    block_sector_t sector_idx = byte_to_sector(inode, offset);
    int sector_ofs = offset % BLOCK_SECTOR_SIZE;

    /* Bytes left in inode, bytes left in sector, lesser of the two. */
    off_t inode_left = inode_length(inode) - offset;
    int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
    int min_left = inode_left < sector_left ? inode_left : sector_left;

    /* Number of bytes to actually write into this sector. */
    int chunk_size = size < min_left ? size : min_left;
    if (chunk_size <= 0)
      break;

    if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE) {
      /* Write full sector directly to disk. */
      cache_write(sector_idx, buffer + bytes_written);
    } else {
      /* We need a bounce buffer. */
      if (bounce == NULL) {
        bounce = malloc(BLOCK_SECTOR_SIZE);
        if (bounce == NULL)
          break;
      }

      /* If the sector contains data before or after the chunk
             we're writing, then we need to read in the sector
             first.  Otherwise we start with a sector of all zeros. */
      if (sector_ofs > 0 || chunk_size < sector_left)
        block_read(fs_device, sector_idx, bounce);
      else
        memset(bounce, 0, BLOCK_SECTOR_SIZE);
      memcpy(bounce + sector_ofs, buffer + bytes_written, chunk_size);
      block_write(fs_device, sector_idx, bounce);
    }

    /* Advance. */
    size -= chunk_size;
    offset += chunk_size;
    bytes_written += chunk_size;
  }
  free(bounce);

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
off_t inode_length(const struct inode* inode) { return inode->data.length; }

bool handle_direct(block_sector_t* buffer, off_t size, int i, off_t offset) {
  block_sector_t sector;
  bool success;
  if (size <= BLOCK_SECTOR_SIZE * (i + offset) && buffer[i] != 0) {
    free_map_release(buffer[i], 1);
    buffer[i] = 0;
  }
  if (size > BLOCK_SECTOR_SIZE * (i + offset) && buffer[i] == 0) {
    success = free_map_allocate(1, &sector);
    if (!success) {
      return false;
    }
    buffer[i] = sector;
  }
  return true;
}

bool handle_indirect(block_sector_t** buffer_id, off_t size, off_t offset) {
  block_sector_t sector;
  block_sector_t buffer[128];
  bool success;

  //Allocating sector for indirect pointer
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

  //Allocating sectors for direct blocks
  for (int i = 0; i < 128; i++) {
    success = handle_direct(buffer, size, i, offset);
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

bool handle_doubly_indirect(block_sector_t** buffer_id, off_t size, off_t offset) {
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
    success = handle_indirect_pointer(&buffer[i], size, offset + (128 * i));
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

bool inode_resize(struct inode_disk* id, off_t size) {
  bool success;
  // Add resize lock
  // Handle all direct pointers
  for (int i = 0; i < 121; i++) {
    success = handle_direct(id->direct, size, i, 0);
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
  success = handle_indirect_pointer(&id->indirect, size, 121);
  if (!success) {
    inode_resize(id, id->length);
    return false;
  }

  if (id->doubly_indirect == 0 && size <= (128 * BLOCK_SECTOR_SIZE + 121 * BLOCK_SECTOR_SIZE)) {
    id->length = size;
    return true;
  }

  //Handle Doubly-Indirect Pointer
  block_sector_t sector;
  block_sector_t buffer[128];
  success = handle_doubly_indirect(&id->doubly_indirect, size, 121 + 128);
  if (!success) {
    inode_resize(id, id->length);
    return false;
  }
  id->length = size;
  return true;
}

// # 121 Direct pointer allocates up to 1 Sectors
// # Indirect pointer allocates up to 128
// # Doubly indirect allocates up to 128*128
// Create a doubly indirect pointer
// For i in range(128): call handle_indirect() -> handle_direct()