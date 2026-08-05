#!/bin/sh
set -eu

log() {
    echo "[cont-init.d] $(basename "$0"): $*"
}

# HOME and the XDG directories point at /config (see the Dockerfile), so the
# remotes, the saved tasks and the per-job logs all land in the volume. The
# application creates its own subdirectories.
mkdir -p /config

# "chown -R /config/*" fails on a fresh, empty volume because the glob does
# not expand. Chown the directory itself.
chown -R "$USER_ID:$GROUP_ID" /config

# Use the rclone.conf this machine already has, if there is one.
#
# compose mounts the platform's usual rclone directory at /rclone-config --
# the rclone folder under %APPDATA% on Windows, $HOME/.config/rclone
# elsewhere -- so remotes set up outside the container are simply there.
#
# Done by pointing /config/rclone at it rather than by setting RCLONE_CONFIG,
# so that "docker exec <container> rclone listremotes" agrees with what the
# application is using. An environment variable exported around the
# application would not be visible to exec, and the two would disagree about
# which remotes exist.
#
# A directory symlink, not a file one: rclone saves by writing a temporary
# file beside the config and renaming it over the top. Through a symlinked
# file that rename replaces the link itself and silently detaches from the
# host copy; through a symlinked directory it happens inside the mount, where
# it works.
#
# With no /rclone-config, or one without an rclone.conf, nothing is linked and
# rclone falls back to its own default, which HOME puts at
# /config/rclone/rclone.conf inside the volume. rclone creates the file there
# when the first remote is added.
if [ -f /rclone-config/rclone.conf ]; then
    if [ -L /config/rclone ]; then
        rm -f /config/rclone
    elif [ -d /config/rclone ] && [ -n "$(ls -A /config/rclone 2>/dev/null)" ]; then
        log "WARNING: /config/rclone already holds a configuration"
        log "WARNING: leaving it alone and ignoring /rclone-config"
    elif [ -d /config/rclone ]; then
        rmdir /config/rclone
    fi

    if [ ! -e /config/rclone ]; then
        ln -s /rclone-config /config/rclone
        log "using the rclone.conf from /rclone-config"
    fi
elif [ -d /rclone-config ]; then
    log "/rclone-config holds no rclone.conf; using the one in /config"
else
    log "no /rclone-config mounted; using the one in /config"
fi

# An rclone supplied from outside takes precedence over the one in the image,
# for anyone who wants to stay on the exact build they already run:
#
#   docker run -v /usr/local/bin/rclone:/opt/rclone/rclone:ro ...
#
# It has to be a Linux binary for the container's architecture. A Windows
# rclone.exe cannot run here whatever it is mounted as, so on Windows this
# means supplying a Linux build rather than the installed one.
#
# Checked by running it, so a binary for the wrong platform says so here
# instead of leaving the application reporting that it cannot determine the
# rclone version.
if [ -e /opt/rclone/rclone ]; then
    if /opt/rclone/rclone version >/dev/null 2>&1; then
        ln -sf /opt/rclone/rclone /usr/bin/rclone
        log "using the mounted rclone: $(/opt/rclone/rclone version | head -1)"
    else
        log "WARNING: /opt/rclone/rclone will not run here and was ignored"
        log "WARNING: it has to be a Linux $(uname -m) binary"
        log "WARNING: continuing with the bundled $(rclone version | head -1)"
    fi
fi

log "ready"
