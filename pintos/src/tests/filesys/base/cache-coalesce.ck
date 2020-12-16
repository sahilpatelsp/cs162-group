# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected (IGNORE_EXIT_CODES => 1, [<<'EOF']);
(cache-coalesce) begin
(cache-coalesce) flushing cache
(cache-coalesce) creating file blargle
(cache-coalesce) opening file blargle
(cache-coalesce) open "blargle"
(cache-coalesce) reading from file blargle
(cache-coalesce) writing to file blargle
(cache-coalesce) writing to file blargle
(cache-coalesce) reading from file blargle
(cache-coalesce) closing file blargle
(cache-coalesce) Number of writes is a factor of 128
(cache-coalesce) end
EOF
pass;