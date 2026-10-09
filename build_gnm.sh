#!/bin/sh
set -eu
kgfx_root=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
exec python3 "$kgfx_root/scripts/build_gnm.py" "$@"
