#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later

## @file
## @brief Install additional files for Geeqie development purposes
##

SCRIPT_DIR="$(dirname "$0")"
# shellcheck disable=SC1091
. "$SCRIPT_DIR/../build-aux/goto-project-root.sh"
find_project_root || exit 1

if ! zenity --title="Install files for Geeqie development" --question --no-wrap --text "This script will install:\n
<tt>
curl                # for this script
default-jre         # for doxygen diagrams
libdw-dev           # for devel=enabled
libdwarf-dev        # for devel=enabled
libunwind-dev       # for devel=enabled
mdl                 # for meson tests (markdown files)
shellcheck          # for meson tests (shell scripts)
texlive-font-utils  # for doxygen diagrams
xvfb                # for meson tests
</tt>

The following will be downloaded to $HOME/bin/ and made executable:
<tt>
https://github.com/plantuml/plantuml/releases/download/<i>latest</i>/plantuml-<i>latest</i>.jar  # for doxygen diagrams
https://raw.githubusercontent.com/Anvil/bash-doxygen/master/doxygen-bash.sed       # for documenting script files\
</tt>

The following snap will be installed:
<tt>
snapd    # for installing snaps
mdl      # for checking markdown files\n
</tt>

Continue?"
then
    exit 0
fi

if ! mkdir -p "$HOME"/bin
then
    printf "Cannot create %s\n" "$HOME"/bin
    exit 1
fi

sudo apt update

sudo apt install curl
sudo apt install default-jre
sudo apt install libdw-dev
sudo apt install libdwarf-dev
sudo apt install libunwind-dev
sudo apt install shellcheck
sudo apt install texlive-font-utils
sudo apt install xvfb

sudo apt install snapd
sudo snap install mdl

cd "$HOME"/bin || exit 1

if ! [ -f doxygen-bash.sed ]
then
    wget https://raw.githubusercontent.com/Anvil/bash-doxygen/master/doxygen-bash.sed
    chmod +x doxygen-bash.sed
fi

latest_plantuml=$(curl --silent -qI https://github.com/plantuml/plantuml/releases/latest | awk -W posix -F '/' 'BEGIN {LINT = "fatal"} /^location/ {print  substr($NF, 1, length($NF)-1)}')

if ! [ -f "plantuml-${latest_plantuml#v}.jar" ]
then
    wget "https://github.com/plantuml/plantuml/releases/download/$latest_plantuml/plantuml-${latest_plantuml#v}.jar"
    ln --symbolic --force "plantuml-${latest_plantuml#v}.jar" plantuml.jar
    chmod +x plantuml.jar
fi
