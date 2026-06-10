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

## Example

```robotframework
${hub}=      Start CAN HUB On ${LOCAL_SERVER} With ${hub_cfg}
${agent}=    Start CAN Agent On ${LOCAL_SERVER} With ${agent_cfg}
```
