#!/bin/sh
# shellcheck disable=SC2154

export XDG_CACHE_HOME="$SNAP_USER_COMMON/.cache"
export XDG_CONFIG_HOME="$SNAP_USER_COMMON/.config"
export XDG_DATA_HOME="$SNAP_USER_COMMON/.local/share"

exec "$@"
