#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later

## @file
## @brief Look for function calls that do not conform to GTK4 migration format
##
## The files compat.cc and compat.h contain functions that should be used instead
## of the standard GTK calls. The compatibility calls are all prefixed by gq_
##
## Create a list of function names to be looked for by searching
## compat.cc and compat.h for identifiers prefixed by gq_gtk_
##
## Search the input file for any of the above gtk_ function names that
## are not prefixed by gq_
##
## $1 full path to file to process
## $2 full path to compat.cc
## $3 full path to compat.h
##

exit_status=0

if [ ! "${1#*compat.cc}" = "$1" ]
then
	exit "$exit_status"
fi

if [ ! "${1#*compat.h}" = "$1" ]
then
	exit "$exit_status"
fi

compat_functions=$(grep --only-matching --no-filename 'gq_gtk_\(\(\([[:alnum:]]*\)\_*\)*\)' "$2" "$3" | sort | uniq | cut --characters 4-)

while read -r line
do
	if grep --line-number --perl-regexp '(?<!gq_)'"$line" "$1"
	then
		exit_status=1
	fi
done << EOF
$compat_functions
EOF

exit "$exit_status"
