#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later

## @file
## @brief Prepare full and minimal AppImages for uploading to the Latest release on GitHub.
##
## This script should be run after a new release of Geeqie has been made, and after
## new AppImages have been created in Continuous Build on GitHub.
##
## Download AppImages from Continuous Build, remove "latest" from the file name
## and replace the text with the version number.
##
## The renamed files may then be uploaded to the Latest Release section on GitHub.
##
## @FIXME If the latest version is a patch version, the AppImage will only show
## the major/minor version plus git commit and not the patch version.
##

tmp_dir=$(mktemp -d "${TMPDIR:-/tmp}/geeqie.XXXXXXXXXX")
tmp_file=$(mktemp  "$tmp_dir/geeqie.XXXXXXXXXX")

latest_tag=$(git tag | tail -1)
latest_version="${latest_tag#?}"

cd "$tmp_dir" || exit

minimal=""
architecture="x86_64"
wget --no-verbose --show-progress --output-file="$tmp_file" "https://github.com/BestImageViewer/geeqie/releases/download/continuous/Geeqie$minimal-latest-$architecture.AppImage"
new_name="Geeqie-$latest_version$minimal-$architecture.AppImage"
mv "Geeqie$minimal-latest-$architecture.AppImage" "$new_name"

minimal="-minimal"
architecture="x86_64"
wget --no-verbose --show-progress --output-file="$tmp_file" "https://github.com/BestImageViewer/geeqie/releases/download/continuous/Geeqie$minimal-latest-$architecture.AppImage"
new_name="Geeqie-$latest_version$minimal-$architecture.AppImage"
mv "Geeqie$minimal-latest-$architecture.AppImage" "$new_name"

minimal=""
architecture="aarch64"
wget --no-verbose --show-progress --output-file="$tmp_file" "https://github.com/BestImageViewer/geeqie/releases/download/continuous/Geeqie$minimal-latest-$architecture.AppImage"
new_name="Geeqie-$latest_version$minimal-$architecture.AppImage"
mv "Geeqie$minimal-latest-$architecture.AppImage" "$new_name"

minimal="-minimal"
architecture="aarch64"
wget --no-verbose --show-progress --output-file="$tmp_file" "https://github.com/BestImageViewer/geeqie/releases/download/continuous/Geeqie$minimal-latest-$architecture.AppImage"
new_name="Geeqie-$latest_version$minimal-$architecture.AppImage"
mv "Geeqie$minimal-latest-$architecture.AppImage" "$new_name"

rm "$tmp_file"

echo "$tmp_dir"
