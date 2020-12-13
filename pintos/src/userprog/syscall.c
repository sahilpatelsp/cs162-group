#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "filesys/cache.h"

// struct lock lock;
static void syscall_handler(struct intr_frame*);
bool syscall_create(const char* file, unsigned initial_size);
bool syscall_remove(const char* file);
int syscall_open(const char* file, struct thread* t);
int syscall_filesize(int fd, struct thread* t);
int syscall_read(int fd, void* buffer, unsigned size, struct thread* t);
int syscall_write(int fd, void* buffer, unsigned size, struct thread* t);
void syscall_seek(int fd, unsigned position, struct thread* t);
bool chdir(const char* dir);
bool mkdir(const char* dir);
bool readdir(int fd, char* name);
bool isdir(int fd);
int inumber(int fd);
unsigned syscall_tell(int fd, struct thread* t);
void validate_ptr(void* ptr, int size);
void validate_str(void* str);
int cache_hitrate();

int cache_hitrate() { return buffer_cache_hitrate(); }

void syscall_init(void) {
  // lock_init(&lock);
  intr_register_int(0x30, 3, INTR_ON, syscall_handler, "syscall");
}

/*
* validate_ptr() is a helper function for validating arguments to ensure that pointers to syscall arguments 
* are not null and point to user virtual address space.
*/
void validate_ptr(void* ptr, int size) {
  if (ptr == NULL || !is_user_vaddr(ptr) || !pagedir_get_page(thread_current()->pagedir, ptr) ||
      !is_user_vaddr(ptr + size - 1) ||
      !pagedir_get_page(thread_current()->pagedir, ptr + size - 1)) {
    general_exit(-1);
  }
}

/*
* validate_str() validates the string by validating the pointer to the string and the location of the string itself.
*/
void validate_str(void* str) {
  char* strng = (char*)str;
  validate_ptr(strng, 1);
  while (*strng != '\0') {
    strng++;
    validate_ptr(strng, 1);
  }
}

/*
* general_exit() is a helper function that is called in the case of a syscall failure/invalid inputs.
* It's main purpose is to ensure that the exit status of the current thread has been updated in the thread's
* thread_data struct, so that a parent waiting on a child is able to retrieve its exit status.
*/
void general_exit(int status) {
  thread_current()->thread_data->exit_status = status;
  thread_exit();
}

bool syscall_create(const char* file, unsigned initial_size) {
  return filesys_create(file, initial_size);
}

bool syscall_remove(const char* file) { return filesys_remove(file); }

int syscall_open(const char* file, struct thread* t) { return fd_open(file); }

int syscall_filesize(int fd, struct thread* t) {
  struct file* file_struct = (struct file*)(t->file_d[fd]).filesys_ptr;
  if (!file_struct) {
    general_exit(-1);
  }
  return file_length(file_struct);
}

int syscall_read(int fd, void* buffer, unsigned size, struct thread* t) {
  char* buf = (char*)buffer;
  if (fd == 0) {
    unsigned i;
    for (i = 0; i < size; i++) {
      buf[i] = input_getc();
    }
    return i;
  } else {
    struct file_meta file_meta = t->file_d[fd];
    if (file_meta.isdir == true) {
      return -1;
    }
    struct file* file_struct = (struct file*)file_meta.filesys_ptr;
    if (!file_struct) {
      return -1;
    }
    off_t ret = file_read(file_struct, buffer, size);
    if (ret == 0) {
      thread_yield();
    }
    return ret;
  }
}

int syscall_write(int fd, void* buffer, unsigned size, struct thread* t) {
  if (fd == 1) {
    putbuf(buffer, size);
  } else {
    struct file_meta file_meta = t->file_d[fd];
    if (file_meta.isdir == true) {
      general_exit(-1);
      return -1;
    }
    struct file* file_struct = (struct file*)file_meta.filesys_ptr;
    if (!file_struct) {
      general_exit(-1);
      return -1;
    }
    int result = file_write(file_struct, buffer, size);
    return result;
  }
}

void syscall_seek(int fd, unsigned position, struct thread* t) {
  struct file* file_struct = (struct file*)(t->file_d[fd]).filesys_ptr;
  if (file_struct) {
    file_seek(file_struct, position);
  } else {
    general_exit(-1);
  }
}

unsigned syscall_tell(int fd, struct thread* t) {
  struct file* file_struct = (struct file*)(t->file_d[fd]).filesys_ptr;
  if (!file_struct) {
    general_exit(-1);
  }
  return file_tell(file_struct);
}

void syscall_close(int fd, struct thread* t) {
  struct file_meta file_meta = t->file_d[fd];
  if (file_meta.isdir) {
    dir_close((struct dir*)file_meta.filesys_ptr);
  } else {
    file_close((struct file*)file_meta.filesys_ptr);
  }
  remove_file_d(fd, t);
}
/*
  * The syscall_handler function handles the syscall request by switching over args[0]. In the case of 
  * a file operation, the function acquires/releases a lock for the sake of mutual exclusion. It validates 
  * all the arguments, calls a respective syscall helper function, and stores the return value in eax.
  */
static void syscall_handler(struct intr_frame* f UNUSED) {
  uint32_t* args = ((uint32_t*)f->esp);
  validate_ptr(args, 4);
  switch (args[0]) {
    case SYS_PRACTICE:
      validate_ptr(args + 1, 4);
      f->eax = (uint32_t)args[1] + 1;
      break;
    case SYS_HALT:
      shutdown_power_off();
      break;
    case SYS_EXIT:
      validate_ptr(args + 1, 4);
      int exit_status = (int)args[1];
      f->eax = exit_status;
      general_exit(exit_status);
      break;
    case SYS_EXEC:
      validate_ptr(args + 1, 4);
      validate_str(args[1]);
      f->eax = process_execute((char*)args[1]);
      break;
    case SYS_WAIT:
      validate_ptr(args + 1, 4);
      f->eax = process_wait((int)args[1]);
      break;
    case SYS_WRITE:
      validate_ptr(args + 1, 4);
      validate_ptr(args + 2, 4);
      validate_ptr(args + 3, 4);
      int fd_write = args[1];
      void* buffer_write = args[2];
      unsigned size_write = args[3];
      if (fd_write == 0 || fd_write > 127) {
        general_exit(-1);
      }
      validate_ptr(buffer_write, size_write);
      f->eax = syscall_write(fd_write, buffer_write, size_write, thread_current());
      break;
    case SYS_CREATE:
      validate_ptr(args + 1, 4);
      validate_ptr(args + 2, 4);
      validate_str(args[1]);
      char* file_create = (char*)args[1];
      unsigned initial_size_create = (unsigned)args[2];
      f->eax = syscall_create(file_create, initial_size_create);
      break;
    case SYS_OPEN:
      validate_ptr(args + 1, 4);
      validate_str(args[1]);
      f->eax = syscall_open((char*)args[1], thread_current());
      break;
    case SYS_READ:
      validate_ptr(args + 1, 4);
      validate_ptr(args + 2, 4);
      validate_ptr(args + 3, 4);
      int fd_read = args[1];
      void* buffer_read = args[2];
      unsigned size_read = args[3];
      if (fd_read == 1 || fd_read > 127) {
        general_exit(-1);
      }
      validate_ptr(buffer_read, size_read);
      f->eax = syscall_read(fd_read, buffer_read, size_read, thread_current());
      break;
    case SYS_FILESIZE:
      validate_ptr(args + 1, 4);
      if (args[1] < 0 || args[1] > 127) {
        general_exit(-1);
      }
      f->eax = syscall_filesize(args[1], thread_current());
      break;
    case SYS_REMOVE:
      validate_ptr(args + 1, 4);
      validate_str((args[1]));
      f->eax = syscall_remove((char*)args[1]);
      break;
    case SYS_SEEK:
      validate_ptr(args + 1, 4);
      validate_ptr(args + 2, 4);
      int fd_seek = args[1];
      unsigned position = args[2];
      if (fd_seek < 0 || fd_seek > 127) {
        general_exit(-1);
      }
      syscall_seek(fd_seek, position, thread_current());
      break;
    case SYS_TELL:
      validate_ptr(args + 1, 4);
      int fd_tell = args[1];
      if (fd_tell < 0 || fd_tell > 127) {
        general_exit(-1);
      }
      f->eax = syscall_tell(fd_tell, thread_current());
      break;
    case SYS_CLOSE:
      validate_ptr(args + 1, 4);
      int fd_close = args[1];
      if (fd_close < 2 || fd_close > 127) {
        general_exit(-1);
      }
      syscall_close(fd_close, thread_current());
      break;
    case SYS_CHDIR:
      validate_ptr(args + 1, 4);
      const char* dir_chdir = args[1];
      validate_str(dir_chdir);
      f->eax = filesys_chdir(dir_chdir, thread_current());
      break;
    case SYS_MKDIR:
      validate_ptr(args + 1, 4);
      const char* dir_mkdir = args[1];
      validate_str(dir_mkdir);
      f->eax = filesys_mkdir(dir_mkdir, thread_current());
      break;
    case SYS_READDIR:
      validate_ptr(args + 1, 4);
      validate_ptr(args + 2, 4);
      int fd_readdir = args[1];
      char* name = args[2];
      validate_str(name);
      f->eax = filesys_readdir(fd_readdir, name, thread_current());
      break;
    case SYS_ISDIR:
      validate_ptr(args + 1, 4);
      int fd_isdir = args[1];
      f->eax = filesys_isdir(fd_isdir, thread_current());
      break;
    case SYS_INUMBER:
      validate_ptr(args + 1, 4);
      int fd_inumber = args[1];
      f->eax = filesys_inumber(fd_inumber, thread_current());
      break;
    default:
      break;
  }
}
