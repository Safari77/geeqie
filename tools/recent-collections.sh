#! /bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later

## @file
## @brief List recently used Collections
##
## This may be useful if the user has Collections outside
## the default directory for Collections.
## Although the Recent Collections menu item has been deleted,
## the history list is still maintained.

awk -W posix '
BEGIN {
    LINT = "fatal"
    found = 0
}

/\[recent\]/ {
    found = 1
    next
}

/^$/ {
    found = 0
}

{
    if (found == 1) {
        print
    }
}
' "$HOME"/.config/geeqie/history | tr -d '"'
