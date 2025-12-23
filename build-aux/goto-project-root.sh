#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later

## @file
## @brief Change dir to the project root.
##
## Used in scripts to always start at the project root level.
##

# Files/directories that uniquely identify the Geeqie project root
ROOT_MARKERS="
build-aux
data
doc
po
snap
src
"

# Return 0 if the current directory is the project root
is_project_root()
{
	for m in $ROOT_MARKERS
	do
		[ -e "$m" ] || return 1
	done
	return 0
}

# Find the project root by walking upward from the current directory.
# If found, cd into it and export GQ_PROJECT_ROOT.
# If not found, print an error and return non-zero.
find_project_root()
{
	START_DIR=$PWD
	GQ_PROJECT_ROOT=""

	if is_project_root
	then
		GQ_PROJECT_ROOT=$PWD
	else
		while [ "$PWD" != "/" ]
		do
			cd .. || break
			if is_project_root
			then
				GQ_PROJECT_ROOT=$PWD
				break
			fi
		done
	fi

	if [ -n "$GQ_PROJECT_ROOT" ]
	then
		cd "$GQ_PROJECT_ROOT" || {
			echo "Error: Cannot enter project root directory: $GQ_PROJECT_ROOT" >&2
			return 1
		}
		export GQ_PROJECT_ROOT
		return 0
	fi

	echo "Error: Geeqie project root not found (search started from: $START_DIR)" >&2
	return 1
}
