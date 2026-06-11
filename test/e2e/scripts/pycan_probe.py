"""Drive the canhub python-can backend on the bench.

usage: pycan_probe.py <url> <channel> dump [options]
       pycan_probe.py <url> <channel> send <can-id-hex> <payload-hex> [options]

options: --state-dir PATH --cert PATH --key PATH --hub-fingerprint HEX
"""

import argparse

import can


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("url")
    parser.add_argument("channel")
    parser.add_argument("action", choices=["dump", "send"])
    parser.add_argument("frame_arguments", nargs="*")
    parser.add_argument("--state-dir")
    parser.add_argument("--cert")
    parser.add_argument("--key")
    parser.add_argument("--hub-fingerprint")
    arguments = parser.parse_args()

    bus = can.Bus(
        interface="canhub",
        channel=arguments.channel,
        url=arguments.url,
        state_dir=arguments.state_dir,
        identity_cert=arguments.cert,
        identity_key=arguments.key,
        hub_fingerprint=arguments.hub_fingerprint,
    )

    if arguments.action == "send":
        message = can.Message(
            arbitration_id=int(arguments.frame_arguments[0], 16),
            data=bytes.fromhex(arguments.frame_arguments[1]),
            is_extended_id=False,
        )
        bus.send(message)
        bus.shutdown()
        return

    print(f"dumping {arguments.channel}", flush=True)
    while True:
        message = bus.recv(timeout=1.0)
        if message is not None:
            print(f"{message.arbitration_id:03X}#{message.data.hex().upper()}", flush=True)


if __name__ == "__main__":
    main()
