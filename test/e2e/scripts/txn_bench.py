"""Times a request/response sequence against ecu_sim.py uds, two ways.

usage: txn_bench.py <url> <channel> transaction|lockstep <requests> [--state-dir D]

transaction: the agent runs the whole sequence (one round trip to the client
for the outcome). lockstep: the client sends each request and waits for its
answer before the next, the way a tester without transactions does, paying a
network round trip per request.

Prints "mode=M requests=N seconds=S per_request_ms=X".
"""

import argparse
import time

import can

from canhub import Advance, Pattern, Step, Template

REQUEST_ID = 0x7E0
RESPONSE_ID = 0x7E8
SERVICE = 0x36
POSITIVE_SERVICE = 0x76
BYTE_MASK = 0xFF
TIMEOUT_S = 2.0
MILLISECONDS_PER_SECOND = 1000


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("url")
    parser.add_argument("channel")
    parser.add_argument("mode", choices=["transaction", "lockstep"])
    parser.add_argument("requests", type=int)
    parser.add_argument("--state-dir")
    arguments = parser.parse_args()

    bus = can.Bus(interface="canhub", channel=arguments.channel, url=arguments.url, state_dir=arguments.state_dir, reliable=True)
    try:
        started = time.monotonic()
        if arguments.mode == "transaction":
            run_transaction(bus, arguments.requests)
        else:
            run_lockstep(bus, arguments.requests)
        seconds = time.monotonic() - started
    finally:
        bus.shutdown()

    per_request_ms = seconds * MILLISECONDS_PER_SECOND / arguments.requests
    print(f"mode={arguments.mode} requests={arguments.requests} seconds={seconds:.3f} per_request_ms={per_request_ms:.2f}", flush=True)


def run_transaction(bus, requests):
    answer = Template(
        advance_on=Pattern(RESPONSE_ID, data=bytes([0, POSITIVE_SERVICE, 0]), data_mask=bytes([0, BYTE_MASK, BYTE_MASK])),
        mode=Advance.COUNT,
        timeout=TIMEOUT_S,
        abort_on_mismatch=True,
    )
    with bus.transaction() as transaction:
        transaction.template(0, answer)
        transaction.send(request_step(index) for index in range(requests))
        transaction.wait()


def request_step(index):
    counter = index & BYTE_MASK
    return Step(arbitration_id=REQUEST_ID, data=bytes([0x02, SERVICE, counter]), expect=bytes([0, POSITIVE_SERVICE, counter]))


def run_lockstep(bus, requests):
    for index in range(requests):
        counter = index & BYTE_MASK
        bus.send(can.Message(arbitration_id=REQUEST_ID, data=[0x02, SERVICE, counter], is_extended_id=False))
        wait_answer(bus, counter)


def wait_answer(bus, counter):
    deadline = time.monotonic() + TIMEOUT_S
    while time.monotonic() < deadline:
        message = bus.recv(timeout=deadline - time.monotonic())
        if message is None:
            break
        if message.arbitration_id == RESPONSE_ID and message.data[1] == POSITIVE_SERVICE and message.data[2] == counter:
            return
    raise TimeoutError(f"no answer to request {counter}")


main()
