# can-hub-web

Web admin panel for the hub (epic #14). A separate daemon — `web/daemon`
(Rust, axum) — that consumes the hub admin plane over the unix socket and
serves a REST API plus a React UI (`web/ui`). The hub binary is untouched.

The REST API contract is specified in [`openapi.yaml`](openapi.yaml) (OpenAPI
3.1) — the source of truth for clients and the web <-> can-hub end-to-end tests.

## Build

Backend (needs a recent Rust toolchain — axum 0.8 requires rustc >= 1.80):

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

`web/build-release.sh` builds the UI and then the binary with the SPA embedded
(cargo feature `embed-ui`), producing a single self-contained `can-hub-web`
that needs no `--assets`:

```sh
web/build-release.sh                      # build only
DESTDIR=/tmp/stage web/build-release.sh   # build + stage package layout
```

With `DESTDIR` it installs the binary to `usr/bin`, the systemd unit to
`lib/systemd/system/can-hub-web.service` and the config to
`etc/can-hub/web.conf` under `$DESTDIR` — the layout the deb packages.

## Run

```sh
can-hub-web \
  --connect /run/can-hub/hub.sock \   # hub admin socket (default)
  --listen 127.0.0.1:8080 \           # HTTP bind (default loopback)
  --db /var/lib/can-hub/web.db \      # users/groups/sessions/audit
  --assets /usr/share/can-hub-web \   # built SPA (web/ui/dist); omit in dev
  [--secure-cookies]                  # mark the session cookie Secure (TLS)
```

In development run the daemon without `--assets` and use the Vite dev server
(`npm run dev` in `web/ui`), which proxies `/api` and the telemetry WebSocket
to `127.0.0.1:8080`. (A dev `cargo build` without `embed-ui` serves no static
assets; use the Vite dev server or pass `--assets web/ui/dist`.)

## systemd / packaging

`packaging/systemd/can-hub-web.service` runs the daemon as the `can-hub`
user/group (so it can reach `/run/can-hub/hub.sock`), `After=can-hub.service`,
with the SPA embedded so no asset path is needed. `packaging/web.conf` (→
`/etc/can-hub/web.conf`) holds `WEB_ARGS`. Debian maintainer scripts live in
`packaging/debian/web/` (enable the unit, reuse the `can-hub` system user,
remove `web.db` on purge). A deb stages the files produced by
`DESTDIR=… web/build-release.sh`.

## First-run bootstrap

With zero users the panel serves only a setup page that creates the first
admin (placed in an `admins` group holding every permission). Headless
alternative:

```sh
CANHUB_WEB_PASSWORD=secret can-hub-web --add-user alice --db /var/lib/can-hub/web.db
```

## Authentication and authorization

- Local users and groups live in `web.db` (passwords argon2id). The hub's
  `hub.db` and agent/client identities are untouched.
- Group permissions are operation classes: `views.read`, `peers.kick`,
  `interfaces.config`, `pins.manage`, `acl.manage`, `users.manage`. A user's
  effective permission is the union across its groups.
- Sessions are server-side, carried in an HttpOnly, SameSite=Strict cookie,
  with a 12 h idle and 7 day absolute expiry.
- Mutating requests require an `X-CSRF-Token` header matching the session
  token (the UI sends it automatically; it is returned by `/api/auth/state`,
  `/api/login` and `/api/setup`).
- Failed logins are rate limited per client IP (5 / minute).
- Every mutating operation is recorded in the audit log (`/api/audit`, or the
  Audit tab) with actor, action, target and resulting status.

## Deployment behind TLS

The daemon serves plain HTTP and binds loopback by default. Terminate TLS at a
reverse proxy (nginx, Caddy) on the same host and proxy to it, forwarding the
WebSocket upgrade. Pass `--secure-cookies` so the session cookie is only sent
over HTTPS.

Example (nginx):

```nginx
location / {
    proxy_pass http://127.0.0.1:8080;
    proxy_set_header Host $host;
    proxy_set_header Upgrade $http_upgrade;
    proxy_set_header Connection "upgrade";
}
```

The hub accepts the admin role only on local transports, so `can-hub-web` must
run on the hub host; do not expose its admin socket connection remotely.
