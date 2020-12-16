
#include "tests/lib.h"
#include "tests/main.h"
#include <random.h>
#include <stdio.h>
#include <stdlib.h>

#define BLOCK_SIZE 512
static char* buf[65536];

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

  msg("reading from file %s", file_name);
  for (int i = 0; i < 65536 / 2; i++) {
    read(fd, buf + i, 1);
  }

  msg("writing to file %s", file_name);
  for (int i = 0; i < 65536 / 2; i++) {
    write(fd, buf + i, 1);
  }

  msg("writing to file %s", file_name);
  for (int i = 0; i < 65536 / 2; i++) {
    write(fd, buf + i, 1);
  }

  msg("reading from file %s", file_name);
  for (int i = 0; i < 65536 / 2; i++) {
    read(fd, buf + i, 1);
  }

  int num_writes_1 = write_count();
  msg("closing file %s", file_name);
  close(fd);
  if (num_writes_1 % 128 == 0) {
    msg("Number of writes is a factor of 128");
  }
  close(fd);
}
