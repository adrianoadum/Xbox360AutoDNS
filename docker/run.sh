#!/usr/bin/env bash
# Runs build.sh (or any command) in a container that has the XDK tools under
# Wine, for hosts without Windows. The XDK comes from the autodns-xdk volume
# that docker/setup-xdk.sh fills; set XDK to use another volume or an absolute
# path to an XDK folder instead.
#
#   docker/run.sh           # ./build.sh
#   docker/run.sh bash      # a shell in the container
set -e
cd "$(dirname "$0")/.."

docker build -q -t autodns-build --build-arg UID="$(id -u)" --build-arg GID="$(id -g)" docker >/dev/null
docker run --rm -v "$PWD:/src" -v "${XDK:-autodns-xdk}:/xdk:ro" \
    -e XEDK=/xdk -e CRT_INC=/xdk/TechPreview/Jul12Compiler/include/xbox \
    autodns-build "${@:-./build.sh}"
