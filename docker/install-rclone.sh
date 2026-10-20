#!/bin/sh
# Puts the rclone an image ships with in /usr/lib/rclone-bundled, links it
# from /usr/bin/rclone, and records where it came from in /etc/rclone-source.
# Used by both images (the one with the window and the web-only one), so they
# cannot come to install rclone differently.
#
#   install-rclone.sh <repo> <version|latest> <amd64|arm64>
set -eu

repo="$1"
version="$2"
arch="$3"

if [ "$version" = "latest" ]; then
    version="$(wget -qO- "https://api.github.com/repos/${repo}/releases/latest" |
        sed -n 's/.*"tag_name"[[:space:]]*:[[:space:]]*"v\{0,1\}\([^"]*\)".*/\1/p' |
        head -1)"
    [ -n "$version" ] || { echo "cannot resolve the latest ${repo} release" >&2; exit 1; }
    echo "resolved ${repo} latest to v${version}"
fi
version="${version#v}"

wget -q -O /tmp/rclone.zip \
    "https://github.com/${repo}/releases/download/v${version}/rclone-v${version}-linux-${arch}.zip"
unzip -q /tmp/rclone.zip -d /tmp

# Kept out of /usr/bin, which holds only a link to whichever rclone is in use.
# The container may be told at start to use a different one (RB_RCLONE_REPO,
# RB_RCLONE_VERSION, or one mounted at /opt/rclone), and whatever happens to
# that choice, this one is still here to fall back on.
mkdir -p /usr/lib/rclone-bundled
mv /tmp/rclone-*-linux-"${arch}"/rclone /usr/lib/rclone-bundled/rclone
chmod +x /usr/lib/rclone-bundled/rclone
ln -sf /usr/lib/rclone-bundled/rclone /usr/bin/rclone
rm -rf /tmp/rclone*

# So a running container can say where its rclone came from without anyone
# having to remember how it was built.
printf '%s v%s\n' "$repo" "$version" > /etc/rclone-source
