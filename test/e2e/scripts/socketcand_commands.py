"""Send raw socketcand commands to a bridge and collect its replies.

Run inside the client Server's namespace. Connects to the bridge's local
socketcand server (retrying until it listens), reads the greeting, then sends
each command and reads the one reply it answers with. Prints the replies as a
JSON list, in command order.

Usage: socketcand_commands.py <host> <port> <timeout> <command> [<command> ...]
"""

import json
import socket
import sys
import time


def connect(host: str, port: int, deadline: float) -> socket.socket:
    while True:
        try:
            return socket.create_connection((host, port), timeout=1)
        except OSError:
            if time.monotonic() > deadline:
                raise
            time.sleep(0.1)


def read_reply(connection: socket.socket, pending: bytearray) -> str:
    while b">" not in pending:
        chunk = connection.recv(4096)
        if not chunk:
            raise ConnectionError("bridge closed the connection")
        pending.extend(chunk)
    end = pending.index(b">") + 1
    reply = pending[:end].decode().strip()
    del pending[:end]
    return reply


def main() -> None:
    host, port, timeout, commands = sys.argv[1], int(sys.argv[2]), float(sys.argv[3]), sys.argv[4:]
    connection = connect(host, port, time.monotonic() + timeout)
    connection.settimeout(timeout)
    pending = bytearray()
    replies = []
    try:
        read_reply(connection, pending)
        for command in commands:
            connection.sendall(command.encode())
            replies.append(read_reply(connection, pending))
    finally:
        connection.close()
    print(json.dumps(replies))


if __name__ == "__main__":
    main()
