#!/usr/bin/env bash
# Builds AutoDNS.xex with the Xbox 360 XDK's own compiler. No Visual Studio.
# The DNS servers aren't built in: build/ gets a copy of AutoDNS.ini to go
# next to the plugin.
set -e
cd "$(dirname "$0")"

XEDK="${XEDK:-/c/XDK21256/XDK}"
[ -f "$XEDK/bin/win32/cl.exe" ] || XEDK=/c/XDK21256/XDK   # the XDK installer sets XEDK to a tools-only install
CRT="${CRT_INC:-/c/XDK21256/TP/XDK/TechPreview/Jul12Compiler/include/xbox}"   # plain C headers the XDK keeps here
BIN="$XEDK/bin/win32"

# Git Bash on Windows runs the XDK tools directly. Anywhere else (docker/run.sh)
# they run under Wine.
if command -v cygpath >/dev/null; then
    WINE=; winpath() { cygpath -w "$1"; }
else
    WINE=wine; winpath() { winepath -w "$1"; }
fi
INC="$(winpath "$XEDK/include/xbox")"

mkdir -p build

INCLUDE="$INC;$(winpath "$CRT")" $WINE "$BIN/cl.exe" -nologo -c -W4 -WX -Ox -MT -GR- -EHsc -TP \
    -D _XBOX -D NDEBUG \
    -FI"$INC\\xbox_intellisense_platform.h" -Fobuild/AutoDNS.obj AutoDNS.cpp

LIB="$(winpath "$XEDK/lib/xbox")" $WINE "$BIN/link.exe" -nologo -RELEASE -OPT:REF -DLL -ENTRY:_DllMainCRTStartup \
    -XEX:NO -ALIGN:128,4096 -OUT:build/AutoDNS.exe build/AutoDNS.obj xboxkrnl.lib xapilib.lib

$WINE "$BIN/imagexex.exe" -nologo -config:AutoDNS.xex.xml -out:build/AutoDNS.xex build/AutoDNS.exe
cp AutoDNS.ini build/
echo build/AutoDNS.xex build/AutoDNS.ini
