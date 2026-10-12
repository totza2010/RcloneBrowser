# Rclone Browser in a container

One image, three ways to run it. `RB_MODE` chooses:

| `RB_MODE` | what runs | where to look |
|---|---|---|
| `web` | the application and its own web interface, no window | `http://localhost:19999` |
| `both` (default) | that, and the window streamed to a browser | `https://localhost:3001` as well |
| `desktop` | the streamed window only; web interface and API off | `https://localhost:3001` |

```bash
cp docker/.env.example docker/.env    # then set RB_MODE and the passwords
docker compose -f docker/compose.yaml up -d
```

The **web interface** is the application's own: queue, tasks, schedules,
history, remotes. It asks for the token the container prints when it starts
(`docker logs rclonebrowser`), and for the administrator password once one
has been set. It cannot yet create or edit a task or a remote, which is what
the window is still needed for.

The **streamed window** is the desktop application itself, drawn inside the
container and sent as video by [Selkies](https://docs.linuxserver.io/selkies).
It is served over HTTPS with a self-signed certificate, so the browser warns
the first time.

In `both`, the window and the web interface are two views of one running
program: start a transfer in one and it appears in the other.

Published to `ghcr.io/totza2010/rclonebrowser`. Nothing is pushed except
from a `v*` tag.

## Which rclone, and which tag

Two variants, differing only in the rclone inside.

A tag says which rclone is inside and which version of it, because that is
the part that changes on its own. The application version comes first where
it is pinned:

| tag | application | rclone | moves? |
|---|---|---|---|
| `3.1.1-tgdrive-1.73.1` | 3.1.1 | [tgdrive fork](https://github.com/tgdrive/rclone) 1.73.1 | never |
| `3.1.1-rclone-1.75.0` | 3.1.1 | [mainline](https://github.com/rclone/rclone) 1.75.0 | never |
| `tgdrive-1.73.1` | newest release | fork 1.73.1 | on an application release |
| `rclone-1.75.0` | newest release | mainline 1.75.0 | on an application release |
| `latest` | newest release | newest fork | on either |
| `latest-mainline` | newest release | newest mainline | on either |

Pin the first form if you need to know exactly what you are running. Follow
`latest` if you would rather not think about it. Use the middle form to hold
an rclone version while still picking up application releases.

Only the fork carries the `teldrive` backend, which is why it is the default.
It does trail mainline -- 1.73.1 against 1.75.0 at the time of writing -- so
the mainline variant is there for anyone who does not need teldrive and would
rather have the newer rclone.

Which one an image actually got is recorded inside it, so a running container
can answer the question:

```bash
docker exec rclonebrowser cat /etc/rclone-source
# tgdrive/rclone v1.73.1
```

### Staying current

An image that was current when it was published stops being so a few weeks
later. A scheduled workflow checks both upstreams once a day and rebuilds
when either has a release the registry does not have a tag for yet, then
moves the tags above onto it.

It builds from the newest **release tag of this repository**, not from the
default branch. A rebuild triggered by rclone should change the rclone and
nothing else; building from the branch would quietly ship unreleased
application code under the same moving tags.

The registry is the state: if `:tgdrive-1.73.1` is already there, that
combination has been built, and nothing is remembered between runs. Run it by
hand from the Actions tab, with a force option, to rebuild anyway.

Before anything is published the image is checked for the backend set it
claims, for missing shared libraries, and for having ended up with the rclone
version that was asked for.

The version resolves at build time rather than being pinned. A pinned
default has to be bumped by hand and nobody remembers to: the image this
replaced sat on rclone 1.71.0 and Alpine 3.16 long after both had moved on.

Pin it if you want to, which is the right way round -- the person who needs a
fixed version knows they need one:

```bash
docker build -f docker/Dockerfile   --build-arg RCLONE_REPO=rclone/rclone   --build-arg RCLONE_VERSION=1.75.0   -t rclonebrowser:mainline .
```

## Choosing rclone without rebuilding

The tags above fix which rclone is *in* the image. Which one the container
*runs* can be changed at any start:

```ini
RB_RCLONE_REPO=rclone/rclone
RB_RCLONE_VERSION=1.75.0
```

Either may be left out: the repository defaults to the one the image was
built from, the version to `latest`. With both empty nothing is fetched and
the rclone in the image is used.

What is asked for is downloaded once into `/config/.rclone-bin` and kept, so
going back to a version already fetched needs no network, and `latest` falls
back to the newest kept when GitHub cannot be reached. The download is
checked against the checksum the release publishes; a release that publishes
none is used with a warning in the log saying so.

Every way this can fail -- no network, a version that does not exist, a
checksum that does not match -- ends with the container running the rclone
it shipped with and a line in `docker logs` saying why. It never ends with
no rclone.

The order, when more than one is given: an rclone mounted at `/opt/rclone`,
then these variables, then the image's own.

> The names are `RB_RCLONE_*` on purpose. rclone reads `RCLONE_<FLAG>` from
> its environment as that flag, so `RCLONE_VERSION=1.75.0` in a container
> makes every rclone command in it fail with *invalid argument "1.75.0" for
> "--version"*.

## Where the data goes

Everything lives under `/config`, which the image declares as a volume, so a
plain `docker run` persists it without needing compose:

```
/config/rclone/rclone.conf                    the remotes
/config/rclone-browser/rclone-browser.conf    settings
/config/rclone-browser/rclone-browser/        saved tasks, scheduler
/config/rclone-browser/rclone-browser/logs/   per-job logs
```

`/media` is what the container can see to transfer.

The base image leaves `HOME` at `/`, which put all of the above in the
container's writable layer, where a recreate lost it. The Dockerfile points
`HOME` and the XDG directories at `/config` instead. Mounting a volume there
previously achieved nothing, because nothing was being written to it.

Name the volumes to be able to find them again:

```bash
docker run -d --name rclonebrowser \
  -e RB_MODE=web \
  -v rclonebrowser-config:/config -v rclonebrowser-media:/media \
  -p 127.0.0.1:19999:19999 \
  --stop-timeout 20 \
  ghcr.io/totza2010/rclonebrowser:latest
```

Both default to Docker-managed volumes, which behave the same on Windows and
Linux and need no ownership fiddling. The previous compose file hard-coded
one person's home directory, which worked on exactly one machine.

To keep the data on the host instead, set these in `docker/.env`:

```ini
RB_CONFIG=/srv/rclonebrowser
RB_MEDIA=/mnt
```

or on Windows:

```ini
RB_CONFIG=D:\rclonebrowser
RB_MEDIA=D:\Media
```

Compose reads anything that is not a path as a volume name, which is what
makes one setting cover both cases. `PUID`/`PGID` only matter when
pointing at a host directory; set them to the owner of that directory.

`RB_CONFIG` replaces the whole directory, so it wants an empty one to fill.
It is not how an existing `rclone.conf` is reused -- that happens on its own,
see below.

## Your existing rclone.conf

The remotes already set up on the machine are picked up without configuring
anything. compose mounts wherever rclone keeps them:

| | |
|---|---|
| Windows | the `rclone` folder under `%APPDATA%` |
| Linux, macOS | `$HOME/.config/rclone` |

If that directory has an `rclone.conf`, the container uses it, and remotes
added through the interface are written back to it. If it does not, or the
mount was left out of a plain `docker run`, rclone falls back to
`/config/rclone/rclone.conf` in the volume and creates it on the first
remote. Nothing is copied or overwritten either way, and the log says which
one it settled on:

```
using the rclone.conf from /rclone-config
/rclone-config holds no rclone.conf; using the one in /config
no /rclone-config mounted; using the one in /config
```

Set `RB_RCLONE_CONF` in `docker/.env` to point somewhere else.

### Why it is done by symlink

`/config/rclone` becomes a symlink to the mounted directory, rather than
`RCLONE_CONFIG` being exported around the application.

An environment variable set for the application is not visible to
`docker exec`, so `docker exec rclonebrowser rclone listremotes` would list a
different set of remotes from the ones on screen. Through the symlink both
agree, because there is only one path involved.

It is a directory symlink and the mount is a directory, both for the same
reason: rclone saves by writing a temporary file beside the config and
renaming it over the top. Against a single mounted *file* that rename cannot
happen:

```
ERROR : Failed to save config after 10 tries: failed to move previous config
        to backup location: rename /etc/rclone.conf /etc/rclone.conf.old...:
        device or resource busy
```

Reading works, so it looks fine until the first remote is added -- and that
remote is gone on restart.

`RCLONE_CONFIG` is also never set to an empty string anywhere here. rclone
reads an empty value as a request to keep the configuration in memory:

```
$ rclone config file
Configuration is in memory only
```

Nothing is written and every remote disappears when the container stops.

Setting a configuration file path in Preferences overrides all of this, as
the application then passes `--config` explicitly.

## What the web interface can see of this side

Uploading and downloading from the web interface need the program to say
what is on its own disk, because a browser's file dialog shows the machine
the browser is on. How far it may look is `RB_LOCAL_ROOTS`, which the image
sets to `/media` -- the folder compose mounts from the host. So the boundary
is what you chose to hand the container:

```ini
RB_MEDIA=D:\Media
```

Nothing outside it can be listed, uploaded from or downloaded to, including
through a link that points out. More than one folder: mount each somewhere
and list them, separated by semicolons, in `RB_LOCAL_ROOTS`.

## What each mode costs

The image is the same size whichever mode is chosen, about 635 MB: an image
that can show the window has to carry something to show it with. What
differs is what is running.

- **`web`** runs one process, the application. No X server, no desktop, no
  streaming, no audio daemon -- they are taken out of the service list before
  the supervisor reads it.
- **`both`** and **`desktop`** run the whole streaming stack as well, and
  want `--shm-size=1gb`.

## Stopping

`docker stop` is honoured in every mode. The application stops its
transfers, records in the history what became of them, and leaves the queue
exactly as it was, so the next start carries on. It takes up to eight
seconds, and Docker's default is to kill after ten -- `compose.yaml` sets
`stop_grace_period: 20s`, and with plain `docker run` it is `--stop-timeout
20`.

## Coming from the noVNC image

Earlier images streamed the window with noVNC on port 5800. The volume is
picked up as it is -- tasks, schedules, history and `rclone.conf` are where
they were. What changes is around it:

| before | now |
|---|---|
| port `5800`, plain HTTP | `3001` HTTPS for the window, `19999` for the web interface |
| `VNC_PASSWORD` | `CUSTOM_USER` + `PASSWORD` for the window (`RB_STREAM_USER` / `RB_STREAM_PASSWORD` in `.env`) |
| `USER_ID` / `GROUP_ID` | `PUID` / `PGID` |
| `KEEP_APP_RUNNING` | not needed |
| `INSTALL_PACKAGES` | not available; build with the package added |

## Notes on running it

- Both ports are bound to loopback in `compose.yaml`. Put a reverse proxy
  with TLS in front before exposing either.
- **The streamed window has no password unless one is set.** With the
  loopback binding that means anyone on the machine. Set `RB_STREAM_USER`
  and `RB_STREAM_PASSWORD`. `docker/.env` is git-ignored and kept out of the
  build context.
- The web interface listens on every address *inside* the container, which
  is what makes the published port work. It is the port mapping that decides
  who can reach it.
- `PUID`/`PGID` default to 1000.
- `rclone mount` needs `/dev/fuse` and `SYS_ADMIN`. Those are commented out
  because they widen what a compromised container can reach, and transfers
  do not need them.
- On Windows a default port may fail to bind with a message about socket
  access permissions. Windows reserves scattered ranges for Hyper-V. Set
  `RB_WEB_PORT` or `RB_PORT` in `docker/.env` to something outside them;
  `netsh interface ipv4 show excludedportrange protocol=tcp` lists the
  reserved ranges.
- The image carries DejaVu and Noto, including Thai. CJK is left out at
  roughly 100 MB.
- X11 is the default for the streamed window. `PIXELFLUX_WAYLAND=true`
  switches to Wayland, and the window then runs on Qt's own Wayland plugin.
  The tray icon has not been tried there.

## Using your own rclone build

Mount it at `/opt/rclone/rclone` and it is used instead:

```bash
docker run -v /usr/local/bin/rclone:/opt/rclone/rclone:ro ...
```

It has to be a Linux binary for the container's architecture. **A Windows
`rclone.exe` cannot run in a Linux container**, so on Windows this means
supplying a Linux build rather than the one already installed.

The container runs it once at startup to check. If it will not run, the log
says so and the bundled rclone is used, instead of leaving the application
reporting that it cannot determine the rclone version:

```
WARNING: /opt/rclone/rclone will not run here and was ignored
WARNING: it has to be a Linux x86_64 binary
WARNING: continuing with the bundled rclone v1.73.1
```

## Working on the image

`docker/compose.dev.yaml` is for trying a change. It always builds from the
working tree, and it has its own name, ports and volumes, so it can be up
beside the real container -- or beside the program running outside Docker --
without either noticing the other.

```bash
docker compose -f docker/compose.dev.yaml up -d --build
# web interface     http://localhost:19991
# streamed window   https://localhost:3011
```

It mounts two things from outside:

- **This machine's `rclone.conf`**, so the remotes already set up are there.
  It is the real file, read-write, because rclone has to save refreshed
  tokens back to it. Set `RB_DEV_RCLONE_CONF` to a directory holding a copy
  to keep the two apart.
- **An rclone of your choosing**, two ways: `RB_DEV_RCLONE_REPO` and
  `RB_DEV_RCLONE_VERSION` fetch a published one at start, or
  `RB_DEV_RCLONE_DIR` names a directory with a file called `rclone` in it. The container uses that one and says so
  in its log. It has to be a Linux build; see the next section.

Its settings in `docker/.env` are `RB_DEV_*`, listed at the bottom of
`.env.example`.

## Building

The Dockerfile compiles from the build context, so the binary always matches
the checkout it was built from:

```bash
docker build -f docker/Dockerfile -t rclonebrowser:dev .
```

Build arguments: `ALPINE_VERSION`, `SELKIES_TAG`, `RCLONE_REPO`,
`RCLONE_VERSION`, `TARGETARCH`. The builder stage has to stay on the same Alpine release as the
base image, or the Qt libraries it links against will not be the ones present
at run time.

## History

This lived in `totza2010/rclonebrowser-docker`. That version cloned the
sources from GitHub at build time without pinning a commit, so an image could
not be traced back to what it was built from, and it was based on Alpine 3.16,
which stopped receiving updates in 2024. It also published to Docker Hub;
this one publishes only to ghcr.io.
