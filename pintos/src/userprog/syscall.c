#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include <pthread.h>

pthread_mutex_t lock;
static void syscall_handler(struct intr_frame*);
void validate_ptr(void* ptr, size_t size);
bool syscall_create(const char* file, unsigned initial_size);
bool syscall_remove(const char* file);
int syscall_open(const char* file, struct thread* t);
int syscall_filesize(int fd, struct thread* t);
int syscall_read(int fd, void* buffer, unsigned size, struct thread* t);
int syscall_write(int fd, void* buffer, unsigned size, struct thread* t);
void syscall_seek(int fd, unsigned position, struct thread* t);
unsigned syscall_tell(int fd, struct thread* t);
void syscall_close(int fd, struct thread* t);

void syscall_init(void) {
  pthread_mutex_init(&lock, NULL);
  intr_register_int(0x30, 3, INTR_ON, syscall_handler, "syscall");
}

void validate_ptr(void* ptr, size_t size) {
  if (ptr == NULL || !is_user_vaddr(ptr) || !pagedir_get_page(thread_current()->pagedir, ptr) ||
      !is_user_vaddr(ptr + size - 1) ||
      !pagedir_get_page(thread_current()->pagedir, ptr + size - 1)) {
    printf("%s: exit(%d)\n", -1);
    thread_exit();
  }
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
    return -1;
  }
  return file_length(file_struct);
}

int syscall_read(int fd, void* buffer, unsigned size, struct thread* t) {
  if (fd == 0) {
    //not sure what to pass in
    return input_getc();
  } else if (fd == 1) {
    //EXIT
  } else {
    struct file* file_struct = t->file_d[fd];
    int result = file_read(file_struct, buffer, size);
    return result;
  }
}

int syscall_write(int fd, void* buffer, unsigned size, struct thread* t) {
  if (fd == 1) {
    putbuf(buffer, size);
  } else if (fd == 0) {
    //EXIT
  } else {
    struct file* file_struct = t->file_d[fd];
    if (!file_struct) {
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
  }
}

unsigned syscall_tell(int fd, struct thread* t) {
  struct file* file_struct = t->file_d[fd];
  if (!file_struct) {
    return 0;
  }
  return file_tell(file_struct);
}

void close(int fd, struct thread* t) {
  struct file* file_struct = t->file_d[fd];
  if (file_struct) {
    file_close(file_struct);
  }
  remove_file_d(fd, t);
}
}
static void syscall_handler(struct intr_frame* f UNUSED) {
  pthread_mutex_unlock(&lock);
  uint32_t* args = ((uint32_t*)f->esp);
  validate_ptr(args, sizeof(uint32_t));

  /*
   * The following print statement, if uncommented, will print out the syscall
   * number whenever a process enters a system call. You might find it useful
   * when debugging. It will cause tests to fail, however, so you should not
   * include it in your final submission.
   */

  /* printf("System call number: %d\n", args[0]); */

  switch (args[0]) {
    case SYS_PRACTICE:
      f->eax = (uint32_t)args[1] + 1;

    case SYS_HALT:
      shutdown_power_off();

    case SYS_EXIT:
      validate_ptr(args[1]);
      f->eax = args[1];
      printf("%s: exit(%d)\n", &thread_current()->name, args[1]);
      thread_exit();

    case SYS_WRITE:
      validate_ptr(args[1], sizeof(char*);
      validat
    default:
      break;
  }

  if (args[0] == SYS_EXIT) {
    f->eax = args[1];
    printf("%s: exit(%d)\n", &thread_current()->name, args[1]);
    thread_exit();
  }

  else if (args[0] == SYS_WRITE) {
    int fd = args[1];
    const void* buffer = args[2];
    unsigned size = args[3];
    validate_ptr(buffer, size);
    syscall_write(fd, buffer, size, thread_current()); // &thread_current?
  }

  pthread_mutex_lock(&lock);
}
