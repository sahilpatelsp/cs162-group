# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected (IGNORE_EXIT_CODES => 1, [<<'EOF']);
(test-remove) begin
(test-remove) create "test.txt"
(test-remove) open "test.txt"
(test-remove) remove "test.txt"
(test-remove) remove "test.txt" again
(test-remove) create "test.txt"
(test-remove) open "test.txt"
(test-remove) close "test.txt"
(test-remove) remove "test.txt" after close
(test-remove) create "test.txt"
(test-remove) open "test.txt"
(test-remove) remove "test.txt" after open
(test-remove) end
EOF
pass;
