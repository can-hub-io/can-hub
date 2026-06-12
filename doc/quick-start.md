# Quick start

First remote frame in ten minutes. Two paths: a sandbox on one machine with
no CAN hardware, then the real hub + device + consumer deployment.

## Sandbox (one machine, no CAN hardware)

Install or build the binaries ([installation](installation.md)), create a
virtual CAN bus, and run everything on loopback:

```sh
sudo ip link add dev vcan0 type vcan && sudo ip link set up vcan0

can-hub &
can-hub-agent --connect tcp://127.0.0.1:7228 --name demo vcan0 &
```

`list` shows the catalogue. Interfaces are addressed by numeric id or by
their namespaced name `agent/iface`:

```sh
$ can-hub-client --connect tcp://127.0.0.1:7228 list
id         agent                            interface
1          demo                             vcan0
```

Traffic on the bus reaches the client, and `send` reaches the bus:

```sh
$ can-hub-client --connect tcp://127.0.0.1:7228 dump demo/vcan0 &
$ cansend vcan0 123#DEADBEEF
(1780847295.078524) 123 [4] DE AD BE EF

$ candump vcan0 &
$ can-hub-client --connect tcp://127.0.0.1:7228 send demo/vcan0 123#CAFE
```

Loopback plaintext TCP carries no identities or pinning — that is the
sandbox shortcut, not the deployment posture. When the hub runs as the
packaged service, local clients use its unix socket instead and `--connect`
can be omitted entirely.

## Real deployment

On the hub host (defaults: quic://7227 UDP, tls://7227 TCP, plain tcp://7228
on loopback, and a local unix socket):

```sh
can-hub
```

On the device. The agent generates its TLS identity on first start and pins
the hub's fingerprint on first contact (TOFU); the hub pins the agent's in
return:

```sh
can-hub-agent --connect quic://hub.example.com:7227 --name truck42 can0 can1
```

From anywhere:

```sh
can-hub-client --connect tls://hub.example.com:7227 list
can-hub-client --connect tls://hub.example.com:7227 dump truck42/can0
can-hub-client --connect tls://hub.example.com:7227 send truck42/can0 123#DEADBEEF
```

Administration, on the hub host:

```sh
can-hub-cli status
can-hub-cli agents
```

## Where next

- [Installation](installation.md) — packages, systemd services, static binaries.
- [Security](security.md) — lock down agents (allowlist) and clients (ACLs).
- [Client](client.md) — socketcand bridge, vcan mirroring (`attach`), CAN id filters.
- [Web admin](web.md) — browser panel over the same admin plane.
