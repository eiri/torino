#!/bin/bash
set -euo pipefail

if [[ $# != 2 || -z "$1" || ! -f "$2" || "$2" != *.uf2 ]]; then
    echo "usage: bash scripts/flash.sh <board-serial> <firmware.uf2>" >&2
    exit 1
fi

serial="$1"
image="$2"
picotool="${PICOTOOL:-picotool}"

# The amplifier must be disconnected and the BOOT volume unmounted first.
"$picotool" info -a --ser "$serial"
"$picotool" info -a "$image"

# Failed verification leaves the board in BOOT mode.
"$picotool" load -v "$image" --ser "$serial"
"$picotool" reboot --ser "$serial"
