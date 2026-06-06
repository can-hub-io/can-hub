# can-hub

CAN-over-network hub. An alternative to socketcand and cannelloni: devices behind NAT export their CAN interfaces to a central hub over QUIC (mTLS), and clients consume those interfaces through the hub or, when the network allows it, directly over P2P.

Status: design / initial scaffold. See [doc/design.md](doc/design.md) and [doc/protocol.md](doc/protocol.md).

## Components

- `can-hub` — the hub: rendezvous, daemon/interface registry and frame relay.
- `can-hub-agent` — device daemon: exports local SocketCAN interfaces to the hub.
- `can-hub-cli` — hub administration and queries (local unix socket).

## Build

```sh
make release            # -O2, ARCH=x86_64 by default
make debug              # -O0 -g
make release ARCH=arm64 # cross: aarch64-linux-gnu
make release ARCH=armhf # cross: arm-linux-gnueabihf
make test               # unit tests (CEST, host x86_64)
make install            # install release binaries
```

Requirements: cmake >= 3.16, ninja, gcc (`GENERATOR=...` overrides the cmake generator). Cross builds: `aarch64-linux-gnu` / `arm-linux-gnueabihf` toolchains.

## License

MIT. See [LICENSE](LICENSE).
