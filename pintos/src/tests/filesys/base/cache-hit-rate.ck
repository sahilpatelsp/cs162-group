# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected (IGNORE_EXIT_CODES => 1, [<<'EOF']);
(cache-hit-rate) begin
(cache-hit-rate) flushing cache
(cache-hit-rate) creating file blargle
(cache-hit-rate) opening file blargle
(cache-hit-rate) open "blargle"
(cache-hit-rate) writing to file blargle
(cache-hit-rate) closing file blargle
(cache-hit-rate) flushing cache
(cache-hit-rate) opening file blargle
(cache-hit-rate) open "blargle"
(cache-hit-rate) reading from file blargle
(cache-hit-rate) closing file blargle
(cache-hit-rate) opening file blargle
(cache-hit-rate) open "blargle"
(cache-hit-rate) reading from file blargle
(cache-hit-rate) Hit rate improved in the second access
(cache-hit-rate) end
EOF
pass;