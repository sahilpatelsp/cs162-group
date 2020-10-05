/* Tries to remove the same file twice
   and remove after both open/close,
   which should handle gracefully or return with exit code -1*/

#include <syscall.h>
#include "tests/userprog/sample.inc"
#include "tests/lib.h"
#include "tests/main.h"

void test_main(void) {
  int h1;
  CHECK(create("test.txt", sizeof sample - 1), "create \"test.txt\"");
  CHECK((h1 = open("test.txt")) > 1, "open \"test.txt\"");
  CHECK(remove("test.txt"), "remove \"%s\"", "test.txt");
  CHECK(!remove("test.txt"), "remove \"test.txt\" again");

  CHECK(create("test.txt", sizeof sample - 1), "create \"test.txt\"");
  CHECK((h1 = open("test.txt")) > 1, "open \"test.txt\"");
  msg("close \"%s\"", "test.txt");
  close(h1);
  CHECK(remove("test.txt"), "remove \"test.txt\" after close");

  CHECK(create("test.txt", sizeof sample - 1), "create \"test.txt\"");
  CHECK((h1 = open("test.txt")) > 1, "open \"test.txt\"");
  CHECK(remove("test.txt"), "remove \"test.txt\" after open");
}