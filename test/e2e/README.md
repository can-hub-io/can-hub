# can-hub end-to-end bench

Robot Framework suite that drives the real binaries over real `vcan` buses, in
one privileged container, with a Linux network namespace per **Server** and
`tc netem` latency (LOCAL 0 ms, LAN ~1.5 ms, WAN 50 ms behind NAT).

## Run

```sh
make e2e        # builds release binaries + image, runs the suite
```

Needs Docker and the host `vcan` module (`/lib/modules` is mounted read-only so
the container can `modprobe vcan`). Local only for now (no CI).

## Layout

- `lib/` — the bench characterisation library (pure Python):
  - `Bench` builds the topology (namespaces, veth, bridge, netem).
  - `Server` is a host abstraction (one namespace); knows nothing about can-hub.
  - `CanHub` / `CanAgent` / `CanClient` / `CanCli` wrap each binary on a Server.
  - `HubConfig` / `AgentConfig` / `ClientConfig` parametrise the launches.
  - `metrics.py` — one-way latency + delivery%% by `candump` correlation, CPU.
- `bench_variables.py` — Robot variable file: exports `${BENCH}`,
  `${LOCAL_SERVER}`, `${LAN_SERVER}`, `${WAN_SERVER}` (use `${WAN_SERVER.fqdn}`).
- `BenchKeywords.py` — Robot keyword layer over the library.
- `tests/` — the suites (`smoke.robot` to start).

## Web admin suite

`tests/web.robot` drives the `can-hub-web` daemon over REST against a real hub +
agent, validating every response body against `web/openapi.yaml` (the contract).
Extras: `CanHubWeb` (`lib/can_hub_web.py`) launches the daemon bound on the
Server's bridge IP so the Robot process reaches it; `RestClient` /
`SchemaValidator` (`lib/rest.py`) carry the session + CSRF and check schemas;
`WebKeywords.py` is the keyword layer. `make e2e` builds the daemon
(`web/daemon`, `cargo build --release`) and points the container at it via
`CAN_HUB_WEB_BIN`.

## Example

```robotframework
${hub}=      Start CAN HUB On ${LOCAL_SERVER} With ${hub_cfg}
${agent}=    Start CAN Agent On ${LOCAL_SERVER} With ${agent_cfg}
${web}=      Start CAN HUB Web On ${LOCAL_SERVER} Against ${hub}
${r}=        GET /api/status On ${web}
Status Of ${r} Should Be    200
Body Of ${r} Should Match    Status
```
