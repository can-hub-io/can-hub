"""Simulated nodes on a local bus, answering the transactions of txn_run.py.

usage: ecu_sim.py <interface> uds [--negative-at N] [--pending] [--delay-ms D]
       ecu_sim.py <interface> nodes <count> [--silent NODE ...] [--wrong-crc NODE ...]

uds: one ECU on 0x7E0 -> 0x7E8. Each request [02 36 n] gets [02 76 n]; the
N-th request (1-based) gets the negative response [03 7F 36 31] instead.
--pending answers [03 7F 36 78] twice, --delay-ms apart, and the positive
response --delay-ms after the second one. nodes: <count> nodes (ids
0x700 | node) that, after every batch of 16 frames on 0x123, acknowledge it
with [01 crc_lo crc_hi], the crc being the 16-bit sum of the batch payload
bytes; --silent nodes never answer, --wrong-crc nodes acknowledge the sum
plus one.
"""

import argparse
import time

import can

REQUEST_ID = 0x7E0
RESPONSE_ID = 0x7E8
SERVICE = 0x36
POSITIVE_SERVICE = 0x76
NEGATIVE_RESPONSE = 0x7F
GENERAL_REJECT = 0x31
RESPONSE_PENDING = 0x78
PENDING_RESPONSES = 2
BATCH_ID = 0x123
BATCH_FRAMES = 16
ACK_ID_BASE = 0x700
ACK_MARKER = 0x01
CRC_MASK = 0xFFFF
BYTE_MASK = 0xFF
BITS_PER_BYTE = 8
MILLISECONDS_PER_SECOND = 1000


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("interface")
    parser.add_argument("mode", choices=["uds", "nodes"])
    parser.add_argument("count", nargs="?", type=int, default=0)
    parser.add_argument("--negative-at", type=int, default=0)
    parser.add_argument("--pending", action="store_true")
    parser.add_argument("--delay-ms", type=int, default=0)
    parser.add_argument("--silent", type=int, nargs="*", default=[])
    parser.add_argument("--wrong-crc", type=int, nargs="*", default=[])
    arguments = parser.parse_args()

    bus = can.Bus(interface="socketcan", channel=arguments.interface)
    print("ready", flush=True)
    if arguments.mode == "uds":
        answer_requests(bus, arguments)
    else:
        acknowledge_batches(bus, arguments)


def answer_requests(bus, arguments):
    requests = 0
    for message in bus:
        if message.arbitration_id != REQUEST_ID or len(message.data) < 3:
            continue
        requests += 1
        counter = message.data[2]
        if requests == arguments.negative_at:
            send(bus, RESPONSE_ID, [0x03, NEGATIVE_RESPONSE, SERVICE, GENERAL_REJECT])
            continue
        for _ in range(PENDING_RESPONSES if arguments.pending else 0):
            send(bus, RESPONSE_ID, [0x03, NEGATIVE_RESPONSE, SERVICE, RESPONSE_PENDING])
            time.sleep(arguments.delay_ms / MILLISECONDS_PER_SECOND)
        if arguments.delay_ms and not arguments.pending:
            time.sleep(arguments.delay_ms / MILLISECONDS_PER_SECOND)
        send(bus, RESPONSE_ID, [0x02, POSITIVE_SERVICE, counter])
        print(f"answered {requests}", flush=True)


def acknowledge_batches(bus, arguments):
    batch = bytearray()
    frames = 0
    for message in bus:
        if message.arbitration_id != BATCH_ID:
            continue
        batch.extend(message.data)
        frames += 1
        if frames < BATCH_FRAMES:
            continue
        crc = sum(batch) & CRC_MASK
        for node in range(arguments.count):
            if node in arguments.silent:
                continue
            acknowledged = (crc + 1) & CRC_MASK if node in arguments.wrong_crc else crc
            send(bus, ACK_ID_BASE | node, [ACK_MARKER, acknowledged & BYTE_MASK, acknowledged >> BITS_PER_BYTE])
        print(f"acknowledged crc {crc:04x}", flush=True)
        batch.clear()
        frames = 0


def send(bus, arbitration_id, data):
    bus.send(can.Message(arbitration_id=arbitration_id, data=data, is_extended_id=False))


main()
