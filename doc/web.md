# can-hub-web — web admin panel

Browser panel for the same admin surface as `can-hub-cli`: peers, agents,
clients, interfaces, live telemetry, kick, interface config, pins and ACLs —
plus its own users, groups and audit log.

It is a separate daemon, not part of the hub: an ordinary admin client of
the hub's unix socket on one side, an HTTP server (REST API + embedded React
SPA) towards browsers on the other. The hub accepts the admin role only on
local transports, so `can-hub-web` must run **on the hub host**.

## Install

Built from source today (Rust >= 1.80 and Node 20+):

```sh
web/build-release.sh                      # UI + daemon with the SPA embedded
sudo install web/daemon/target/release/can-hub-web /usr/bin/
sudo install -m644 packaging/systemd/can-hub-web.service /lib/systemd/system/
sudo install -m644 packaging/web.conf /etc/can-hub/web.conf
sudo systemctl enable --now can-hub-web
```

`DESTDIR=<stage> web/build-release.sh` stages that exact layout for
packaging. The service runs as the `can-hub` user (so it reaches
`/run/can-hub/hub.sock`), after `can-hub.service`, with the database at
`/var/lib/can-hub/web.db`.

## Run

```sh
can-hub-web \
  --connect /run/can-hub/hub.sock \   # hub admin socket (default)
  --listen 127.0.0.1:8080 \           # HTTP bind (default loopback)
  --db /var/lib/can-hub/web.db \      # users/groups/sessions/audit
  [--secure-cookies]                  # session cookie Secure (behind TLS)
```

## First run

With zero users the panel serves only a setup page that creates the first
admin (placed in an `admins` group holding every permission). Headless
alternative:

```sh
CANHUB_WEB_PASSWORD=secret can-hub-web --add-user alice --db /var/lib/can-hub/web.db
```

## Users, groups, permissions

The daemon holds full admin power over the socket; who may do what in the
browser is enforced in the web layer. Users (argon2id passwords) belong to
groups; groups hold permissions by operation class — `views.read`,
`peers.kick`, `interfaces.config`, `pins.manage`, `acl.manage`,
`users.manage` — and a user's effective permission is the union across its
groups. The last `users.manage` holder cannot be removed (anti-lockout).
All of this lives in `web.db`; the hub's `hub.db` and the agent/client
identities are untouched.

Sessions are server-side over an HttpOnly, SameSite=Strict cookie (12 h
idle, 7 day absolute expiry); mutating requests carry a CSRF token (the UI
handles it); failed logins are rate limited per client IP; every mutating
operation lands in the audit log (Audit tab or `/api/audit`).

## Telemetry

The hub only keeps cumulative counters. The daemon polls them over the admin
socket, derives rates from successive samples, and fans them out to all
subscribed browsers over one WebSocket pipeline — one poller regardless of
how many dashboards are open.

## Behind TLS

The daemon serves plain HTTP on loopback. Expose it through a TLS reverse
proxy on the same host, forwarding the WebSocket upgrade, and pass
`--secure-cookies`:

```nginx
location / {
    proxy_pass http://127.0.0.1:8080;
    proxy_set_header Host $host;
    proxy_set_header Upgrade $http_upgrade;
    proxy_set_header Connection "upgrade";
}
```

`--trusted-proxy` makes login rate limiting key on `X-Forwarded-For` when
behind such a proxy.

## REST API

The JSON API is first-class and scriptable; the contract is
[`web/openapi.yaml`](../web/openapi.yaml) (OpenAPI 3.1), validated by the
end-to-end suite. Development workflow (Vite dev server, embed feature) in
[web/README.md](../web/README.md).
