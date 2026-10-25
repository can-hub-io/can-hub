"""Run a transaction through the hub with python-can-hub.

usage: txn_run.py <url> <channel> uds <count> [--exclusive] [--timeout-ms T] [--within S] [--abort-after-ms T]
                   [--close-after-ms T] [--step-by-step] [--lossy] [--state-dir PATH]
       txn_run.py <url> <channel> batch <batches> <nodes> [--hold-ms T] [--keepalive-ms T] [--idle-timeout-ms T] [--state-dir PATH]

uds: <count> requests [02 36 n] to 0x7E0, each waiting for [.. 76 n] on 0x7E8,
aborting on a negative response and extended by a response pending.
batch: <batches> batches of 16 frames on 0x123, each acknowledged with its
crc by every node of a <nodes>-strong roster (ids 0x700 | node).
--abort-after-ms cancels the transaction that long after handing the steps
over; --close-after-ms shuts the bus down that long after, with the
transaction still running; --step-by-step waits for each step to complete
before handing over the next; --lossy opens the bus without the reliable channel a transaction needs;
--hold-ms hands the first batch over, then keeps reading for T ms without
handing over another step; --idle-timeout-ms is how long the agent then waits
for steps, sending a keepalive on 0x7DF every --keepalive-ms meanwhile.
Prints "granted" once the first step is at the agent, then "done steps=N
seconds=S" (exit 0), or "aborted reason=R step=S missing=[...] node=N ..."
(exit 2), or "failed: why" on stderr (exit 4); --within S fails with exit 3 when the
transaction took longer.
"""

import argparse
import itertools
import sys
import time

import can

from can import CanOperationError

from canhub import Keepalive, Match, Response, TransactionAborted

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
NODE_BITS = 0x1F
ACK_MARKER = 0x01
CRC_MASK = 0xFFFF
BYTE_MASK = 0xFF
BITS_PER_BYTE = 8
FRAME_BYTES = 8
MILLISECONDS_PER_SECOND = 1000
ACK_TIMEOUT_S = 0.5
STEP_COMPLETION_TIMEOUT_S = 2.0
POLL_S = 0.01
EXIT_ABORTED = 2
EXIT_TOO_SLOW = 3
EXIT_FAILED = 4


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
    parser.add_argument("--close-after-ms", type=int)
    parser.add_argument("--step-by-step", action="store_true")
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
            f" node={report.offending_node}"
            f" expected={report.expected.hex()} got={report.got.hex()}",
            flush=True,
        )
        sys.exit(EXIT_ABORTED)
    except CanOperationError as error:
        print(f"failed: {error}", file=sys.stderr, flush=True)
        sys.exit(EXIT_FAILED)
    finally:
        bus.shutdown()

    seconds = time.monotonic() - started
    print(f"done steps={result.completed_steps} seconds={seconds:.2f}", flush=True)
    if arguments.within is not None and seconds > arguments.within:
        sys.exit(EXIT_TOO_SLOW)


def run_requests(bus, arguments):
    count = arguments.counts[0]
    timeout = arguments.timeout_ms / MILLISECONDS_PER_SECOND
    transfer = Response(
        Match(RESPONSE_ID, data={1: POSITIVE_SERVICE}),
        timeout=timeout,
        failure=Match(RESPONSE_ID, data={1: NEGATIVE_RESPONSE}),
        pending=Match(RESPONSE_ID, data={1: NEGATIVE_RESPONSE, 3: RESPONSE_PENDING}),
        timeout_after_pending=timeout,
        on_mismatch_abort=True,
    )
    with bus.transaction(exclusive=arguments.exclusive) as transaction:
        for index in range(count):
            counter = index & BYTE_MASK
            transaction.send(REQUEST_ID, bytes([0x02, SERVICE, counter]), transfer, where=Match(RESPONSE_ID, data={2: counter}))
            announce_grant(index)
            if arguments.step_by_step:
                await_completion(bus, transaction, index + 1)
        if arguments.abort_after_ms is not None:
            time.sleep(arguments.abort_after_ms / MILLISECONDS_PER_SECOND)
            transaction.abort()
        if arguments.close_after_ms is not None:
            time.sleep(arguments.close_after_ms / MILLISECONDS_PER_SECOND)
            bus.shutdown()
    return transaction.result


def await_completion(bus, transaction, steps):
    """Read the bus, as a live application does, until the agent completed
    the steps handed over: each one is at the agent once its call returns."""
    deadline = time.monotonic() + STEP_COMPLETION_TIMEOUT_S
    while transaction.progress().completed_steps < steps:
        if time.monotonic() > deadline:
            raise CanOperationError(f"step {steps - 1} did not complete before the next was handed over")
        bus.recv(timeout=POLL_S)


def announce_grant(step):
    if step == 0:
        print("granted", flush=True)


def run_batches(bus, arguments):
    batches, nodes = arguments.counts[0], arguments.counts[1]
    acknowledgement = Response(
        Match(ACK_ID_BASE, data={0: ACK_MARKER}, ignored_bits=NODE_BITS),
        timeout=ACK_TIMEOUT_S,
        nodes=range(nodes),
        on_mismatch_abort=True,
    )
    with bus.transaction(idle_timeout=idle_timeout(arguments), keepalive=keepalive(arguments)) as transaction:
        for index, step in enumerate(step for batch in range(batches) for step in batch_steps(batch)):
            hand_over(transaction, step, acknowledgement)
            announce_grant(index)
            if index == BATCH_FRAMES + 1 and arguments.hold_ms is not None:
                hold_back(bus, arguments.hold_ms)
    return transaction.result


def hand_over(transaction, step, acknowledgement):
    kind, value = step
    if kind == "frame":
        transaction.send(BATCH_ID, value)
        return
    transaction.expect(acknowledgement, where=value)


def idle_timeout(arguments):
    return None if arguments.idle_timeout_ms is None else arguments.idle_timeout_ms / MILLISECONDS_PER_SECOND


def keepalive(arguments):
    if arguments.keepalive_ms is None:
        return None
    return Keepalive(KEEPALIVE_ID, KEEPALIVE_DATA, arguments.keepalive_ms / MILLISECONDS_PER_SECOND)


def hold_back(bus, hold_ms):
    """Keep reading, as a live application does, without handing over steps."""
    print("holding", flush=True)
    deadline = time.monotonic() + hold_ms / MILLISECONDS_PER_SECOND
    while time.monotonic() < deadline:
        bus.recv(timeout=deadline - time.monotonic())


def batch_steps(batch):
    """A batch's frames, then the wait for its acknowledgement."""
    payloads = [bytes((batch + frame + offset) & BYTE_MASK for offset in range(FRAME_BYTES)) for frame in range(BATCH_FRAMES)]
    crc = sum(sum(payload) for payload in payloads) & CRC_MASK
    steps = [("frame", payload) for payload in payloads]
    steps.append(("acknowledgement", Match(ACK_ID_BASE, data={1: crc & BYTE_MASK, 2: crc >> BITS_PER_BYTE}, ignored_bits=NODE_BITS)))
    return steps


main()
