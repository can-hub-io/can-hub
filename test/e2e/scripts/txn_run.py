"""Run a transaction through the hub with python-can-hub.

usage: txn_run.py <url> <channel> uds <count> [--exclusive] [--timeout-ms T] [--within S] [--abort-after-ms T]
                   [--lossy] [--state-dir PATH]
       txn_run.py <url> <channel> batch <batches> <nodes> [--hold-ms T] [--keepalive-ms T] [--idle-timeout-ms T] [--state-dir PATH]

uds: <count> requests [02 36 n] to 0x7E0, each waiting for [.. 76 n] on 0x7E8,
aborting on a negative response and extended by a response pending.
batch: <batches> batches of 16 frames on 0x123, each acknowledged with its
crc by every node of a <nodes>-strong roster (ids 0x700 | node).
--abort-after-ms cancels the transaction that long after handing the steps
over; --lossy opens the bus without the reliable channel a transaction needs;
--hold-ms hands the first batch over, then keeps reading for T ms without
handing over another step; --idle-timeout-ms is how long the agent then waits
for steps, sending a keepalive on 0x7DF every --keepalive-ms meanwhile.
Prints "granted", then "done steps=N seconds=S" (exit 0), or "aborted
reason=R step=S missing=[...]" (exit 2); --within S fails with exit 3 when
the transaction took longer.
"""

import argparse
import itertools
import sys
import time

import can

from canhub import Advance, Keepalive, Origin, Pattern, Step, Template, TransactionAborted

REQUEST_ID = 0x7E0
RESPONSE_ID = 0x7E8
SERVICE = 0x36
POSITIVE_SERVICE = 0x76
NEGATIVE_RESPONSE = 0x7F
RESPONSE_PENDING = 0x78
BATCH_ID = 0x123
KEEPALIVE_ID = 0x7DF
KEEPALIVE_DATA = bytes([0x02, 0x3E, 0x80])
BATCH_FRAMES = 16
ACK_ID_BASE = 0x700
ACK_ID_MASK = 0x7E0
NODE_MASK = 0x1F
ACK_MARKER = 0x01
CRC_MASK = 0xFFFF
BYTE_MASK = 0xFF
BITS_PER_BYTE = 8
FRAME_BYTES = 8
MILLISECONDS_PER_SECOND = 1000
ACK_TIMEOUT_S = 0.5
RESPONSE_TEMPLATE = 0
WRITE_TEMPLATE = 1
ACK_TEMPLATE = 2
EXIT_ABORTED = 2
EXIT_TOO_SLOW = 3


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("url")
    parser.add_argument("channel")
    parser.add_argument("mode", choices=["uds", "batch"])
    parser.add_argument("counts", type=int, nargs="+")
    parser.add_argument("--exclusive", action="store_true")
    parser.add_argument("--timeout-ms", type=int, default=200)
    parser.add_argument("--within", type=float)
    parser.add_argument("--state-dir")
    parser.add_argument("--abort-after-ms", type=int)
    parser.add_argument("--lossy", action="store_true")
    parser.add_argument("--hold-ms", type=int)
    parser.add_argument("--keepalive-ms", type=int)
    parser.add_argument("--idle-timeout-ms", type=int)
    arguments = parser.parse_args()

    bus = can.Bus(
        interface="canhub",
        channel=arguments.channel,
        url=arguments.url,
        state_dir=arguments.state_dir,
        reliable=not arguments.lossy,
    )
    started = time.monotonic()
    try:
        if arguments.mode == "uds":
            result = run_requests(bus, arguments)
        else:
            result = run_batches(bus, arguments)
    except TransactionAborted as aborted:
        report = aborted.result
        print(
            f"aborted reason={report.reason} step={report.failing_step} missing={sorted(report.missing)}"
            f" origin={report.offending_origin}"
            f" expected={report.expected.hex()} got={report.got.hex()}",
            flush=True,
        )
        sys.exit(EXIT_ABORTED)
    finally:
        bus.shutdown()

    seconds = time.monotonic() - started
    print(f"done steps={result.completed_steps} seconds={seconds:.2f}", flush=True)
    if arguments.within is not None and seconds > arguments.within:
        sys.exit(EXIT_TOO_SLOW)


def run_requests(bus, arguments):
    count = arguments.counts[0]
    template = Template(
        advance_on=Pattern(RESPONSE_ID, data=bytes([0, POSITIVE_SERVICE, 0]), data_mask=bytes([0, BYTE_MASK, BYTE_MASK])),
        abort_on=Pattern(RESPONSE_ID, data=bytes([0, NEGATIVE_RESPONSE]), data_mask=bytes([0, BYTE_MASK])),
        extend_on=Pattern(
            RESPONSE_ID,
            data=bytes([0, NEGATIVE_RESPONSE, 0, RESPONSE_PENDING]),
            data_mask=bytes([0, BYTE_MASK, 0, BYTE_MASK]),
        ),
        mode=Advance.COUNT,
        timeout=arguments.timeout_ms / MILLISECONDS_PER_SECOND,
        abort_on_mismatch=True,
    )
    with bus.transaction(exclusive=arguments.exclusive) as transaction:
        print("granted", flush=True)
        transaction.template(RESPONSE_TEMPLATE, template)
        transaction.send(request_step(index) for index in range(count))
        if arguments.abort_after_ms is not None:
            time.sleep(arguments.abort_after_ms / MILLISECONDS_PER_SECOND)
            transaction.abort()
        return transaction.wait()


def request_step(index):
    counter = index & BYTE_MASK
    return Step(
        template=RESPONSE_TEMPLATE,
        arbitration_id=REQUEST_ID,
        data=bytes([0x02, SERVICE, counter]),
        expect=bytes([0, POSITIVE_SERVICE, counter]),
    )


def run_batches(bus, arguments):
    batches, nodes = arguments.counts[0], arguments.counts[1]
    acknowledgement = Template(
        advance_on=Pattern(ACK_ID_BASE, can_mask=ACK_ID_MASK, data=bytes([ACK_MARKER, 0, 0])),
        mode=Advance.ROSTER_ALL,
        timeout=ACK_TIMEOUT_S,
        abort_on_mismatch=True,
    )
    with bus.transaction(
        roster=range(nodes), origin=Origin(mask=NODE_MASK), idle_timeout=idle_timeout(arguments), keepalive=keepalive(arguments)
    ) as transaction:
        print("granted", flush=True)
        transaction.template(WRITE_TEMPLATE, Template())
        transaction.template(ACK_TEMPLATE, acknowledgement)
        steps = (step for batch in range(batches) for step in batch_steps(batch))
        if arguments.hold_ms is not None:
            transaction.send(itertools.islice(steps, BATCH_FRAMES + 2))
            hold_back(bus, arguments.hold_ms)
        transaction.send(steps)
        return transaction.wait()


def idle_timeout(arguments):
    return None if arguments.idle_timeout_ms is None else arguments.idle_timeout_ms / MILLISECONDS_PER_SECOND


def keepalive(arguments):
    if arguments.keepalive_ms is None:
        return None
    return Keepalive(KEEPALIVE_ID, KEEPALIVE_DATA, arguments.keepalive_ms / MILLISECONDS_PER_SECOND)


def hold_back(bus, hold_ms):
    """Keep reading, as a live application does, without handing over steps.
    The library holds the last step handed over back; it is the next batch's
    first frame, so the acknowledgement before it is already at the agent."""
    print("holding", flush=True)
    deadline = time.monotonic() + hold_ms / MILLISECONDS_PER_SECOND
    while time.monotonic() < deadline:
        bus.recv(timeout=deadline - time.monotonic())


def batch_steps(batch):
    payloads = [bytes((batch + frame + offset) & BYTE_MASK for offset in range(FRAME_BYTES)) for frame in range(BATCH_FRAMES)]
    crc = sum(sum(payload) for payload in payloads) & CRC_MASK
    steps = [Step(template=WRITE_TEMPLATE, arbitration_id=BATCH_ID, data=payload) for payload in payloads]
    steps.append(Step(template=ACK_TEMPLATE, expect=bytes([ACK_MARKER, crc & BYTE_MASK, crc >> BITS_PER_BYTE])))
    return steps


main()
