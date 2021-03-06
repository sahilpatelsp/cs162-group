#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "filesys/filesys.h"
#include "filesys/file.h"

struct lock lock;
static void syscall_handler(struct intr_frame*);
bool syscall_create(const char* file, unsigned initial_size);
bool syscall_remove(const char* file);
int syscall_open(const char* file, struct thread* t);
int syscall_filesize(int fd, struct thread* t);
int syscall_read(int fd, void* buffer, unsigned size, struct thread* t);
int syscall_write(int fd, void* buffer, unsigned size, struct thread* t);
void syscall_seek(int fd, unsigned position, struct thread* t);
unsigned syscall_tell(int fd, struct thread* t);
void validate_ptr(void* ptr, int size);

void syscall_init(void) {
  lock_init(&lock);
  intr_register_int(0x30, 3, INTR_ON, syscall_handler, "syscall");
}

void validate_ptr(void* ptr, int size) {
  if (ptr == NULL || !is_user_vaddr(ptr) || !pagedir_get_page(thread_current()->pagedir, ptr) ||
      !is_user_vaddr(ptr + size - 1) ||
      !pagedir_get_page(thread_current()->pagedir, ptr + size - 1)) {
    printf("HELLO WORLD");
    general_exit(-1);
  }
}

void general_exit(int status) {
  //barebone exit, don't know if we have to write anything, modify later
  printf("%s: exit(%d)\n", &thread_current()->name, status);
  thread_exit();
}

bool syscall_create(const char* file, unsigned initial_size) {
  if (strlen(file) > 14) {
    return false;
  }
  return filesys_create(file, initial_size);
}

bool syscall_remove(const char* file) { return filesys_remove(file); }

int syscall_open(const char* file, struct thread* t) {
  struct file* open_file = filesys_open(file);
  if (open_file == NULL) {
    return -1;
  }
  int file_descriptor = add_file_d(file, t);
  return file_descriptor;
}

int syscall_filesize(int fd, struct thread* t) {
  struct file* file_struct = t->file_d[fd];
  if (!file_struct) {
    general_exit(-1);
  }
  return file_length(file_struct);
}

int syscall_read(int fd, void* buffer, unsigned size, struct thread* t) {
  if (fd == 0) {
    //not sure what to pass in
    return input_getc();
  } else {
    struct file* file_struct = t->file_d[fd];
    if (!file_struct) {
      general_exit(-1);
    }
    int result = file_read(file_struct, buffer, size);
    return result;
  }
}

int syscall_write(int fd, void* buffer, unsigned size, struct thread* t) {
  if (fd == 1) {
    putbuf(buffer, size);
  } else {
    struct file* file_struct = t->file_d[fd];
    if (!file_struct) {
      general_exit(-1);
      return -1;
    }
    int result = file_write(file_struct, buffer, size);
    return result;
  }
}

void syscall_seek(int fd, unsigned position, struct thread* t) {
  struct file* file_struct = t->file_d[fd];
  if (file_struct) {
    file_seek(file_struct, position);
  } else {
    general_exit(-1);
  }
}

unsigned syscall_tell(int fd, struct thread* t) {
  struct file* file_struct = t->file_d[fd];
  if (!file_struct) {
    general_exit(-1);
  }
  return file_tell(file_struct);
}

void syscall_close(int fd, struct thread* t) {
  struct file* file_struct = t->file_d[fd];
  if (file_struct) {
    file_close(file_struct);
  }
  remove_file_d(fd, t);
}

static void syscall_handler(struct intr_frame* f UNUSED) {
  lock_acquire(&lock);
  uint32_t* args = ((uint32_t*)f->esp);
  validate_ptr(args, 4);

  /*
   * The following print statement, if uncommented, will print out the syscall
   * number whenever a process enters a system call. You might find it useful
   * when debugging. It will cause tests to fail, however, so you should not
   * include it in your final submission.
   */

  /* printf("System call number: %d\n", args[0]); */

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
      f->eax = args[1];
      general_exit((int)args[1]);
      break;
    case SYS_EXEC:
      validate_ptr(args + 1, 4);
      validate_ptr((char*)args[1], (strlen((char*)args[1]) + 1));
      f->eax = process_execute((char*)args[1]);
      break;
      //task 3
    case SYS_WRITE:
      validate_ptr(args + 1, 4);
      validate_ptr(args + 2, 4);
      validate_ptr(args + 3, 4);
      int fd_write = args[1];
      void* buffer_write = args[2];
      unsigned size_write = args[3];
      if (!fd_write || !size_write) {
        general_exit(-1);
      }
      validate_ptr((char*)buffer_write, size_write + 1);
      f->eax = syscall_write(fd_write, buffer_write, size_write, thread_current());
      break;
    case SYS_CREATE:
      validate_ptr(args + 1, 4);
      validate_ptr(args + 2, 4);
      char* file_create = args[1];
      unsigned initial_size_create = args[2];
      if (!initial_size_create) {
        general_exit(-1);
      }
      validate_ptr((char*)file_create, strlen(file_create) + 1);
      f->eax = syscall_create(file_create, initial_size_create);
      break;
    case SYS_OPEN:
      validate_ptr(args + 1, 4);
      validate_ptr(args[1], (strlen((void*)args[1]) + 1));
      f->eax = syscall_open(args[1], thread_current());
      break;
    case SYS_READ:
      validate_ptr(args + 1, 4);
      validate_ptr(args + 2, 4);
      validate_ptr(args + 3, 4);
      int fd_read = args[1];
      void* buffer_read = args[2];
      unsigned size_read = args[3];
      if (!fd_read || !size_read) {
        general_exit(-1);
      }
      validate_ptr((char*)buffer_read, (size_read + 1));
      f->eax = syscall_read(fd_read, buffer_read, size_read, thread_current());
      break;
    case SYS_FILESIZE:
      validate_ptr(args + 1, 4);
      if (!args[1]) {
        general_exit(-1);
      }
      f->eax = syscall_filesize(args[1], thread_current());
      break;
    case SYS_REMOVE:
      validate_ptr(args + 1, 4);
      validate_ptr((char*)args[1], (strlen((char*)args[1]) + 1));
      f->eax = syscall_remove(args[1]);
      break;
    case SYS_SEEK:
      validate_ptr(args + 1, 4);
      validate_ptr(args + 2, 4);
      int fd_seek = args[1];
      unsigned position = args[2];
      if (!fd_seek || !position) {
        general_exit(-1);
      }
      syscall_seek(fd_seek, position, thread_current());
      break;
    case SYS_TELL:
      validate_ptr(args + 1, 4);
      int fd_tell = args[1];
      if (!fd_tell) {
        general_exit(-1);
      }
      f->eax = syscall_tell(fd_tell, thread_current());
      break;
    case SYS_CLOSE:
      validate_ptr(args + 1, 4);
      int fd_close = args[1];
      if (!fd_close) {
        general_exit(-1);
      }
      syscall_close(fd_close, thread_current());
      break;
    default:
      break;
  }

  // if (args[0] == SYS_WRITE) {
  //   int fd = args[1];
  //   const void* buffer = args[2];
  //   unsigned size = args[3];
  //   validate_ptr(buffer, size);
  //   syscall_write(fd, buffer, size, thread_current()); // &thread_current?
  // }

  // Iterate through args and set them to variables
  // Call corresponding function and store return value
  //return the value

  lock_release(&lock);
}
