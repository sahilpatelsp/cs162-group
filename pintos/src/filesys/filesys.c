#include "filesys/filesys.h"
#include <debug.h>
#include <stdio.h>
#include <string.h>
#include "filesys/file.h"
#include "filesys/free-map.h"
#include "filesys/inode.h"
#include "filesys/directory.h"
#include "filesys/cache.h"
#include "threads/thread.h"

/* Partition that contains the file system. */
struct block* fs_device;

static void do_format(void);

/* Initializes the file system module.
   If FORMAT is true, reformats the file system. */
void filesys_init(bool format) {
  fs_device = block_get_role(BLOCK_FILESYS);
  if (fs_device == NULL)
    PANIC("No file system device found, can't initialize file system.");
  cache_init();
  inode_init();
  free_map_init();

  if (format)
    do_format();

  free_map_open();

  struct dir* dir = dir_open_root();
  dir_add(dir, ".", dir->inode->sector);
  dir_add(dir, "..", dir->inode->sector);
  dir_close(dir);
}

/* Shuts down the file system module, writing any unwritten data
   to disk. */
void filesys_done(void) {
  free_map_close();
  cache_flush();
}

/* Creates a file named NAME with the given INITIAL_SIZE.
   Returns true if successful, false otherwise.
   Fails if a file named NAME already exists,
   or if internal memory allocation fails. */
bool filesys_create(const char* name, off_t initial_size) {
  block_sector_t inode_sector = 0;
  // CHANGE TO TRAVERSE PATH ******
  // struct dir* dir = dir_open_root();
  char new_name[NAME_MAX + 1];
  struct dir* dir = NULL;
  bool success = resolve_path(name, &dir, new_name);
  if (!success) {
    return false;
  }
  success =
      (dir != NULL && free_map_allocate(1, &inode_sector) &&
       inode_create(inode_sector, initial_size, false) && dir_add(dir, new_name, inode_sector));
  if (!success && inode_sector != 0)
    free_map_release(inode_sector, 1);
  dir_close(dir);

  return success;
}

/* Opens the file with the given NAME.
   Returns the new file if successful or a null pointer
   otherwise.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
struct file* filesys_open(const char* name) {
  struct dir* dir = dir_open_root();
  struct inode* inode = NULL;

  if (dir != NULL)
    dir_lookup(dir, name, &inode);
  dir_close(dir);

  return file_open(inode);
}

int fd_open(const char* name) {
  char new_name[NAME_MAX + 1];
  struct dir* dir = NULL;
  bool success = resolve_path(name, &dir, new_name);
  if (!success) {
    return -1;
  }
  struct inode* inode = NULL;
  if (dir != NULL)
    dir_lookup(dir, new_name, &inode);
  dir_close(dir);
  if (inode->isdir) {
    struct dir* open_file = dir_open(inode);
    return add_file_d((void*)open_file, thread_current(), true);
  } else {
    struct file* open_file = file_open(inode);
    return add_file_d(open_file, thread_current(), false);
  }
  return -1;
}

/* Deletes the file named NAME.
   Returns true if successful, false on failure.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
//TODO******************
bool filesys_remove(const char* name) {
  char new_name[NAME_MAX + 1];
  struct dir* dirs = NULL;
  bool success = resolve_path(name, &dirs, new_name);
  if (!success) {
    return false;
  }
  // dir = ./a/b/c -> b, new_name -> c
  struct inode* inode;
  if (!dir_lookup(dirs, name, &inode)) {
    dir_close(dirs);
    return false;
  }
  dir_close(dirs);
  if (inode->isdir) {
    dirs = dir_open(inode);
    if (dirs == NULL) {
      return false;
    }
  } else {
    success = dirs != NULL && dir_remove(dir, new_name);
    dir_close(dir);

    return success;
  }
}

bool filesys_chdir(const char* dir, struct thread* t) {
  struct dir* dirs;
  char name[NAME_MAX + 1];
  bool success = resolve_path(dir, &dirs, name);
  if (!success) {
    return false;
  }
  struct inode* inode;
  if (!dir_lookup(dirs, name, &inode)) {
    dir_close(dirs);
    return false;
  }
  dir_close(dirs);
  dirs = dir_open(inode);
  if (dirs == NULL) {
    return false;
  }
  dir_close(t->cwd);
  t->cwd = dirs;
  return true;
}

bool filesys_mkdir(const char* dir, struct thread* t) {
  struct dir* dirs;
  char name[NAME_MAX + 1];
  bool success = resolve_path(dir, &dirs, name);
  if (!success) {
    return false;
  }

  struct inode* inode;
  if (dir_lookup(dirs, name, &inode)) {
    dir_close(dirs);
    return false;
  }
  block_sector_t inode_sector = 0;
  success = (dirs != NULL && free_map_allocate(1, &inode_sector) && dir_create(inode_sector, 2) &&
             dir_add(dirs, name, inode_sector));
  if (!success && inode_sector != 0)
    free_map_release(inode_sector, 1);

  if (!dir_lookup(dirs, name, &inode)) {
    dir_close(dirs);
    return false;
  }
  struct dir* new_dir = dir_open(inode);
  if (new_dir == NULL) {
    dir_close(dirs);
    return false;
  }
  dir_add(new_dir, ".", new_dir->inode->sector);
  dir_add(new_dir, "..", dirs->inode->sector);
  dir_close(dirs);
  dir_close(new_dir);
  return true;
}

bool filesys_readdir(int fd, char* name, struct thread* t) {
  struct dir* dir_struct = (struct dir*)(t->file_d[fd]).filesys_ptr;
  if (!dir_struct) {
    return false;
  }
  return dir_readdir(dir_struct, name);
}

bool filesys_isdir(int fd, struct thread* t) { return (t->file_d[fd]).isdir; }

int filesys_inumber(int fd, struct thread* t) {
  struct file* file = (struct file*)(t->file_d[fd]).filesys_ptr;
  return file->inode->sector;
}

/* Formats the file system. */
static void do_format(void) {
  printf("Formatting file system...");
  free_map_create();
  if (!dir_create(ROOT_DIR_SECTOR, 16))
    PANIC("root directory creation failed");
  free_map_close();
  printf("done.\n");
}

/* Resolve path into dir and name, unsure about close */
bool resolve_path(char* path, struct dir** dir, char* name) {
  if (path[0] == '\0') {
    return false;
  } else if (path[0] == '/') {
    *dir = dir_open_root();
    path++;
  } else {
    *dir = thread_current()->cwd;
  }

  char cur[NAME_MAX + 1];
  char next[NAME_MAX + 1];

  // cur = 1 and next = 1 : cur = directory, might be stuff after next?
  // cur = 1 and next = 0 : cur = last thing (could be dir or file)
  int cur_rv = get_next_part(cur, &path);
  int next_rv = get_next_part(next, &path);
  struct inode* inode = NULL;

  while (cur_rv == 1 && next_rv == 1) {
    if (!dir_lookup(*dir, cur, &inode)) {
      dir_close(*dir);
      return false;
    }
    dir_close(*dir);
    *dir = dir_open(inode);
    if (*dir == NULL) {
      return false;
    }
    strcpy(cur, next);
    cur_rv = next_rv;
    next_rv = get_next_part(next, &path);
  }

  if (cur_rv == 1 && next_rv == 0) {
    strcpy(name, cur);
    return true;
  }

  if (cur_rv == -1 || next_rv == -1) {
    dir_close(*dir);
    return false;
  }
  return false;
}

/* Extracts a file name part from *SRCP into PART, and updates *SRCP so that the next call will return the next file name part. Returns 1 if successful, 0 at end of string, -1 for a too-long file name part. */
int get_next_part(char part[NAME_MAX + 1], const char** srcp) {
  const char* src = *srcp;
  char* dst = part;
  /* Skip leading slashes. If it’s all slashes, we’re done. */ while (*src == '/')
    src++;
  if (*src == '\0')
    return 0;
  /* Copy up to NAME_MAX character from SRC to DST. Add null terminator. */
  while (*src != '/' && *src != '\0') {
    if (dst < part + NAME_MAX)
      *dst++ = *src;
    else
      return -1;
    src++;
  }
  *dst = '\0';
  /* Advance source pointer. */
  *srcp = src;
  return 1;
}
