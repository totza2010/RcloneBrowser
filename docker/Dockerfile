# Rclone Browser in a container: one image, three ways to run it.
#
#   RB_MODE=web       the core and its own web interface, and nothing else
#   RB_MODE=both      the window as well, streamed to a browser (the default)
#   RB_MODE=desktop   the window only; the web interface and API are off
#
# Moved here from totza2010/rclonebrowser-docker so that the image is built
# from the commit it ships, rather than from whatever master happened to be
# when the image was built.
#
# The window is streamed by Selkies rather than noVNC. Both are pixels down a
# socket, but Selkies encodes them as video, serves them over HTTPS and works
# from a phone. Because an image that offers the window at all has to carry
# something to show it with, the streaming stack is always in the image; in
# web mode it is simply never started. See docs/DOCKER-MODES.md.

ARG ALPINE_VERSION=3.24
# The Selkies base names its tags after the Alpine release without the dot.
ARG SELKIES_TAG=alpine324

# The builder must match the runtime's Alpine release, or the Qt libraries it
# links against will not be the ones present at run time.
FROM alpine:${ALPINE_VERSION} AS build

RUN apk add --no-cache \
        build-base \
        cmake \
        ninja \
        qt6-qtbase-dev \
        qt6-qtmultimedia-dev \
        qt6-qttools-dev

# The sources come from the build context, so the binary always matches this
# commit.
COPY . /src

RUN cmake -S /src -B /build -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DRB_BUILD_TESTS=OFF \
    && cmake --build /build --parallel

# ---- The web-only image: docker build --target web ---------------------------
#
# The daemon and its web page and nothing to draw a window with: no desktop,
# no streaming, no X server, no QtWidgets. For a server or a NAS where nobody
# will ever look at the window, at a fraction of the size. It is RB_MODE=web
# of the image below, without carrying the other two modes.

FROM alpine:${ALPINE_VERSION} AS build-web

RUN apk add --no-cache build-base cmake ninja qt6-qtbase-dev

COPY . /src

RUN cmake -S /src -B /build -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DRB_BUILD_TESTS=OFF \
        -DRB_BUILD_GUI=OFF \
    && cmake --build /build --parallel

FROM alpine:${ALPINE_VERSION} AS web

ARG RCLONE_REPO=tgdrive/rclone
ARG RCLONE_VERSION=latest
ARG TARGETARCH=amd64

# qt6-qtbase-sqlite: the run history's database driver, a package of its own
# on Alpine (see the image below). shadow and su-exec: the entrypoint gives
# the user the ids it is asked for, then runs the program as that user.
COPY docker/install-rclone.sh /usr/lib/rclone-browser/install-rclone.sh
RUN apk add --no-cache \
        ca-certificates \
        fuse3 \
        qt6-qtbase \
        qt6-qtbase-sqlite \
        shadow \
        su-exec \
        tzdata \
    && sh /usr/lib/rclone-browser/install-rclone.sh "${RCLONE_REPO}" "${RCLONE_VERSION}" "${TARGETARCH}" \
    && addgroup -g 1000 abc \
    && adduser -D -u 1000 -G abc -h /config -s /bin/sh abc \
    && mkdir -p /config /media \
    # The user's, as on the image with the window: a new named volume takes
    # the owner of the folder it is mounted over, and a root /media was one
    # nothing could be downloaded into (2026-10-21).
    && chown abc:abc /config /media \
    && echo "web" > /etc/rclone-browser-image

COPY --from=build-web /build/build/rclone-browser /usr/bin/rclone-browser
# The same preparation the image below runs at start, from the same file.
COPY docker/rootfs/etc/s6-overlay/s6-rc.d/init-rclonebrowser/run /usr/lib/rclone-browser/init.sh
COPY docker/web/entrypoint.sh /entrypoint.sh
COPY VERSION /
RUN chmod +x /entrypoint.sh

# The same layout and the same boundaries as RB_MODE=web of the image below,
# so a volume moves from one to the other as it is.
ENV RB_MODE=web \
    RB_LOCAL_ROOTS=/media \
    RB_API_ADDRESS=0.0.0.0 \
    HOME=/config \
    XDG_CONFIG_HOME=/config \
    XDG_DATA_HOME=/config

EXPOSE 19999
VOLUME ["/config"]
ENTRYPOINT ["/entrypoint.sh"]

# ---- The image with the window (the default) ----------------------------------

FROM ghcr.io/linuxserver/baseimage-selkies:${SELKIES_TAG}

# Which rclone goes in the image.
#
# The default is tgdrive's fork, which carries the teldrive backend mainline
# rclone does not have, and which is why this project exists. The cost is that
# the fork trails: it was on 1.73.1 when mainline was on 1.75.0, so anyone who
# does not need teldrive was being handed an older rclone for no reason.
# RCLONE_REPO lets them take mainline instead.
#
# The version resolves at build time. A pinned default would have to be
# bumped by hand, and nobody remembers to: the image this replaced sat on
# rclone 1.71.0 and Alpine 3.16 long after both had moved on. Anyone who wants
# a fixed version passes one, which is the right way round -- the person who
# needs a pin knows they need it, and everyone else gets something current.
#
#   --build-arg RCLONE_VERSION=1.73.1
#
# Which version an image ended up with is recorded in /etc/rclone-source, so
# it can always be read back off a container.
ARG RCLONE_REPO=tgdrive/rclone
ARG RCLONE_VERSION=latest
ARG TARGETARCH=amd64

COPY docker/install-rclone.sh /usr/lib/rclone-browser/install-rclone.sh

# font-noto-thai is here because the remote and folder names in use are
# Thai. font-noto-cjk is left out at roughly 100 MB.
#
# qt6-qtbase-wayland is 2.4 MB and is what lets the window run when the base
# is switched to Wayland (PIXELFLUX_WAYLAND=true). The base starts on X11, so
# qt6-qtbase-x11 is what is used by default. The compositor half of Qt's
# Wayland support (qt6-qtwayland) is not needed.
#
# qt6-qtbase-sqlite is a separate package on Alpine and is not pulled in by
# qt6-qtbase. Without it the driver is missing and the run history silently
# fails to persist -- see docs/PLAN.md 6.8.
RUN apk add --no-cache \
        ca-certificates \
        dbus \
        font-dejavu \
        font-noto \
        font-noto-thai \
        fontconfig \
        fuse3 \
        qt6-qtbase \
        qt6-qtbase-sqlite \
        qt6-qtbase-wayland \
        qt6-qtbase-x11 \
        qt6-qtmultimedia \
        xterm \
    && sh /usr/lib/rclone-browser/install-rclone.sh "${RCLONE_REPO}" "${RCLONE_VERSION}" "${TARGETARCH}"

COPY --from=build /build/build/rclone-browser /usr/bin/rclone-browser

COPY docker/rootfs/ /
COPY VERSION /
# Alpine's empty cdrom, floppy and usb in /media mean nothing in a container,
# and /media is what the page's folder chooser shows first. rmdir, so only
# while empty.
RUN chmod +x /rb-init /usr/bin/rb-desktop \
        /etc/s6-overlay/s6-rc.d/init-rclonebrowser/run \
        /etc/s6-overlay/s6-rc.d/svc-rclonebrowser/run \
    && (rmdir /media/cdrom /media/floppy /media/usb 2>/dev/null || true)

# TITLE is what the streamed page calls itself.
#
# S6_SERVICES_GRACETIME is how long the supervisor waits for services to stop
# before it starts killing. The application takes up to eight seconds to stop
# its transfers and write down what became of them, so the default of three
# would cut that short -- which is the whole thing stopping politely was
# written to avoid. "docker stop" needs to be given at least as long:
# compose.yaml sets stop_grace_period.
# RB_LOCAL_ROOTS is how far the web interface may look on this side: the
# folders it can upload from and download to. In a container that is what
# was mounted in from the host, and /media is where compose puts it. More
# than one is separated by semicolons. See docs/WEB-API-PLAN.md.
ENV TITLE="Rclone Browser" \
    RB_MODE=both \
    RB_LOCAL_ROOTS=/media \
    S6_SERVICES_GRACETIME=10000

# The base image sets HOME to /config, which is the volume. The application's
# own files are put directly in it rather than under /config/.config, by
# pointing XDG at /config for the application alone (see rb-desktop and the
# service's run file) -- the same layout the previous image used, so a volume
# made by that one is picked up as it is:
#
#   /config/rclone/rclone.conf                     the remotes
#   /config/rclone-browser/rclone-browser/         tasks, scheduler, settings
#   /config/rclone-browser/rclone-browser/logs/    per-job logs
#
# XDG is not set for the whole container, because the base image keeps its
# own desktop configuration under $HOME/.config and looks for it there.

# 3001  the streamed window, HTTPS with a self-signed certificate
# 3000  the same over plain HTTP
# 19999 the application's own web interface and API
EXPOSE 3000 3001 19999

# Declared so a plain "docker run" persists them without compose. Name them to
# be able to find them again:
#   docker run -v rclonebrowser-config:/config -v rclonebrowser-media:/media ...
VOLUME ["/config"]

# Wrapped so the mode can decide which services exist before the supervisor
# reads the list. See /rb-init.
ENTRYPOINT ["/rb-init"]
