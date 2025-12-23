#! /bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later

## @file
## @brief Use desktop-file-validate on .desktop files.
##
## $1 temp directory \n
## $2 desktop file name including .in extension \n
##
## desktop-file-validate will not process a file with
## the extension ".in". Use a symlink as a workaround.

if [ ! -d "$1" ]
then
	mkdir --parents "$1"
fi

desktop_file=$(basename "$2" ".in")

ln --symbolic "$2" "$1/$desktop_file"

result=$(desktop-file-validate "$1/$desktop_file")

rm "$1/$desktop_file"

if [ -z "$result" ]
then
	exit 0
else
	printf "%s\n" "$result"

	exit 1
fi
