#!/bin/sh
# Build a self-contained can-hub-web release: compile the React UI, then build
# the Rust binary with the SPA embedded (feature embed-ui). With DESTDIR set,
# also stage the binary, systemd unit and config into a package layout.
#
# Usage:
#   web/build-release.sh                 # build only
#   DESTDIR=/tmp/stage web/build-release.sh   # build + stage for packaging
set -e

here=$(CDPATH= cd "$(dirname "$0")" && pwd)
repo=$(CDPATH= cd "$here/.." && pwd)

echo "==> building UI"
cd "$here/ui"
npm ci
npm run build

echo "==> building daemon (embed-ui)"
cd "$here/daemon"
cargo build --release --features embed-ui

binary="$here/daemon/target/release/can-hub-web"
echo "built: $binary"

if [ -n "$DESTDIR" ]; then
    echo "==> staging into $DESTDIR"
    install -Dm755 "$binary" "$DESTDIR/usr/bin/can-hub-web"
    install -Dm644 "$repo/packaging/systemd/can-hub-web.service" \
        "$DESTDIR/lib/systemd/system/can-hub-web.service"
    install -Dm644 "$repo/packaging/web.conf" "$DESTDIR/etc/can-hub/web.conf"
    install -Dm644 "$repo/packaging/completions/bash/can-hub-web" \
        "$DESTDIR/usr/share/bash-completion/completions/can-hub-web"
    install -Dm644 "$repo/packaging/completions/zsh/_can-hub-web" \
        "$DESTDIR/usr/share/zsh/vendor-completions/_can-hub-web"
    echo "staged binary, systemd unit, /etc/can-hub/web.conf and shell completions"
fi
