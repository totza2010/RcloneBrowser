#!/bin/sh
# The web-only image's start: the daemon and its web page, nothing to draw a
# window with. What the image with the window does through its base image's
# supervisor is done here by hand, in the same order:
#
#   1. the user the files belong to takes the ids it was asked for
#      (PUID / PGID, 1000 unless said -- as on the other image)
#   2. the same preparation the other image runs (init-rclonebrowser): the
#      volume handed to that user, Alpine's empty folders taken out of /media,
#      which rclone to use, the rclone.conf from /rclone-config
#   3. the program, as that user, as the process the container's stop
#      signal reaches -- which is what lets it stop its transfers and write
#      down what became of them before it goes
set -eu

PUID="${PUID:-1000}"
PGID="${PGID:-1000}"
if [ "$(id -g abc)" != "$PGID" ]; then
    groupmod -o -g "$PGID" abc
fi
if [ "$(id -u abc)" != "$PUID" ]; then
    usermod -o -u "$PUID" abc
fi

sh /usr/lib/rclone-browser/init.sh

cd /config
exec su-exec abc:abc /usr/bin/rclone-browser --daemon
