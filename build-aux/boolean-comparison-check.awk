#!/usr/bin/awk -f
#
## @file
## @brief Check for boolean comparison
##
## https://docs.gtk.org/glib/types.html#gboolean
## states:
## Never directly compare the contents of a gboolean variable with the values TRUE or FALSE.
## Use if (condition) to check a gboolean is ‘true’, instead of if (condition == TRUE).
## Likewise use if (!condition) to check a gboolean is ‘false’.
##
## Note that clang-tidy -readability-simplify-boolean-expr will not detect:
## if (a == FALSE)
##

BEGIN {
    found = 0
}

/==[[:space:]]*(FALSE|TRUE)/ {
    printf("%s:%d: comparison to FALSE|TRUE\n", FILENAME, FNR)
    print "  " $0

    found = 1
}

END {
    exit found
}
