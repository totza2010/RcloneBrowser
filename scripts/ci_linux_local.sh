#!/usr/bin/env bash
# The Linux job of .github/workflows/ci.yml, on this machine: the same Ubuntu,
# the same distribution Qt (6.4 -- older than a developer's), the same build
# and the same tests. Nothing is installed here: it runs in a container that
# is thrown away when it ends, and the checkout is mounted read-only.
#
# Why it exists: CI's Linux job was red for two days (2026-10-17..19) while
# every build and test on Windows passed -- a Qt 6.5 call in a test, and three
# tests that wrote Windows paths. Run this before pushing anything that
# touches tests or uses a Qt call you are not sure of.
#
#   scripts/ci_linux_local.sh              # build and run every test
#   scripts/ci_linux_local.sh test_api     # and run only the tests matching
#
# Needs Docker. The results are in ./ci-linux-out (configure, build, ctest).
set -euo pipefail

here="$(cd "$(dirname "$0")/.." && pwd)"
out="$here/ci-linux-out"
mkdir -p "$out"
filter="${1:-}"

docker run --rm \
  -v "$here:/src:ro" \
  -v "$out:/out" \
  -e FILTER="$filter" \
  ubuntu:24.04 bash -c '
    set -e
    export DEBIAN_FRONTEND=noninteractive
    apt-get update -qq >/dev/null
    apt-get -y -qq install g++ cmake ninja-build qt6-base-dev qt6-multimedia-dev \
      qt6-base-dev-tools libgl1-mesa-dev libqt6sql6-sqlite >/dev/null 2>&1
    echo "Qt: $(dpkg -s qt6-base-dev | sed -n "s/^Version: //p")"
    mkdir -p /work
    tar -C /src --exclude=./build --exclude=./graphify-out --exclude=./.git \
        --exclude=./ci-linux-out --exclude=./Testing -cf - . | tar -C /work -xf -
    cd /work
    cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release > /out/configure.txt 2>&1
    if ! cmake --build build --parallel > /out/build.txt 2>&1; then
      echo "build failed:"; grep -E "error:" /out/build.txt | head -20; exit 1
    fi
    set +e
    ctest --test-dir build --output-on-failure ${FILTER:+-R "$FILTER"} > /out/ctest.txt 2>&1
    status=$?
    grep -E "tests passed|tests failed|\*\*\*Failed" /out/ctest.txt
    exit $status
  '
