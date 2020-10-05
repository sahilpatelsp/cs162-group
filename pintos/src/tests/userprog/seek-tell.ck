# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected ([<<'EOF']);
(seek-tell) begin
(seek-tell) create "test.txt"
(seek-tell) open "test.txt"
(seek-tell) end
seek-tell: exit(0)
EOF
pass;
