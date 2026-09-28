#!/usr/bin/env bash
# Fills the autodns-xdk volume for docker/run.sh from an XDK installer
# (XDKSetupXenon21256.*.exe). The installer's payload is a run of cabinets
# appended to the exe, so cabextract unpacks it without running setup. Only
# what build.sh uses is kept: the compiler tools, the Xbox headers, the Tech
# Preview compiler's C headers, and the four libraries the link pulls in (all
# of lib/xbox is 2 GB). A new library in build.sh needs a -F line here too.
#
#   docker/setup-xdk.sh ~/Downloads          # folder that holds the installer
#   docker/setup-xdk.sh autodns-xdk-setup    # or a Docker volume that does
set -e
cd "$(dirname "$0")/.."

SRC="${1:?usage: $0 <folder or volume holding XDKSetupXenon21256.*.exe>}"
[[ "$SRC" == */* ]] && SRC="$(realpath "$SRC")"   # volume names can't hold a slash

docker build -q -t autodns-build --build-arg UID="$(id -u)" --build-arg GID="$(id -g)" docker >/dev/null
docker volume create autodns-xdk >/dev/null
docker run --rm --user root -v "$SRC:/setup:ro" -v autodns-xdk:/xdk autodns-build sh -ec '
    set -- /setup/XDKSetupXenon21256.*.exe
    [ -f "$1" ] || { echo "no XDKSetupXenon21256.*.exe in the folder or volume" >&2; exit 1; }
    echo "extracting $1"
    rm -rf /tmp/x /xdk/*
    cabextract -q -d /tmp/x \
        -F "XDK/bin/win32/*" -F "XDK/include/xbox/*" \
        -F "XDK/TechPreview/Jul12Compiler/include/xbox/*" \
        -F "XDK/lib/xbox/xboxkrnl.lib" -F "XDK/lib/xbox/xapilib.lib" \
        -F "XDK/lib/xbox/libcmt.lib" -F "XDK/lib/xbox/oldnames.lib" "$1"
    mv /tmp/x/XDK/* /xdk/
    du -sh /xdk/*/*
'
