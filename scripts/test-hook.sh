#!/bin/sh
# The same stand-in as test-hook.cmd, for Linux and macOS. See that file for
# what it is for; the arguments and the output file are identical so a test
# written against one reads the same against the other.
#
#   test-hook.sh <label> [exit-code] [seconds-to-take]

label="${1:-no-label}"
code="${2:-0}"
delay="${3:-}"

if [ -n "$XDG_CONFIG_HOME" ]; then
    outdir="$XDG_CONFIG_HOME/rclone-browser/logs"
elif [ "$(uname)" = "Darwin" ]; then
    outdir="$HOME/Library/Preferences/rclone-browser/logs"
else
    outdir="$HOME/.config/rclone-browser/logs"
fi

mkdir -p "$outdir" 2>/dev/null
printf '%s  label=%s  exit=%s  cwd=%s\n' \
    "$(date -Iseconds)" "$label" "$code" "$PWD" >> "$outdir/hook-runs.txt"

[ -n "$delay" ] && sleep "$delay"

exit "$code"
