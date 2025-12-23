#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later

## @file
## @brief Run doxygen for the Geeqie project
##
## The environment variables needed to generate the
## Geeqie doxygen documentation are set up here.
##
## $1 Destination directory. If blank, default is ../doxygen
##

SCRIPT_DIR="$(dirname "$0")"
# shellcheck disable=SC1091
. "$SCRIPT_DIR/../build-aux/goto-project-root.sh"
find_project_root || exit 1

if [ -n "$1" ]
then
	DOCDIR="$1"
else
	DOCDIR="$PWD"/../doxygen
fi

if ! mkdir -p "$DOCDIR"/
then
	printf "Cannot create %s\n" "$DOCDIR"
	exit 1
fi

export DOCDIR
export SRCDIR="$PWD"
export PROJECT="Geeqie"
VERSION=$(git -C "$PWD" tag --list v[1-9]* | tail -n 1)
export VERSION
export PLANTUML_JAR_PATH="$HOME"/bin/plantuml.jar
export INLINE_SOURCES=YES
export STRIP_CODE_COMMENTS=NO

# Set doxygen.conf parameters so that searchdata.xml is generated
EXTERNAL_SEARCH="YES"
SERVER_BASED_SEARCH="YES"
export EXTERNAL_SEARCH
export SERVER_BASED_SEARCH

doxygen "doc/doxygen.conf"

tmp_searchdata_xml=$(mktemp "${TMPDIR:-/tmp}/geeqie.XXXXXXXXXX")
mv "$DOCDIR/searchdata.xml" "$tmp_searchdata_xml"

# Run again with default settings so that the html search box is generated
EXTERNAL_SEARCH="NO"
SERVER_BASED_SEARCH="NO"

doxygen "doc/doxygen.conf"

mv "$tmp_searchdata_xml" "$DOCDIR/searchdata.xml"
