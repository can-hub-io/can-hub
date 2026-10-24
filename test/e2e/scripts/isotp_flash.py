"""Flashes an image into isotp_ecu.py over ISO-TP transaction steps.

usage: isotp_flash.py <url> <channel> --size N [--block-size B] [--extended TARGET SOURCE]
                      [--fd] [--skip-counter K] [--frame-first] [--state-dir D]

RequestDownload, then the image in TransferData blocks of B bytes (block
sequence counter 1, 2, .. 255, 0, ..), then RequestTransferExit, each one step
the agent segments. The image is deterministic, so its CRC-32 printed here
matches the one the ECU prints when the transfer arrived whole. --skip-counter
jumps the counter after block K, which the ECU refuses. --frame-first starts
the transaction with a plain TesterPresent frame on 0x7DF, before any
ISO-TP step.

Prints "done steps=N crc=XXXXXXXX" (exit 0) or "aborted reason=R step=S" (exit 2).
"""

import argparse
import sys
import zlib

import can

from canhub import IsotpLink, Match, Response, TransactionAborted

REQUEST_ID = 0x7E0
RESPONSE_ID = 0x7E8
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
TESTER_PRESENT_ID = 0x7DF
TESTER_PRESENT = bytes([0x02, 0x3E, 0x80])
PADDING = 0xCC


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("url")
    parser.add_argument("channel")
    parser.add_argument("--size", type=int, required=True)
    parser.add_argument("--block-size", type=int, default=4093)
    parser.add_argument("--extended", type=lambda text: int(text, 0), nargs=2, metavar=("TARGET", "SOURCE"))
    parser.add_argument("--fd", action="store_true")
    parser.add_argument("--skip-counter", type=int)
    parser.add_argument("--frame-first", action="store_true")
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
    link = isotp_link(arguments)
    download = answer(POSITIVE_DOWNLOAD)
    transfer = transfer_answer()
    transfer_exit = answer(POSITIVE_EXIT)
    with bus.transaction(exclusive=True) as transaction:
        if arguments.frame_first:
            transaction.send(TESTER_PRESENT_ID, TESTER_PRESENT)
        transaction.send_message(link, REQUEST_DOWNLOAD + len(image).to_bytes(SIZE_BYTES, "big"), download)
        counter = 1
        for block_number, offset in enumerate(range(0, len(image), arguments.block_size), start=1):
            block = image[offset:offset + arguments.block_size]
            transaction.send_message(link, bytes([TRANSFER_DATA, counter]) + block, transfer, where=Match(RESPONSE_ID, data={1: counter}))
            counter = (counter + 1) & COUNTER_MASK
            if block_number == arguments.skip_counter:
                counter = (counter + 1) & COUNTER_MASK
        transaction.send_message(link, TRANSFER_EXIT, transfer_exit)
    return transaction.result


def isotp_link(arguments):
    target, source = arguments.extended if arguments.extended else (None, None)
    return IsotpLink(REQUEST_ID, RESPONSE_ID, tx_address=target, rx_address=source, padding=PADDING, fd=arguments.fd)


def answer(positive):
    return Response(
        Match(RESPONSE_ID, data={0: positive}),
        timeout=TIMEOUT_S,
        failure=Match(RESPONSE_ID, data={0: NEGATIVE_RESPONSE}),
    )


def transfer_answer():
    return Response(
        Match(RESPONSE_ID, data={0: POSITIVE_TRANSFER}),
        timeout=TIMEOUT_S,
        failure=Match(RESPONSE_ID, data={0: NEGATIVE_RESPONSE}),
        pending=Match(RESPONSE_ID, data={0: NEGATIVE_RESPONSE, 2: RESPONSE_PENDING}),
        timeout_after_pending=PENDING_TIMEOUT_S,
        on_mismatch_abort=True,
    )


main()
