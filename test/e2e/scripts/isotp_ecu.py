"""A UDS flash target over the kernel's ISO-TP socket, for isotp_flash.py.

usage: isotp_ecu.py <interface> [--extended TARGET SOURCE] [--fd] [--pending]
                    [--block-size N] [--st-min MS]

Answers RequestDownload (34) with 74 20 <max block length>, each TransferData
(36 <counter> <data>) with 76 <counter> after checking the counter runs 1, 2,
.. 255, 0, 1, .., and RequestTransferExit (37) with 77 followed by the CRC-32
of the image, which it also prints with its size. A wrong counter gets
7F 36 73. --pending answers every TransferData with 7F 36 78 first. The
kernel stack does the segmentation and flow control (--block-size, --st-min).
"""

import argparse
import socket
import struct
import zlib

REQUEST_ID = 0x7E0
RESPONSE_ID = 0x7E8
SOL_CAN_ISOTP = 106
CAN_ISOTP_OPTS = 1
CAN_ISOTP_RECV_FC = 2
CAN_ISOTP_LL_OPTS = 5
CAN_ISOTP_EXTEND_ADDR = 0x002
CAN_ISOTP_TX_PADDING = 0x004
CAN_ISOTP_RX_EXT_ADDR = 0x200
CANFD_MTU = 72
FD_DATA_LENGTH = 64
PADDING = 0xCC
MESSAGE_MAX = 4095
MAX_BLOCK_LENGTH = 4095
REQUEST_DOWNLOAD = 0x34
TRANSFER_DATA = 0x36
TRANSFER_EXIT = 0x37
POSITIVE_OFFSET = 0x40
NEGATIVE_RESPONSE = 0x7F
RESPONSE_PENDING = 0x78
WRONG_BLOCK_SEQUENCE = 0x73
LENGTH_FORMAT = 0x20
COUNTER_MASK = 0xFF


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("interface")
    parser.add_argument("--extended", type=lambda text: int(text, 0), nargs=2, metavar=("TARGET", "SOURCE"))
    parser.add_argument("--fd", action="store_true")
    parser.add_argument("--pending", action="store_true")
    parser.add_argument("--block-size", type=int, default=0)
    parser.add_argument("--st-min", type=int, default=0)
    arguments = parser.parse_args()

    endpoint = open_endpoint(arguments)
    print("ready", flush=True)
    serve(endpoint, arguments)


def open_endpoint(arguments):
    endpoint = socket.socket(socket.AF_CAN, socket.SOCK_DGRAM, socket.CAN_ISOTP)
    flags = CAN_ISOTP_TX_PADDING
    address = 0
    rx_address = 0
    if arguments.extended:
        flags |= CAN_ISOTP_EXTEND_ADDR | CAN_ISOTP_RX_EXT_ADDR
        target, source = arguments.extended
        address, rx_address = source, target
    endpoint.setsockopt(SOL_CAN_ISOTP, CAN_ISOTP_OPTS, struct.pack("=LLBBBB", flags, 0, address, PADDING, PADDING, rx_address))
    endpoint.setsockopt(SOL_CAN_ISOTP, CAN_ISOTP_RECV_FC, struct.pack("=BBB", arguments.block_size, arguments.st_min, 0))
    if arguments.fd:
        endpoint.setsockopt(SOL_CAN_ISOTP, CAN_ISOTP_LL_OPTS, struct.pack("=BBB", CANFD_MTU, FD_DATA_LENGTH, 0))
    endpoint.bind((arguments.interface, REQUEST_ID, RESPONSE_ID))
    return endpoint


def serve(endpoint, arguments):
    image = bytearray()
    expected_counter = 1
    while True:
        request = endpoint.recv(MESSAGE_MAX)
        service = request[0]
        if service == REQUEST_DOWNLOAD:
            image.clear()
            expected_counter = 1
            endpoint.send(bytes([service + POSITIVE_OFFSET, LENGTH_FORMAT]) + MAX_BLOCK_LENGTH.to_bytes(2, "big"))
        elif service == TRANSFER_DATA:
            expected_counter = transfer(endpoint, request, image, expected_counter, arguments.pending)
        elif service == TRANSFER_EXIT:
            crc = zlib.crc32(image)
            print(f"image bytes={len(image)} crc={crc:08x}", flush=True)
            endpoint.send(bytes([service + POSITIVE_OFFSET]) + crc.to_bytes(4, "big"))


def transfer(endpoint, request, image, expected_counter, pending):
    counter = request[1]
    if counter != expected_counter:
        endpoint.send(bytes([NEGATIVE_RESPONSE, TRANSFER_DATA, WRONG_BLOCK_SEQUENCE]))
        return expected_counter
    if pending:
        endpoint.send(bytes([NEGATIVE_RESPONSE, TRANSFER_DATA, RESPONSE_PENDING]))
    image.extend(request[2:])
    endpoint.send(bytes([TRANSFER_DATA + POSITIVE_OFFSET, counter]))
    return (expected_counter + 1) & COUNTER_MASK


main()
