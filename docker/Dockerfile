# Rclone Browser in a container, reachable through a browser over noVNC.
#
# Moved here from totza2010/rclonebrowser-docker so that the image is built
# from the commit it ships, rather than from whatever master happened to be
# when the image was built. The old file ran "git clone ... /tmp" inside its
# own WORKDIR, which pinned nothing and overwrote the working directory.
#
# The base image draws the desktop and serves it over noVNC. That is pixel
# streaming, not a web interface: heavy on the host, awkward on a phone, and
# it offers no way to script anything. The plan is to replace it with a real
# HTTP interface served by the application, at which point this image loses
# X11, openbox and noVNC entirely. Until then it is kept current rather than
# improved.

ARG ALPINE_VERSION=3.24
ARG BASEIMAGE_VERSION=v4.12.6

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

FROM jlesage/baseimage-gui:alpine-${ALPINE_VERSION}-${BASEIMAGE_VERSION}

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

# Fonts are not optional here: without one the base image has no glyphs at
# all and every label in the application renders as an empty box, menus
# included. The previous compose file pulled them at container start
# through INSTALL_PACKAGES, which re-downloaded them on every start and
# left the first run looking broken.
#
# font-noto-thai is here because the remote and folder names in use are
# Thai. font-noto-cjk is left out at roughly 100 MB; add it through
# INSTALL_PACKAGES if you need it.
RUN apk add --no-cache \
        ca-certificates \
        dbus \
        font-dejavu \
        font-noto \
        font-noto-thai \
        fontconfig \
        fuse3 \
        qt6-qtbase \
        qt6-qtbase-x11 \
        qt6-qtmultimedia \
        xterm \
    && version="${RCLONE_VERSION}" \
    && if [ "$version" = "latest" ]; then \
           version="$(wget -qO- "https://api.github.com/repos/${RCLONE_REPO}/releases/latest" \
                      | sed -n 's/.*"tag_name"[[:space:]]*:[[:space:]]*"v\{0,1\}\([^"]*\)".*/\1/p' \
                      | head -1)"; \
           [ -n "$version" ] || { echo "cannot resolve the latest ${RCLONE_REPO} release" >&2; exit 1; }; \
           echo "resolved ${RCLONE_REPO} latest to v${version}"; \
       fi \
    && wget -q -O /tmp/rclone.zip \
        "https://github.com/${RCLONE_REPO}/releases/download/v${version}/rclone-v${version}-linux-${TARGETARCH}.zip" \
    && unzip -q /tmp/rclone.zip -d /tmp \
    && mv /tmp/rclone-*-linux-${TARGETARCH}/rclone /usr/bin/rclone \
    && chmod +x /usr/bin/rclone \
    && rm -rf /tmp/rclone* \
    # Recorded so a running container can say where its rclone came from
    # without anyone having to remember how it was built.
    && printf '%s v%s\n' "${RCLONE_REPO}" "$version" > /etc/rclone-source

COPY --from=build /build/build/rclone-browser /usr/bin/rclone-browser

# Only the initial window is maximised.
RUN sed-patch 's/<application type="normal">/<application type="normal" title="Rclone Browser">/' \
        /etc/xdg/openbox/rc.xml

RUN APP_ICON_URL=https://github.com/rclone/rclone/raw/master/graphics/logo/logo_symbol/logo_symbol_color_512px.png \
    && install_app_icon.sh "$APP_ICON_URL"

COPY docker/rootfs/ /
COPY VERSION /
RUN chmod +x /startapp.sh

ENV APP_NAME="RcloneBrowser" \
    S6_KILL_GRACETIME=8000

# The base image leaves HOME at /, which puts rclone.conf in /.config and the
# application's own data in /.local/share -- both inside the container's
# writable layer, so every recreate loses the remotes, the saved tasks and the
# job logs. Mounting a volume at /config did nothing about that, because
# nothing was ever written there.
#
# Pointing HOME and the XDG directories at /config puts all of it inside the
# declared volume:
#
#   /config/rclone/rclone.conf                     the remotes
#   /config/rclone-browser/rclone-browser/         tasks, scheduler, settings
#   /config/rclone-browser/rclone-browser/logs/    per-job logs
ENV HOME=/config \
    XDG_CONFIG_HOME=/config \
    XDG_DATA_HOME=/config

# Declared so a plain "docker run" persists them without compose. Name them to
# be able to find them again:
#   docker run -v rclonebrowser-config:/config -v rclonebrowser-media:/media ...
VOLUME ["/config"]
VOLUME ["/media"]

LABEL \
      org.opencontainers.image.title="rclonebrowser" \
      org.opencontainers.image.description="Rclone Browser" \
      org.opencontainers.image.source="https://github.com/totza2010/RcloneBrowser" \
      org.rclonebrowser.rclone-repo="${RCLONE_REPO}"
