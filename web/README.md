# can-hub-web

Web admin panel for the hub: `web/daemon` (Rust, axum) consumes the hub
admin plane over the unix socket and serves a REST API plus the React UI in
`web/ui`. The hub binary is untouched.

**Deployment, authentication and operation are documented in
[doc/web.md](../doc/web.md).** This file covers development.

The REST API contract is [`openapi.yaml`](openapi.yaml) (OpenAPI 3.1) — the
source of truth for clients and the web <-> can-hub end-to-end tests.

## Build

Backend (axum 0.8 requires rustc >= 1.80):

```sh
cd web/daemon
cargo build --release
```

Frontend (Node 20+):

```sh
cd web/ui
npm install
npm run build      # emits web/ui/dist
```

### Self-contained release

`web/build-release.sh` builds the UI and then the binary with the SPA
embedded (cargo feature `embed-ui`), producing a single `can-hub-web` that
needs no `--assets`:

```sh
web/build-release.sh                      # build only
DESTDIR=/tmp/stage web/build-release.sh   # build + stage package layout
```

With `DESTDIR` it installs the binary to `usr/bin`, the systemd unit to
`lib/systemd/system/can-hub-web.service` and the config to
`etc/can-hub/web.conf` under `$DESTDIR` — the layout the deb packages.
The `can-hub` deb ships `can-hub-web` alongside the hub daemon: `make deb`
runs this script and folds the binary, unit and config into that package
(its maintainer scripts in `packaging/debian/hub/` enable the web service).

## Development loop

Run the daemon without `--assets` and use the Vite dev server (`npm run dev`
in `web/ui`), which proxies `/api` and the telemetry WebSocket to
`127.0.0.1:8080`. A dev `cargo build` without `embed-ui` serves no static
assets; use the Vite dev server or pass `--assets web/ui/dist`.

The end-to-end suite (`tests/web.robot`, driven by `make e2e`) validates
every REST response against `openapi.yaml` — see
[test/e2e/README.md](../test/e2e/README.md).
