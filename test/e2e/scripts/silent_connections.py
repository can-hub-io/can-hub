"""Hold TCP connections open without ever speaking, and time when the peer closes them.

Run inside the client Server's namespace. Connects <count> sockets to
<host>:<port>, sends nothing, and waits up to <timeout> seconds for the remote
side to close each one. Prints a JSON summary with the seconds after which each
socket was closed by the peer (null when it was still open at the timeout).

Usage: silent_connections.py <host> <port> <count> <timeout>
"""

import json
import select
import socket
import sys
import time


def main() -> None:
    host, port, count, timeout = sys.argv[1], int(sys.argv[2]), int(sys.argv[3]), float(sys.argv[4])
    started = time.monotonic()
    sockets = [socket.create_connection((host, port)) for _ in range(count)]
    closed_after = [None] * count
    while None in closed_after and time.monotonic() - started < timeout:
        waiting = [sock for index, sock in enumerate(sockets) if closed_after[index] is None]
        readable, _, _ = select.select(waiting, [], [], 0.1)
        for sock in readable:
            if not peer_closed(sock):
                continue
            closed_after[sockets.index(sock)] = round(time.monotonic() - started, 2)
    print(json.dumps({"closed_after": closed_after}))


def peer_closed(sock: socket.socket) -> bool:
    try:
        return sock.recv(4096) == b""
    except ConnectionError:
        return True


if __name__ == "__main__":
    main()
