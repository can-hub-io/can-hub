"""Flashes an image into isotp_ecu.py with python-can-hub's UDS helper.

usage: uds_flash.py <url> <channel> --size N [--block-size B] [--skip-counter K] [--state-dir D]

The same flash as isotp_flash.py, written with bus.uds(): RequestDownload,
TransferData blocks, RequestTransferExit. --skip-counter sends block K+1 with
a counter one too far, which the ECU refuses with 7F 36 73.

Prints "done steps=N crc=XXXXXXXX" (exit 0) or
"refused service=0xSS code=0xCC step=S" (exit 2).
"""

import argparse
import sys
import zlib

import can

from canhub import Isotp, UdsNegativeResponse

REQUEST_ID = 0x7E0
RESPONSE_ID = 0x7E8
REQUEST_DOWNLOAD = bytes([0x34, 0x00, 0x44, 0x00, 0x00, 0x00, 0x00])
TRANSFER_DATA = 0x36
TRANSFER_EXIT = bytes([0x37])
COUNTER_MASK = 0xFF
SIZE_BYTES = 4
EXIT_REFUSED = 2


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("url")
    parser.add_argument("channel")
    parser.add_argument("--size", type=int, required=True)
    parser.add_argument("--block-size", type=int, default=4093)
    parser.add_argument("--skip-counter", type=int)
    parser.add_argument("--state-dir")
    arguments = parser.parse_args()

    image = bytes((index * 7 + 3) & COUNTER_MASK for index in range(arguments.size))
    bus = can.Bus(interface="canhub", channel=arguments.channel, url=arguments.url, state_dir=arguments.state_dir, reliable=True)
    try:
        result = flash(bus, image, arguments)
    except UdsNegativeResponse as refused:
        print(f"refused service=0x{refused.service:02X} code=0x{refused.code:02X} step={refused.result.failing_step}", flush=True)
        sys.exit(EXIT_REFUSED)
    finally:
        bus.shutdown()

    print(f"done steps={result.completed_steps} crc={zlib.crc32(image):08x}", flush=True)


def flash(bus, image, arguments):
    with bus.uds(REQUEST_ID, Isotp(RESPONSE_ID), exclusive=True) as ecu:
        ecu.request(REQUEST_DOWNLOAD + len(image).to_bytes(SIZE_BYTES, "big"))
        if arguments.skip_counter is None:
            ecu.transfer(image, block_size=arguments.block_size)
        else:
            transfer_skipping(ecu, image, arguments.block_size, arguments.skip_counter)
        ecu.request(TRANSFER_EXIT)
        return ecu.wait()


def transfer_skipping(ecu, image, block_size, skip_after):
    counter = 1
    for block_number, offset in enumerate(range(0, len(image), block_size), start=1):
        ecu.request(bytes([TRANSFER_DATA, counter]) + image[offset:offset + block_size], expect=bytes([0x76, counter]))
        counter = (counter + 1) & COUNTER_MASK
        if block_number == skip_after:
            counter = (counter + 1) & COUNTER_MASK


main()
