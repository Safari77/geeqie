#! /bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later

## @file
## @brief List keyword-mark links
##
## Because this script uses the Geeqie configuration file, changes
## made during a session will not be listed until Geeqie closes and
## the config. file is updated.

awk -F '"' '/keyword.* mark/ {print $2, $6}' "$HOME"/.config/geeqie/geeqierc.xml | tr -d '"'
