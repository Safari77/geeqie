#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later

## @file
## @brief Check for single value enums
##
## $1 Source file to check
##
## The expected format is: \n
## enum { \n
##  bookmark_drop_types_n = 3 \n
## }; \n
##
## The grep operation prepends line numbers so resulting in: \n
## 123:enum { \n
## 124- bookmark_drop_types_n = 3 \n
## 125-};
##

res=$(grep --line-number --after-context=2 'enum\ {$' "$1" | grep --before-context=2 '^[[:digit:]]\+\-};')

if [ -z "$res" ]
then
	exit 0
else
	printf "%s\n" "$res"
	exit 1
fi
