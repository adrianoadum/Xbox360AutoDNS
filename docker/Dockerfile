# Build environment for AutoDNS.xex: the XDK's Windows tools under Wine.
# The XDK isn't in the image. docker/run.sh mounts it at /xdk.
FROM alpine:3.23

RUN apk add --no-cache bash wine cabextract

# Wine refuses a prefix owned by someone else, so the prefix belongs to a user
# with the host's ids and build/ comes out owned by the host user.
ARG UID=1000
ARG GID=1000
RUN (getent group "$GID" >/dev/null || addgroup -g "$GID" build) \
 && adduser -D -u "$UID" -G "$(getent group "$GID" | cut -d: -f1)" build
USER build

ENV WINEDEBUG=-all WINEPREFIX=/home/build/.wine XDG_RUNTIME_DIR=/home/build/.run
RUN mkdir -m 700 "$XDG_RUNTIME_DIR" && wineboot -i && wineserver -w

WORKDIR /src
