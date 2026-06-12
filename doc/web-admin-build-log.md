# Web admin panel — build log

Working checklist for epic #14 (`can-hub-web`). Living doc on branch `doc/web-admin-design`; not a replacement for the GitHub issues, just the execution tracker so the build runs without per-step interruptions. Remove or fold into docs before merge.

Layout: `web/daemon` (Rust, axum) + `web/ui` (React + Vite).

Legend: `[ ]` todo · `[~]` in progress · `[x]` done · `[!]` blocked/needs decision.

## Phase 1 — Foundations (#83)
- [x] `web/daemon` Cargo crate scaffold (`can-hub-web`), bin + lib, runnable; deps deferred per phase (codecs are dependency-free)
- [x] Admin protocol codecs in Rust: header + HELLO + 0x10–0x2B encode/decode, 23 unit tests green. pin/acl layouts taken from authoritative `src/protocol/admin_message.c` (absent from protocol.md)
- [x] Unix-socket admin client: HELLO role admin, message framing, paginated-view aggregation (status/peers/agents/clients/interfaces/acl). Mock-tested. Reconnect/pooling deferred to runtime wiring (per-request connect for now)
- [x] REST API surface: `/api/{status,peers,agents,clients,interfaces,acls}` + `/healthz`, camelCase DTOs, 502 on hub-unreachable. axum 0.8
- [x] axum server skeleton: binds 127.0.0.1:8080, args `--connect/--listen/--assets`, graceful SIGINT/SIGTERM. Serves built SPA via tower-http ServeDir + index fallback (rust-embed packaging deferred to Phase 6)
- [x] `web/ui` React + Vite scaffold (TS), `/api` dev proxy, status dashboard polling `/api/status`, `npm run build` green

## Phase 2 — Read views + telemetry (#84)
- [x] REST read endpoints: status, peers, agents, clients, interfaces, acls (done in Phase 1)
- [x] UI views/tables for each, auto-refresh (2s polling); tabbed nav
- [x] Telemetry: daemon poll loop over admin counters → deltas/rates (+ per-interface). Pure rate math unit-tested; full poll→broadcast pipe verified by integration test over a real unix socket
- [x] WebSocket fan-out to subscribed browsers, one poller for N clients (broadcast). 101 handshake verified. views.read gating deferred to Phase 4 auth
- [x] Live dashboard: status cards + live rate cards + per-interface throughput table via WS

## Phase 3 — Admin actions (#85)
- [x] REST mutating endpoints + UI: kick peer / kick agent (POST /api/peers/{id}/kick, /api/agents/{name}/kick)
- [x] interface set bitrate / up / down (POST /api/interfaces/config); UI config form
- [x] pins add / delete (POST /api/pins, DELETE /api/pins/{name}) + pins list (added ADMIN_PINS codec) + UI tab
- [x] acl add / delete (POST /api/acls, /api/acls/revoke); UI grant editor + revoke
- [x] admin client action methods unit-tested; endpoints map hub status→200/409, bad input→400, hub-down→502 (smoke verified)

## Phase 4 — Auth (#86)
- [x] `web.db` schema + store: users (argon2id), groups, group_permissions, user_groups, sessions. 11 unit tests (in-memory SQLite): create/verify, disabled login, dup conflict, permission union, session lifecycle + expiry, cascade delete
- [x] Permission classes (views.read/peers.kick/interfaces.config/pins.manage/acl.manage/users.manage); effective = union across groups
- [x] server-side sessions: 256-bit token, idle (12h) + absolute (7d) expiry, last_seen refresh
- [ ] HTTP login/logout + HttpOnly session cookie
- [ ] permission middleware on every REST route + WS subscription
- [ ] bootstrap: setup page on zero users + `--add-user` headless
- [ ] user/group management endpoints + UI (users.manage)

## Phase 5 — Hardening + audit (#87)
- [ ] CSRF token on mutating requests
- [ ] login rate limiting
- [ ] secure cookie flags
- [ ] audit log of mutating operations (actor/target/timestamp)
- [ ] reverse-proxy / TLS deployment docs

## Phase 6 — Packaging + docs (#88)
- [ ] deb package (React build + Rust binary)
- [ ] systemd unit (After=can-hub.service, socket group)
- [ ] user docs (install, bootstrap, TLS)
- [x] update doc/design.md §Web admin with decided design

## Notes / decisions log
- 2026-06-12: backend Rust, frontend React+Vite, separate process, REST + WS telemetry daemon-side, users/groups in web.db. See doc/design.md §Web admin.
- 2026-06-12: toolchain — distro rustc was 1.75; axum 0.8 needs >=1.80. Installed rustup stable (1.96). Build the daemon with `. ~/.cargo/env` so cargo resolves to ~/.cargo/bin.
- 2026-06-12: **protocol.md gap** — pin/acl admin layouts (0x22, 0x24..0x29) are listed in the type table but have no payload-layout block. Rust codecs derived from authoritative `src/protocol/admin_message.c` (offsets body-relative). Worth backfilling protocol.md.
- Phase 1 verified: 28 cargo tests green; daemon serves /healthz, /api/* (502 without a hub), and the built SPA with client-route fallback. Live-hub integration not exercised here (no running hub).
- Phase 2 verified: 32 unit + 1 integration test (telemetry pipe over a real unix socket) green; WS upgrade returns 101; UI builds (tsc + vite). Telemetry interval 1s, broadcast capacity 16 (latest-wins for slow subscribers). Per-request admin connect still (pooling deferred).
