/* Tests the cache hit rate. */

#include "tests/lib.h"
#include "tests/main.h"
#include <random.h>
#include <stdio.h>
#include <stdlib.h>
#include <syscall.h>
#include "filesys/filesys.h"
#include "devices/block.h"

#define BLOCK_SIZE 512
static char buf[BLOCK_SIZE];

void test_main(void) {
  msg("flushing cache");
  cache_flush();
  const char* file_name = "blargle";
  int fd;
  random_init(0);
  random_bytes(buf, sizeof buf);
  create(file_name, BLOCK_SIZE);
  CHECK((fd = open(file_name)) > 1, "open \"%s\"", file_name);
  int bytes_read = 0;
  for (int i = 0; i < sizeof buf; i++) {
    bytes_read += read(fd, buf + i, i);
  }
  int hitrate_1 = cache_hitrate();
  close(file_name);
  CHECK((fd = open(file_name)) > 1, "open \"%s\"", file_name);
  bytes_read = 0;
  for (int i = 0; i < sizeof buf; i++) {
    bytes_read += read(fd, buf + i, i);
  }
  int hitrate_2 = cache_hitrate();
  if (hitrate_2 > hitrate_1) {
    msg("Hit rate improved in the second access");
  }
  close(fd);
}
