# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected (IGNORE_EXIT_CODES => 1, [<<'EOF']);
(cache-hit-rate) begin
(cache-hit-rate) create "blargle"
(cache-hit-rate) open "blargle" for verification
(cache-hit-rate) read "blargle" sequentially
(cache-hit-rate) check hit rate
(cache-hit-rate) close "blargle"
(cache-hit-rate) reopen "blargle" for verification
(cache-hit-rate) read "blargle" sequentially
(cache-hit-rate) check hit rate2
(cache-hit-rate) compare hitrates
(cache-hit-rate) end
EOF
pass;