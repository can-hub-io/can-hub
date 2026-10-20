"""Flashes an image into isotp_ecu.py over ISO-TP transaction steps.

usage: isotp_flash.py <url> <channel> --size N [--block-size B] [--extended TARGET SOURCE]
                      [--fd] [--skip-counter K] [--state-dir D]

RequestDownload, then the image in TransferData blocks of B bytes (block
sequence counter 1, 2, .. 255, 0, ..), then RequestTransferExit, each one step
the agent segments. The image is deterministic, so its CRC-32 printed here
matches the one the ECU prints when the transfer arrived whole. --skip-counter
jumps the counter after block K, which the ECU refuses.

Prints "done steps=N crc=XXXXXXXX" (exit 0) or "aborted reason=R step=S" (exit 2).
"""

import argparse
import sys
import zlib

import can

from canhub import Advance, Isotp, Pattern, Step, Template, TransactionAborted

REQUEST_ID = 0x7E0
RESPONSE_ID = 0x7E8
DOWNLOAD_TEMPLATE = 0
TRANSFER_TEMPLATE = 1
EXIT_TEMPLATE = 2
REQUEST_DOWNLOAD = bytes([0x34, 0x00, 0x44, 0x00, 0x00, 0x00, 0x00])
TRANSFER_DATA = 0x36
TRANSFER_EXIT = bytes([0x37])
POSITIVE_DOWNLOAD = 0x74
POSITIVE_TRANSFER = 0x76
POSITIVE_EXIT = 0x77
NEGATIVE_RESPONSE = 0x7F
RESPONSE_PENDING = 0x78
COUNTER_MASK = 0xFF
SIZE_BYTES = 4
TIMEOUT_S = 0.5
PENDING_TIMEOUT_S = 5.0
EXIT_ABORTED = 2


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("url")
    parser.add_argument("channel")
    parser.add_argument("--size", type=int, required=True)
    parser.add_argument("--block-size", type=int, default=4093)
    parser.add_argument("--extended", type=lambda text: int(text, 0), nargs=2, metavar=("TARGET", "SOURCE"))
    parser.add_argument("--fd", action="store_true")
    parser.add_argument("--skip-counter", type=int)
    parser.add_argument("--state-dir")
    arguments = parser.parse_args()

    image = bytes((index * 7 + 3) & COUNTER_MASK for index in range(arguments.size))
    bus = can.Bus(interface="canhub", channel=arguments.channel, url=arguments.url, state_dir=arguments.state_dir, reliable=True)
    try:
        result = flash(bus, image, arguments)
    except TransactionAborted as aborted:
        print(f"aborted reason={aborted.result.reason} step={aborted.result.failing_step}", flush=True)
        sys.exit(EXIT_ABORTED)
    finally:
        bus.shutdown()

    print(f"done steps={result.completed_steps} crc={zlib.crc32(image):08x}", flush=True)


def flash(bus, image, arguments):
    isotp = transport(arguments)
    with bus.transaction(exclusive=True, isotp=True) as transaction:
        transaction.template(DOWNLOAD_TEMPLATE, answer(POSITIVE_DOWNLOAD, isotp))
        transaction.template(TRANSFER_TEMPLATE, transfer_answer(isotp))
        transaction.template(EXIT_TEMPLATE, answer(POSITIVE_EXIT, isotp))
        transaction.send(steps(image, arguments))
        return transaction.wait()


def transport(arguments):
    target, source = arguments.extended if arguments.extended else (None, None)
    return Isotp(RESPONSE_ID, tx_address=target, rx_address=source, fd=arguments.fd)


def answer(positive, isotp):
    return Template(
        advance_on=Pattern(RESPONSE_ID, data=bytes([positive])),
        abort_on=Pattern(RESPONSE_ID, data=bytes([NEGATIVE_RESPONSE])),
        mode=Advance.COUNT,
        timeout=TIMEOUT_S,
        isotp=isotp,
    )


def transfer_answer(isotp):
    return Template(
        advance_on=Pattern(RESPONSE_ID, data=bytes([POSITIVE_TRANSFER, 0])),
        abort_on=Pattern(RESPONSE_ID, data=bytes([NEGATIVE_RESPONSE])),
        extend_on=Pattern(RESPONSE_ID, data=bytes([NEGATIVE_RESPONSE, 0, RESPONSE_PENDING]), data_mask=bytes([0xFF, 0, 0xFF])),
        mode=Advance.COUNT,
        timeout=TIMEOUT_S,
        extend_timeout=PENDING_TIMEOUT_S,
        abort_on_mismatch=True,
        isotp=isotp,
    )


def steps(image, arguments):
    yield Step(template=DOWNLOAD_TEMPLATE, arbitration_id=REQUEST_ID, data=REQUEST_DOWNLOAD + len(image).to_bytes(SIZE_BYTES, "big"))
    counter = 1
    for block_number, offset in enumerate(range(0, len(image), arguments.block_size), start=1):
        block = image[offset:offset + arguments.block_size]
        yield Step(
            template=TRANSFER_TEMPLATE,
            arbitration_id=REQUEST_ID,
            data=bytes([TRANSFER_DATA, counter]) + block,
            expect=bytes([POSITIVE_TRANSFER, counter]),
        )
        counter = (counter + 1) & COUNTER_MASK
        if block_number == arguments.skip_counter:
            counter = (counter + 1) & COUNTER_MASK
    yield Step(template=EXIT_TEMPLATE, arbitration_id=REQUEST_ID, data=TRANSFER_EXIT)


main()
