#!/bin/sh
# E0 spike comparison: QUIC datagrams vs plain TCP (NODELAY) vs plain UDP.
#
# Same methodology as run-bench.sh: frames generated on vcan0, tunneled to
# vcan1, correlated by payload on a single candump clock. Requires vcan0 and
# vcan1 up, ./tunnel and ./plain_tunnel built, can-utils, python3, openssl.
set -e

CERT=/tmp/e0-cert.pem
KEY=/tmp/e0-key.pem
PORT=4433
FRAME_COUNT=5000

[ -f "$CERT" ] || openssl req -x509 -newkey rsa:2048 -nodes \
    -keyout "$KEY" -out "$CERT" -days 7 -subj "/CN=localhost" 2>/dev/null

cpu_jiffies() {
    awk '{print $14 + $15}' "/proc/$1/stat"
}

run_rate() {
    GAP_MS=$1
    RATE_LABEL=$2

    candump -L vcan0 vcan1 > /tmp/e0-bench.log &
    DUMP_PID=$!
    sleep 0.3

    CLIENT_CPU0=$(cpu_jiffies $CLIENT_PID)
    SERVER_CPU0=$(cpu_jiffies $SERVER_PID)
    START=$(date +%s.%N)

    cangen vcan0 -g "$GAP_MS" -I 555 -L 8 -D i -n $FRAME_COUNT

    sleep 0.5
    END=$(date +%s.%N)
    CLIENT_CPU1=$(cpu_jiffies $CLIENT_PID)
    SERVER_CPU1=$(cpu_jiffies $SERVER_PID)
    kill $DUMP_PID 2>/dev/null
    wait $DUMP_PID 2>/dev/null || true

    python3 - "$RATE_LABEL" "$START" "$END" \
        "$CLIENT_CPU0" "$CLIENT_CPU1" "$SERVER_CPU0" "$SERVER_CPU1" <<'EOF'
import sys

label, start, end, client_cpu0, client_cpu1, server_cpu0, server_cpu1 = sys.argv[1:8]
elapsed = float(end) - float(start)
hz = 100.0

sent = {}
deltas = []
for line in open("/tmp/e0-bench.log"):
    parts = line.split()
    if len(parts) != 3:
        continue
    timestamp = float(parts[0].strip("()"))
    interface, frame = parts[1], parts[2]
    if interface == "vcan0":
        sent[frame] = timestamp
    elif interface == "vcan1" and frame in sent:
        deltas.append((timestamp - sent[frame]) * 1e6)

deltas.sort()
count = len(deltas)
received_percent = 100.0 * count / len(sent) if sent else 0.0
client_cpu = (int(client_cpu1) - int(client_cpu0)) / hz / elapsed * 100
server_cpu = (int(server_cpu1) - int(server_cpu0)) / hz / elapsed * 100


def percentile(p):
    return deltas[min(count - 1, int(count * p))] if count else 0.0


print(f"{label}: sent={len(sent)} received={count} ({received_percent:.1f}%) "
      f"p50={percentile(0.50):.0f}us p99={percentile(0.99):.0f}us max={deltas[-1] if count else 0:.0f}us "
      f"cpu client={client_cpu:.1f}% server={server_cpu:.1f}%")
EOF
}

stop_tunnels() {
    pkill -f './tunnel (server|client)' 2>/dev/null || true
    pkill -f './plain_tunnel' 2>/dev/null || true
    sleep 0.3
}

bench_transport() {
    TRANSPORT=$1

    stop_tunnels
    case "$TRANSPORT" in
    quic)
        ./tunnel server $PORT vcan1 "$CERT" "$KEY" 2>/tmp/e0-server.log &
        SERVER_PID=$!
        sleep 0.3
        ./tunnel client 127.0.0.1 $PORT vcan0 2>/tmp/e0-client.log &
        CLIENT_PID=$!
        sleep 1
        grep -q 'handshake completed' /tmp/e0-client.log || {
            echo "$TRANSPORT: handshake failed"; return 1;
        }
        ;;
    tcp | udp)
        ./plain_tunnel "$TRANSPORT-server" $PORT vcan1 2>/tmp/e0-server.log &
        SERVER_PID=$!
        sleep 0.3
        ./plain_tunnel "$TRANSPORT-client" 127.0.0.1 $PORT vcan0 2>/tmp/e0-client.log &
        CLIENT_PID=$!
        sleep 1
        if [ "$TRANSPORT" = udp ]; then
            # the UDP server learns its peer from the first packet
            cansend vcan0 7FF#00
            sleep 0.2
        fi
        ;;
    esac

    echo "--- $TRANSPORT ---"
    run_rate 2 " 500 fps"
    run_rate 0.5 "2000 fps"
    run_rate 0.2 "5000 fps"
}

echo "frames per rate: $FRAME_COUNT"
bench_transport quic
bench_transport tcp
bench_transport udp
stop_tunnels
