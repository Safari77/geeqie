#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later

## @file
## @brief Check for temporary comments
##
## $1 Source file to check
##
## Look for //~
##

res=$(grep --line-number '//~' "$1")

if [ -z "$res" ]
then
	exit 0
else
	printf "%s\n" "$res"
	exit 1
fi
