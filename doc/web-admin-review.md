# Web admin panel — pre-PR review tasks

Findings from the exhaustive review of epic #14 (`web/daemon` + `web/ui`) on
branch `doc/web-admin-design`. Working checklist, same posture as
`doc/web-admin-build-log.md`: resolve top-to-bottom before opening the PR,
then fold or remove this file.

Overall verdict: architecture is sound (protocol codecs → admin_client → api →
auth store, clean layering), dependencies current (axum 0.8.9, React 19,
Vite 8, TS 6, tokio 1.52), 52 unit + 1 integration tests on the right pure
parts. Nothing below invalidates the design; the P0 items are real security
bugs that must not ship.

Legend: `[ ]` todo · `[~]` in progress · `[x]` done.

## Milestone 1 — P0 security (one commit, each item with a regression test)

- [x] **Disabled users keep valid sessions.** `validate_session`
  (`web/daemon/src/auth/store.rs:318`) never checks `users.enabled`, and
  `set_user_enabled(false)` does not delete sessions — disabling a user does
  nothing until session expiry (up to 7 days). Fix: JOIN `users` in
  `validate_session` and require `enabled != 0` (deletion is already covered
  by `ON DELETE CASCADE`). Test: create session, disable user, expect
  `validate_session` → `None`.
- [x] **Login rate limiter breaks behind the documented reverse proxy.**
  Keyed on `ConnectInfo<SocketAddr>` (`web/daemon/src/api.rs:601`), which is
  always `127.0.0.1` behind the nginx setup `web/README.md` recommends: any
  anonymous visitor locks out *all* users with 5 bad logins/min, and a real
  attacker is never individually limited. Fix: opt-in `--trusted-proxy` flag
  that keys on `X-Forwarded-For` (only when the flag is set; never trust the
  header otherwise), plus the user name as a second limiter key. Test:
  limiter unit tests for both keys; api test that XFF is ignored without the
  flag.
- [x] **`required_permission` is fail-open.** Any future `/api/...` route not
  matched by the prefix chain (`web/daemon/src/api.rs:749`) falls through to
  `ViewsRead`, the weakest permission. Fix: declare the permission next to
  each route registration (route table or per-route layer) so an unmapped
  path is denied, not granted. Test: unknown `/api/x` path with a
  views.read-only session → 403/404, not 200.
- [x] **Empty password accepted.** `create_user`
  (`web/daemon/src/auth/store.rs:140`) guards only the name; `curl
  /api/setup` with `"password":""` creates the bootstrap admin with an empty
  password (the UI validates, the backend does not). Fix: guard in
  `create_user` with a minimum length (e.g. 8). Test: empty/short password →
  `StoreError::Conflict`.
- [x] **`bitrate: 0` reaches the agent.** `InterfaceConfigBody` defaults
  `bitrate` to 0 (`web/daemon/src/api.rs:177`); `op=bitrate` without the
  field sends bitrate 0 to a real CAN interface. Fix: reject `bitrate == 0`
  (and an insane upper bound, e.g. > 1 Mbit/s classic CAN) when
  `op == "bitrate"`. Test: missing/zero bitrate → 400.
- [x] **No timeouts on the hub unix socket.** `with_admin`
  (`web/daemon/src/api.rs:299`) and `sample_hub`
  (`web/daemon/src/telemetry.rs:172`) use sync `UnixStream` with no
  read/write timeout: a wedged hub strands blocking tasks forever (request
  pile-up) and freezes the telemetry loop. Fix: `set_read_timeout` /
  `set_write_timeout` (~2 s) right after connect, in both call sites. Test:
  mock socket that accepts and never replies → `ApiError::Hub` within the
  timeout.

## Milestone 2 — api.rs split + router tests (one commit)

- [x] **Split `web/daemon/src/api.rs` (935 lines).** It mixes router, 15+
  DTOs, auth middleware, error mapping and 30+ handlers. Target:
  `api/{mod,handlers,dtos,middleware,error}.rs`, public surface unchanged
  (`api::router`, `api::AppState`, `api::ApiError`).
- [x] **Add router-level tests (the security-critical layer has zero).**
  Via `tower::ServiceExt::oneshot` with `AuthStore::open_in_memory()`:
  401 without session; 403 without permission; path→permission mapping per
  route family (views/kick/pins/acls/users); mutating request without
  `X-CSRF-Token` → 403, with token → pass; public paths reachable; setup
  conflict after first user.

## Milestone 3 — P1 hardening (one commit)

- [ ] **Audit logins.** `/api/login`, `/api/setup`, `/api/logout` are public
  paths and skip the audit middleware (`web/daemon/src/api.rs:689`); failed
  and successful logins are exactly what an audit log is for. Fix: record
  from the handlers (actor = attempted user name, status 200/401/429).
- [ ] **Security headers.** None today. Add a layer (tower-http
  `SetResponseHeaderLayer`, already a dependency) for `Content-Security-Policy`
  (self-only is enough for the SPA), `X-Frame-Options: DENY`,
  `X-Content-Type-Options: nosniff`, `Referrer-Policy: no-referrer`.
- [ ] **Anti-lockout guards.** You can delete your own user, delete the
  `admins` group, or untick `users.manage` from the last group holding it;
  recovery needs shell access. Fix in the store: refuse to delete/disable the
  requesting user and refuse any change that leaves zero enabled users with
  `users.manage` (requires passing the actor id into
  delete_user/set_user_enabled/set_group_permissions/delete_group paths).
- [ ] **Telemetry loop dies on panic.** `sample_hub` `.expect()`s the
  JoinHandle (`web/daemon/src/telemetry.rs:180`); one panic in the blocking
  task kills telemetry until daemon restart. Fix: log and `continue`.
- [ ] **Transactions.** `setup` (`api.rs:649`: create_user + ensure_group +
  set_group_permissions + add_user_to_group), `run_add_user`
  (`main.rs:159`, same sequence) and `set_group_permissions`
  (`store.rs:232`: DELETE + INSERT loop) can be left half-applied on error.
  Wrap each in a rusqlite transaction.
- [ ] **Hash session tokens at rest.** Sessions are stored as the raw cookie
  token (`store.rs:304`); web.db theft = session hijack. Store
  SHA-256(token), look up by hash. While there: make the CSRF comparison
  (`api.rs:716`) constant-time.

## Milestone 4 — password change (one commit)

- [ ] **No password change exists** (no `update_password` in the store); the
  only recourse is delete+recreate, which loses id and memberships. Add:
  store `update_password(user_id, password)` invalidating the user's other
  sessions; `POST /api/users/{id}/password` (users.manage);
  `POST /api/auth/password` (self-service, requires the current password);
  UI: field in the Users tab + a "change my password" control in the topbar.

## Milestone 5 — UI/UX (one commit)

- [ ] **Hide actions the user cannot perform.** Kick buttons render with only
  `views.read`, the ifconfig form without `interfaces.config`; clicking
  yields a 403 alert. `AuthState.permissions` is already in the client: gate
  the action column / config form (App.tsx `Peers`/`Agents`/`Interfaces`).
- [ ] **Session expiry leaves the app broken.** 401s render as tab errors and
  the telemetry WS reconnects every 2 s forever. Fix: on 401 from any fetch,
  reload auth state so the login screen comes back (e.g. an `onUnauthorized`
  hook in `api.ts`).
- [ ] **Replace `alert()` and confirm destructive actions.** `runAction`
  (App.tsx:154) alerts on error; kick / delete user / delete group / delete
  pin / revoke ACL fire with no confirmation. Inline error area per section +
  a confirm step. Dependency-free.
- [ ] **One-click TOFU enrollment.** Agents view already shows the
  fingerprint; join `/api/pins` to badge each agent pinned/unpinned and add a
  "Pin" button on unpinned agents (pins.manage) instead of copy-pasting into
  the Pins tab.
- [ ] **Peers: transport column.** peer_id ranges encode the transport
  (tcp 0x1, unix 0x40000001, quic 0x80000001, tls 0xC0000001 — hub_main.c);
  decode UI-side.
- [ ] **Clients ↔ ACL correlation.** Client entries carry no fingerprint;
  join with `/api/peers` by `peerId` UI-side so a channel can be matched to
  its ACL subject.
- [ ] **Split `App.tsx` (578 lines)** into one component per tab under
  `web/ui/src/components/`.
- [ ] Minors: bootstrap form gets a password-confirmation field; login inputs
  get `autocomplete="username"`/`"current-password"`; default tab must be a
  permitted one (App.tsx:90 — a user without views.read currently lands on a
  broken Dashboard); clear the module-level CSRF token on logout (api.ts);
  active tab in the URL hash so refresh keeps it.

## Milestone 6 — cleanup (one commit)

- [ ] **Stale comments** (this codebase bans them): `main.rs:3` "Phase 1
  skeleton"; `api.rs:270` "permission gating … lands with auth in a later
  phase" (it landed); `api.rs:53` "Production packaging will embed" (it
  does); `protocol/admin.rs:4` "codecs are deferred" (they are implemented
  below).
- [ ] **Guard-clause style** per CODE_STYLE (happy path first, flat):
  `auth_state` (`api.rs:574`) nests two `if let`s — early-return the
  unauthenticated case; `require_permission` (`api.rs:688`) flattens its
  nested matches.
- [ ] **`list_groups` N+1** (`store.rs:249`): re-prepares the permission
  statement inside the loop; replace with one JOIN query.
- [ ] **Store mutex held across argon2 (~100 ms).** `verify_login` hashes
  under the global store lock, serializing every concurrent request's
  session validation. Fetch the hash under the lock, verify outside.
- [ ] **Scaffold leftovers tracked in git**: `web/ui/src/assets/react.svg`,
  `vite.svg`, unused `hero.png` — delete.
- [ ] **Cache headers in `embedded.rs`**: hashed `/assets/*` →
  `Cache-Control: public, max-age=31536000, immutable`; `index.html` →
  `no-cache`.
- [ ] **Unknown `/api/*` returns the SPA.** Unmatched API paths fall through
  to the index.html fallback as 200 HTML; exclude `/api/` from the fallback
  and return 404 JSON.
- [ ] Minors: `LoginLimiter` map never prunes IPs it does not see again
  (unbounded growth — global sweep on insert); expired sessions and audit
  rows accumulate with no cleanup (periodic DELETE); usage text omits
  `--version`; `run_add_user` echoes the password typed on stdin (termios
  echo off or document `CANHUB_WEB_PASSWORD` as the intended path).

## Milestone 7 — docs (one commit)

- [ ] **Backfill `doc/protocol.md`** with the pin/ACL payload layouts
  (0x22, 0x24–0x29) currently only in `src/protocol/admin_message.c` (gap
  already flagged in the build log).
- [ ] **`doc/design.md:113`** still says "(design decided 2026-06-12,
  unbuilt)" — update §Web admin to the as-built state.
- [ ] **Fold or remove `doc/web-admin-build-log.md`** (its own header says
  "remove before merge") — and this file once done.

## Out of scope for the PR — candidate GitHub issues

- **Hub: expose interface link state + current bitrate.** The single gap that
  genuinely degrades the panel: `ADMIN_INTERFACES_REPLY` carries no up/down
  or bitrate, so the config form flies blind (you can set state but never see
  it). Entry has reserved bytes @5..8; needs C hub + protocol.md + web.
- **Hub: per-peer connected-since timestamp; hub uptime/version in
  `ADMIN_STATUS`; per-peer/per-channel u32 counters wrap** (status is u64).
- **Web: persistent/pooled admin connection** (per-request connect today;
  already noted in the build log).
- **Web: UI test harness** (vitest + testing-library; zero UI tests today —
  acceptable for v1).
- **Web: argon2 CPU exhaustion bound** — logins burn ~100 ms CPU before the
  failure is counted; a global concurrent-login semaphore caps it.

## Verification (after each milestone)

- `cd web/daemon && . ~/.cargo/env && cargo test` — existing 52 + 1 tests
  plus the new regression tests green.
- `cd web/ui && npm run lint && npm run build` — eslint + tsc clean.
- Manual smoke against a real hub (`make release`; run hub + daemon):
  bootstrap → login → disable user in a second browser: next request 401 →
  viewer account sees no Kick/config controls → bad CSRF 403 → 6th bad login
  429 (per user behind `--trusted-proxy`) → `op=bitrate` without bitrate →
  400 → audit shows the logins.
- `web/build-release.sh` still produces the self-contained binary.
