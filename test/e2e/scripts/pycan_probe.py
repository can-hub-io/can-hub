"""Drive the canhub python-can backend on the bench.

usage: pycan_probe.py <url> <channel> dump [options]
       pycan_probe.py <url> <channel> send <can-id-hex> <payload-hex> [options]
       pycan_probe.py <url> <channel> list [options]
       pycan_probe.py <url> <channel> detect [options]

options: --state-dir PATH --cert PATH --key PATH --hub-fingerprint HEX
"""

import argparse
import os

import can

from canhub import CanHubBus


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("url")
    parser.add_argument("channel")
    parser.add_argument("action", choices=["dump", "send", "list", "detect"])
    parser.add_argument("frame_arguments", nargs="*")
    parser.add_argument("--state-dir")
    parser.add_argument("--cert")
    parser.add_argument("--key")
    parser.add_argument("--hub-fingerprint")
    arguments = parser.parse_args()

    if arguments.action == "list":
        list_interfaces(arguments)
        return

    if arguments.action == "detect":
        detect_interfaces(arguments)
        return

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


def list_interfaces(arguments):
    configs = CanHubBus.list_interfaces(
        url=arguments.url,
        identity_cert=arguments.cert,
        identity_key=arguments.key,
        hub_fingerprint=arguments.hub_fingerprint,
        state_dir=arguments.state_dir,
    )
    print_configs(configs)


def detect_interfaces(arguments):
    set_environment("CANHUB_URL", arguments.url)
    set_environment("CANHUB_STATE_DIR", arguments.state_dir)
    set_environment("CANHUB_IDENTITY_CERT", arguments.cert)
    set_environment("CANHUB_IDENTITY_KEY", arguments.key)
    set_environment("CANHUB_HUB_FINGERPRINT", arguments.hub_fingerprint)
    print_configs(can.detect_available_configs("canhub"))


def set_environment(name, value):
    if value is not None:
        os.environ[name] = value


def print_configs(configs):
    for config in configs:
        print(f"{config['interface']} {config['channel']}", flush=True)


if __name__ == "__main__":
    main()
