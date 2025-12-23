#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later

## @file
## @brief Update the Geeqie webpage Help files
##
## It assumes that the main geeqie project folder and the
## <em>geeqie.github.io</em> folder are at the same level.
##
## e.g.
## @code
##            /
##            |
##        somewhere
##            |
##     _______________
##     |             |
##   geeqie    geeqie.github.io
## @endcode
##
## Files in <em>./doc/html</em> are regenerated and copied to the webpage folder.
##
## After the script has run, <em>git diff</em> will show any changes that
## require a <em>git commit</em> and <em>git push</em> to be made.
##

SCRIPT_DIR="$(dirname "$0")"
# shellcheck disable=SC1091
. "$SCRIPT_DIR/../build-aux/goto-project-root.sh"
find_project_root || exit 1

if [ ! -d "../geeqie.github.io/.git" ] || [ ! -d "../geeqie.github.io/help" ]
then
	printf '%s\n' "The Geeqie webpage project folder geeqie.github.io was not found"
	exit 1
fi

ninja -C build

find ../geeqie.github.io/help/ -type f -exec rm "{}" \;
cp -a build/doc/html/* ../geeqie.github.io/help
cp -a build/doc/help.pdf ../geeqie.github.io/help-pdf

exit 0
