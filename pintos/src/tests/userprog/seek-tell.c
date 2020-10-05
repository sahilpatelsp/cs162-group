/* Try seeking a file in the most normal way. */

#include <syscall.h>
#include "tests/userprog/sample.inc"
#include "tests/lib.h"
#include "tests/main.h"

void test_main(void) {
  int handle, byte_cnt;

  CHECK(create("test.txt", sizeof sample - 1), "create \"test.txt\"");
  CHECK((handle = open("test.txt")) > 1, "open \"test.txt\"");

  seek(handle, sizeof(sample) - 2);
  int curr = tell(handle);
  if (curr != (sizeof(sample) - 2))
    fail("tell() returned %d instead of %zu", curr, sizeof sample - 2);
}