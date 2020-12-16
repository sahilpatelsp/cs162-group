/* Tests the cache hit rate. */

#include "tests/lib.h"
#include "tests/main.h"
#include <random.h>
#include <stdio.h>
#include <stdlib.h>
#include <syscall.h>
#include "filesys/filesys.h"
#include "devices/block.h"
#include "filesys/cache.h"

#define BLOCK_SIZE 512
static char buf[BLOCK_SIZE];

void test_main(void) {
  msg("flushing cache");
  flush();
  const char* file_name = "blargle";
  int fd;
  random_init(0);
  random_bytes(buf, sizeof buf);
  msg("creating file %s", file_name);
  create(file_name, 0);
  msg("opening file %s", file_name);
  CHECK((fd = open(file_name)) > 1, "open \"%s\"", file_name);
  int bytes_written;
  msg("writing to file %s", file_name);
  for (int i = 0; i < 60; i++) {
    bytes_written = write(fd, buf, BLOCK_SIZE);
    if (bytes_written != BLOCK_SIZE) {
      fail("write less than number of bytes");
    }
  }
  msg("closing file %s", file_name);
  close(fd);
  msg("flushing cache");
  flush();

  int bytes_read = 0;
  msg("opening file %s", file_name);
  CHECK((fd = open(file_name)) > 1, "open \"%s\"", file_name);
  msg("reading from file %s", file_name);
  for (int i = 0; i < 60; i++) {
    bytes_read = read(fd, buf, BLOCK_SIZE);
    if (bytes_read != BLOCK_SIZE) {
      fail("write less than number of bytes");
    }
  }

  int hitrate_1 = cache_hitrate();
  msg("closing file %s", file_name);
  close(fd);
  msg("opening file %s", file_name);
  CHECK((fd = open(file_name)) > 1, "open \"%s\"", file_name);
  msg("reading from file %s", file_name);
  for (int i = 0; i < 60; i++) {
    bytes_read = read(fd, buf, BLOCK_SIZE);
    if (bytes_read != BLOCK_SIZE) {
      fail("write less than number of bytes");
    }
  }
  int hitrate_2 = cache_hitrate();
  if (hitrate_2 > hitrate_1) {
    msg("Hit rate improved in the second access");
  }
  close(fd);
}
