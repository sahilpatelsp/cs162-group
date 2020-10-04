#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "filesys/filesys.h"
#include "filesys/file.h"

//Dummy comment

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
void validate_str(void* str);

void syscall_init(void) {
  lock_init(&lock);
  intr_register_int(0x30, 3, INTR_ON, syscall_handler, "syscall");
}

void validate_ptr(void* ptr, int size) {
  if (ptr == NULL || !is_user_vaddr(ptr) || !pagedir_get_page(thread_current()->pagedir, ptr) ||
      !is_user_vaddr(ptr + size - 1) ||
      !pagedir_get_page(thread_current()->pagedir, ptr + size - 1)) {
    general_exit(-1);
  }
}

void validate_str(void* str) {
  char* strng = (char*)str;
  validate_ptr(strng, 1);
  while (*strng != '\0') {
    strng++;
    validate_ptr(strng, 1);
  }
}

void general_exit(int status) {
  //barebone exit, don't know if we have to write anything, modify later
  thread_current()->thread_data->exit_status = status;
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
  int file_descriptor = add_file_d(open_file, t);
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
  char* buf = (char*)buffer;
  if (fd == 0) {
    unsigned i;
    for (i = 0; i < size; i++) {
      buf[i] = input_getc();
    }
    return i;
  } else {
    struct file* file_struct = t->file_d[fd];
    if (!file_struct) {
      return -1;
    }
    return file_read(file_struct, buffer, size);
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

void syscall_close(int fd, struct thread* t) { remove_file_d(fd, t); }

static void syscall_handler(struct intr_frame* f UNUSED) {
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
      lock_acquire(&lock);
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
      lock_release(&lock);
      break;
    case SYS_CREATE:
      lock_acquire(&lock);
      validate_ptr(args + 1, 4);
      validate_ptr(args + 2, 4);
      validate_str(args[1]);
      char* file_create = (char*)args[1];
      unsigned initial_size_create = (unsigned)args[2];
      f->eax = syscall_create(file_create, initial_size_create);
      lock_release(&lock);
      break;
    case SYS_OPEN:
      lock_acquire(&lock);
      validate_ptr(args + 1, 4);
      validate_str(args[1]);
      f->eax = syscall_open((char*)args[1], thread_current());
      lock_release(&lock);
      break;
    case SYS_READ:
      lock_acquire(&lock);
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
      lock_release(&lock);
      break;
    case SYS_FILESIZE:
      lock_acquire(&lock);
      validate_ptr(args + 1, 4);
      if (args[1] < 0 || args[1] > 127) {
        general_exit(-1);
      }
      f->eax = syscall_filesize(args[1], thread_current());
      lock_release(&lock);
      break;
    case SYS_REMOVE:
      lock_acquire(&lock);
      validate_ptr(args + 1, 4);
      validate_str((args[1]));
      f->eax = syscall_remove((char*)args[1]);
      lock_release(&lock);
      break;
    case SYS_SEEK:
      lock_acquire(&lock);
      validate_ptr(args + 1, 4);
      validate_ptr(args + 2, 4);
      int fd_seek = args[1];
      unsigned position = args[2];
      if (fd_seek < 0 || fd_seek > 127) {
        general_exit(-1);
      }
      syscall_seek(fd_seek, position, thread_current());
      lock_release(&lock);
      break;
    case SYS_TELL:
      lock_acquire(&lock);
      validate_ptr(args + 1, 4);
      int fd_tell = args[1];
      if (fd_tell < 0 || fd_tell > 127) {
        general_exit(-1);
      }
      f->eax = syscall_tell(fd_tell, thread_current());
      lock_release(&lock);
      break;
    case SYS_CLOSE:
      lock_acquire(&lock);
      validate_ptr(args + 1, 4);
      int fd_close = args[1];
      if (fd_close < 2 || fd_close > 127) {
        general_exit(-1);
      }
      syscall_close(fd_close, thread_current());
      lock_release(&lock);
      break;
    default:
      break;
  }

  // Iterate through args and set them to variables
  // Call corresponding function and store return value
  //return the value
}
