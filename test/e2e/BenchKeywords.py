"""Robot keyword layer over the bench library.

Keywords return Can* instances so tests read like:

    ${hub}=     Start CAN HUB On ${LOCAL_SERVER} With ${config}
    ${agent}=   Start CAN Agent On ${LOCAL_SERVER} With ${config}
"""

import json
import statistics
import time

from robot.api import logger
from robot.api.deco import keyword, library

from lib import (
    AgentConfig, CanAgent, CanClient, CanCli, CanHub, ClientConfig, HubConfig,
)
from lib.binaries import BIN_DIR, binary
from lib.rows import parse_candump

CONSUME_SCRIPT = "/work/test/e2e/scripts/consume.py"
PRODUCE_SCRIPT = "/work/test/e2e/scripts/produce.py"
LATENCY_SEND_SCRIPT = "/work/test/e2e/scripts/latency_send.py"
BENCH_SEND_SCRIPT = "/work/test/e2e/scripts/bench_send.py"


@library(scope="GLOBAL")
class BenchKeywords:

    # ---------- bench lifecycle ----------

    @keyword("Setup Bench")
    def setup_bench(self, bench):
        bench.setup()

    @keyword("Teardown Bench")
    def teardown_bench(self, bench):
        bench.teardown()

    @keyword("Reset Bench")
    def reset_bench(self, bench):
        for server in bench.servers.values():
            server.stop_all()

    @keyword("Create VCAN On ${server}")
    def create_vcan_on(self, server, name="vcan0"):
        return server.make_vcan(name)

    # ---------- configuration builders ----------

    @keyword("Hub Configuration")
    def hub_configuration(self, **kwargs) -> HubConfig:
        return HubConfig(**_normalise(kwargs))

    @keyword("Benchmark Hub Configuration")
    def benchmark_hub_configuration(self) -> HubConfig:
        return HubConfig(listen=["quic://0.0.0.0:7227", "tls://0.0.0.0:7227", "tcp://0.0.0.0:7228"])

    @keyword("Agent Configuration")
    def agent_configuration(self, connect, name, *interfaces, **kwargs) -> AgentConfig:
        return AgentConfig(connect=connect, name=name, interfaces=list(interfaces),
                           **_normalise(kwargs))

    @keyword("Client Configuration")
    def client_configuration(self, command, *args, **kwargs) -> ClientConfig:
        return ClientConfig(command=command, args=list(args), **_normalise(kwargs))

    # ---------- start binaries ----------

    @keyword("Start CAN HUB On ${server} With ${config}")
    def start_can_hub_with(self, server, config) -> CanHub:
        return CanHub.start(server, config)

    @keyword("Start CAN HUB On ${server}")
    def start_can_hub(self, server) -> CanHub:
        return CanHub.start(server)

    @keyword("Start CAN Agent On ${server} With ${config}")
    def start_can_agent_with(self, server, config) -> CanAgent:
        return CanAgent.start(server, config)

    @keyword("Start CAN Client On ${server} With ${config}")
    def start_can_client_with(self, server, config) -> CanClient:
        return CanClient.start(server, config)

    # ---------- actions ----------

    @keyword("Run CLI On Hub ${hub}")
    def run_cli_on_hub(self, hub, *args):
        return hub.cli(*args)

    @keyword("List Interfaces On ${server}")
    def list_interfaces_on(self, server, connect=None):
        return CanClient.list(server, connect)

    @keyword("Send Frame On ${server}")
    def send_frame_on(self, server, interface, frame, connect=None):
        return CanClient.send(server, interface, frame, connect)

    @keyword("Inject CAN Frame On ${server}")
    def inject_can_frame_on(self, server, interface, frame):
        return server.exec("cansend", interface, frame)

    @keyword("Wait Until Agent ${agent} Registered On ${hub}")
    def wait_until_agent_registered(self, agent, hub, timeout=5):
        agent.wait_registered(hub, float(timeout))

    @keyword("Wait Until Client ${client} Has Open Channel")
    def wait_until_client_open(self, client, hub, timeout=5):
        deadline = time.monotonic() + float(timeout)
        while time.monotonic() < deadline:
            if any(row.channel != "-" for row in hub.clients()):
                return
            time.sleep(0.1)
        raise AssertionError("no client channel opened on the hub")

    @keyword("Install Python Package On ${server}")
    def install_python_package_on(self, server, path):
        staging = "/tmp/python-package-staging"
        server.exec("rm", "-rf", staging)
        server.exec("cp", "-r", path, staging)
        server.exec("rm", "-rf", f"{staging}/build", f"{staging}/dist")
        server.exec("sh", "-c", f"rm -rf {staging}/*.egg-info")
        server.exec("python3", "-m", "pip", "install", "--quiet",
                    "--break-system-packages", "--no-build-isolation", staging)

    @keyword("Run Python ${script} On ${server}")
    def run_python_on(self, script, server, *args):
        return server.exec("env", _canhub_library(), "python3", script, *args, check=False)

    @keyword("Start Python ${script} On ${server}")
    def start_python_on(self, script, server, *args):
        return server.exec("env", _canhub_library(), "python3", script, *args,
                           background=True, log_name="pycan")

    @keyword("Fingerprint Of ${path} On ${server}")
    def fingerprint_of(self, path, server):
        return server.exec("python3", "-m", "canhub", "fingerprint", path).stdout.strip()

    @keyword("Run Binary ${name} On ${server}")
    def run_binary_on(self, name, server, *args):
        return server.exec(binary(name), *args, check=False)

    @keyword("Start Binary ${name} On ${server}")
    def start_binary_on(self, name, server, *args):
        return server.exec(binary(name), *args, background=True, log_name=name)

    @keyword("Log Of ${process} Should Contain ${text}")
    def log_should_contain(self, process, text, timeout=5):
        deadline = time.monotonic() + float(timeout)
        while time.monotonic() < deadline:
            if text in process.read_log():
                return
            time.sleep(0.1)
        raise AssertionError(f"log does not contain {text}\n{process.read_log()}")

    @keyword("Start Candump On ${server}")
    def start_candump_on(self, server, interface="vcan0"):
        return server.candump(interface)

    @keyword("Candump ${process} Should Capture ${can_id}#${data}")
    def candump_should_capture(self, process, can_id, data, timeout=5):
        want = (can_id.upper(), data.upper())
        deadline = time.monotonic() + float(timeout)
        while time.monotonic() < deadline:
            for frame in parse_candump(process.read_log()):
                if (frame.can_id, frame.data) == want:
                    return
            time.sleep(0.1)
        raise AssertionError(
            f"candump did not capture {can_id}#{data}\n{process.read_log()}"
        )

    @keyword("Client ${client} Should Not Receive ${can_id}#${data}")
    def client_should_not_receive(self, client, can_id, data, settle=1.0):
        time.sleep(float(settle))
        want = (can_id.upper(), data.upper())
        for frame in client.dumped_frames():
            if (frame.can_id, frame.data) == want:
                raise AssertionError(f"client received filtered-out frame {can_id}#{data}")

    @keyword("Client ${client} Should Receive ${can_id}#${data}")
    def client_should_receive(self, client, can_id, data, timeout=5):
        want = (can_id.upper(), data.upper())
        deadline = time.monotonic() + float(timeout)
        while time.monotonic() < deadline:
            for frame in client.dumped_frames():
                if (frame.can_id, frame.data) == want:
                    return
            time.sleep(0.1)
        raise AssertionError(
            f"client did not receive {can_id}#{data}\n{client.process.read_log()}"
        )

    # ---------- fairness / backpressure ----------

    @keyword("Limit Egress On ${server} To ${rate}")
    def limit_egress_on(self, server, rate):
        server.exec("tc", "qdisc", "replace", "dev", "eth0", "root", "tbf",
                    "rate", rate, "burst", "16kb", "latency", "50ms", check=False)

    @keyword("Throttle Egress On ${server} To ${rate}")
    def throttle_egress_on(self, server, rate):
        server.exec("tc", "qdisc", "replace", "dev", "eth0", "root", "tbf",
                    "rate", rate, "burst", "3kb", "latency", "5ms", check=False)

    @keyword("Set Link Jitter On ${server} ${delay} ${jitter}")
    def set_link_jitter(self, server, delay, jitter):
        server.exec("tc", "qdisc", "replace", "dev", "eth0", "root",
                    "netem", "delay", delay, jitter, check=False)

    @keyword("Set Link Loss On ${server} To ${percent}")
    def set_link_loss(self, server, percent):
        server.exec("tc", "qdisc", "replace", "dev", "eth0", "root",
                    "netem", "loss", f"{percent}%", check=False)

    @keyword("Bring Link Down On ${server}")
    def bring_link_down(self, server):
        server.exec("ip", "link", "set", "eth0", "down", check=False)

    @keyword("Bring Link Up On ${server} With Octet ${octet}")
    def bring_link_up_with_octet(self, server, octet):
        server.exec("ip", "addr", "flush", "dev", "eth0", check=False)
        server.exec("ip", "addr", "add", f"10.0.0.{octet}/24", "dev", "eth0", check=False)
        server.exec("ip", "link", "set", "eth0", "up", check=False)

    @keyword("Sequence Integrity Of ${capture} Over ${count} Frames")
    def sequence_integrity(self, capture, count, can_id="123"):
        values = [int(frame.data, 16) for frame in parse_candump(capture.read_log())
                  if frame.can_id == can_id.upper() and frame.data]
        in_order = all(values[i] < values[i + 1] for i in range(len(values) - 1))
        complete = sorted(values) == list(range(int(count)))
        return {"received": len(values), "in_order": in_order, "complete": complete}

    @keyword("Stream Timestamped Frames On ${server} ${interface}")
    def stream_timestamped_frames(self, server, interface, can_id="123", gap=0.001, duration=4.0):
        process = server.exec("python3", LATENCY_SEND_SCRIPT, interface, can_id, str(gap), str(duration),
                              background=True, log_name="latency_send")
        process.wait(timeout=float(duration) + 30)
        return int(process.read_log().strip().splitlines()[-1])

    @keyword("Stream Benchmark Frames On ${server} ${interface}")
    def stream_benchmark_frames(self, server, interface, can_id="123", count=20000, gap=0.0):
        process = server.exec("python3", BENCH_SEND_SCRIPT, interface, can_id, str(int(count)), str(gap),
                              background=True, log_name="bench_send")
        process.wait(timeout=120)
        return int(process.read_log().strip().splitlines()[-1])

    @keyword("Benchmark Metrics Of ${capture} Over ${sent} For ${can_id}")
    def benchmark_metrics(self, capture, sent, can_id="123"):
        sent = int(sent)
        frames = [frame for frame in parse_candump(capture.read_log())
                  if frame.can_id == can_id.upper() and len(frame.data) >= 16]
        if not frames:
            return {"received": 0, "lost": sent, "out_of_order": 0, "rate_fps": 0,
                    "latency_avg_ms": 0, "latency_p95_ms": 0, "jitter_ms": 0}

        sequences = [int(frame.data[:8], 16) for frame in frames]
        ages_ms = [
            ((int(frame.timestamp * 1_000_000) - int(frame.data[8:16], 16)) & 0xFFFFFFFF) / 1000.0
            for frame in frames
        ]
        out_of_order = sum(1 for i in range(1, len(sequences)) if sequences[i] < sequences[i - 1])
        span = frames[-1].timestamp - frames[0].timestamp
        sorted_ages = sorted(ages_ms)
        return {
            "received": len(frames),
            "lost": sent - len(set(sequences)),
            "out_of_order": out_of_order,
            "rate_fps": round(len(frames) / span) if span > 0 else 0,
            "latency_avg_ms": round(statistics.fmean(ages_ms)),
            "latency_p95_ms": round(sorted_ages[int(len(sorted_ages) * 0.95)]),
            "jitter_ms": round(statistics.pstdev(ages_ms)),
        }

    @keyword("Run Scaled Benchmark ${scheme} Port ${port} Reliable ${reliable} Level ${level}")
    def run_scaled_benchmark(self, scheme, port, reliable, level, hub_server,
                             agent_server, client_server, count=2000, gap=0.001, can_id="123"):
        """One scaled scenario: a hub, ${level} agents (each with a varied number of
        interfaces) and ${level} clients (client i consumes agent i's first interface).
        Returns aggregate transport metrics across all clients. The extra interfaces per
        agent load the registry/registration path; only the first interface of each agent
        carries measured traffic, so the client count tracks the agent count."""
        level = int(level)
        reliable = str(reliable).lower() == "true"
        hub = CanHub.start(hub_server, HubConfig(
            listen=["quic://0.0.0.0:7227", "tls://0.0.0.0:7227", "tcp://0.0.0.0:7228"]))
        connect = f"{scheme}://{hub_server.fqdn}:{port}"

        agents = []
        for index in range(level):
            interfaces = [f"v{index}_{slot}" for slot in range(_varied_interface_count(index))]
            for interface in interfaces:
                agent_server.make_vcan(interface)
            agent = CanAgent.start(agent_server, AgentConfig(
                connect=connect, name=f"a{index}", interfaces=interfaces,
                state_dir=str(agent_server.work_dir / f"agent-{index}")))
            agents.append(agent)
        for agent in agents:
            agent.wait_registered(hub, 30.0)
        hub.cli("acl", "add", "*", "*/*", "rw")

        captures = []
        for index in range(level):
            sink = client_server.make_vcan(f"c{index}")
            CanClient.start(client_server, ClientConfig(
                command="attach", args=[f"a{index}/v{index}_0", sink], connect=connect,
                reliable=reliable, state_dir=str(client_server.work_dir / f"client-{index}")))
            captures.append(client_server.candump(sink, log_name=f"candump-{index}"))
        self.wait_until_channels_open(level, hub, timeout=60)

        senders = [agent_server.exec("python3", BENCH_SEND_SCRIPT, f"v{index}_0", can_id,
                                     str(int(count)), str(gap),
                                     background=True, log_name=f"bench_send-{index}")
                   for index in range(level)]
        for sender in senders:
            sender.wait(timeout=180)

        # Wait for delivery to finish across ALL captures with a single global deadline:
        # ordered transports reach count and break early; a lossy datagram run waits out
        # the deadline once (not 30 s per capture). A short tail pause lets reorder settle.
        count = int(count)
        deadline = time.monotonic() + 30
        while time.monotonic() < deadline:
            if all(self.frames_captured_by(capture) >= count for capture in captures):
                break
            time.sleep(0.5)
        time.sleep(2)

        per_client = []
        spans = []
        for capture in captures:
            per_client.append(self.benchmark_metrics(capture, count, can_id))
            timestamps = [frame.timestamp for frame in parse_candump(capture.read_log())
                          if frame.can_id == can_id.upper()]
            if timestamps:
                spans.append((min(timestamps), max(timestamps)))
        result = _aggregate(per_client, level, sum(_varied_interface_count(i) for i in range(level)))
        result["hub_tx_dropped"] = sum(int(row.tx_dropped) for row in hub.interfaces())
        total_time = max(end for _, end in spans) - min(start for start, _ in spans) if spans else 0.0
        result["total_time_s"] = round(total_time, 3)
        result["global_rate_fps"] = round(result["received"] / total_time) if total_time > 0 else 0
        return result

    @keyword("Log Scaled Metrics ${level} ${scheme} Reliable ${reliable} ${metrics}")
    def log_scaled_metrics(self, level, scheme, reliable, metrics):
        logger.console(
            f"L{level} {scheme} rel={reliable}: "
            f"agents={metrics['agents']} ifaces={metrics['interfaces']} clients={metrics['clients']} "
            f"rx={metrics['received']} lost={metrics['lost']} hub_drop={metrics['hub_tx_dropped']} "
            f"ooo={metrics['out_of_order']} time={metrics['total_time_s']}s "
            f"rate_global={metrics['global_rate_fps']}fps rate_sum={metrics['rate_fps']}fps "
            f"lat={metrics['latency_avg_ms']}ms p95={metrics['latency_p95_ms']}ms jitter={metrics['jitter_ms']}ms"
        )

    @keyword("Wait Until Capture ${capture} Settles")
    def wait_until_capture_settles(self, capture, can_id="123", quiet=3.0, timeout=90.0):
        deadline = time.monotonic() + float(timeout)
        last_count = -1
        settled_at = time.monotonic()
        while time.monotonic() < deadline:
            count = sum(1 for frame in parse_candump(capture.read_log())
                        if frame.can_id == can_id.upper())
            if count != last_count:
                last_count = count
                settled_at = time.monotonic()
            elif time.monotonic() - settled_at >= float(quiet):
                return count
            time.sleep(0.25)
        return last_count

    @keyword("Frame Age Stats Of ${capture} For ${can_id}")
    def frame_age_stats(self, capture, can_id):
        ages = sorted(
            frame.timestamp - int(frame.data, 16) / 1_000_000
            for frame in parse_candump(capture.read_log())
            if frame.can_id == can_id.upper() and frame.data
        )
        if not ages:
            return {"count": 0, "avg_ms": 0, "max_ms": 0, "p95_ms": 0}
        return {
            "count": len(ages),
            "avg_ms": round(sum(ages) / len(ages) * 1000),
            "max_ms": round(ages[-1] * 1000),
            "p95_ms": round(ages[int(len(ages) * 0.95)] * 1000),
        }

    @keyword("Shape CAN Sink On ${server} ${interface} To ${rate}")
    def shape_can_sink_on(self, server, interface, rate):
        server.exec("ip", "link", "set", "dev", interface, "txqueuelen", "10", check=False)
        server.exec("tc", "qdisc", "replace", "dev", interface, "root", "tbf",
                    "rate", rate, "burst", "2kb", "latency", "20ms", check=False)

    @keyword("TX Dropped On ${hub} For ${name}")
    def tx_dropped_on(self, hub, name):
        agent, _, interface = name.partition("/")
        for row in hub.interfaces():
            if row.agent == agent and row.interface == interface:
                return row.tx_dropped
        return 0

    @keyword("Wait Until TX Dropped On ${hub} For ${name} Exceeds ${threshold}")
    def wait_tx_dropped_exceeds(self, hub, name, threshold, timeout=15):
        deadline = time.monotonic() + float(timeout)
        while time.monotonic() < deadline:
            if self.tx_dropped_on(hub, name) > int(threshold):
                return
            time.sleep(0.2)
        raise AssertionError(f"tx-dropped on {name} stayed <= {threshold}")

    @keyword("Wait Until TX Dropped On ${hub} For ${name} Settles")
    def wait_tx_dropped_settles(self, hub, name, timeout=25):
        deadline = time.monotonic() + float(timeout)
        while time.monotonic() < deadline:
            if self._tx_dropped_growth(hub, name) <= 15:
                return self.tx_dropped_on(hub, name)
        raise AssertionError(f"tx-dropped on {name} did not settle")

    @keyword("Wait Until TX Dropped On ${hub} For ${name} Stops Climbing")
    def wait_tx_dropped_stops_climbing(self, hub, name, timeout=30):
        deadline = time.monotonic() + float(timeout)
        peaked = False
        while time.monotonic() < deadline:
            climbed = self._tx_dropped_growth(hub, name) > 15
            peaked = peaked or climbed
            if peaked and not climbed:
                return self.tx_dropped_on(hub, name)
        raise AssertionError(f"tx-dropped on {name} kept climbing")

    def _tx_dropped_growth(self, hub, name):
        before = self.tx_dropped_on(hub, name)
        time.sleep(1.5)
        return self.tx_dropped_on(hub, name) - before

    @keyword("Flood ${interface} On ${server}")
    def flood_on(self, interface, server, gap_ms=0.05, count=120000, can_id="200"):
        return server.cangen(interface, float(gap_ms), int(count), can_id=can_id)

    @keyword("Start Draining ${channels} On ${server}")
    def start_draining(self, channels, server, seconds=10, host="127.0.0.1", port="29536"):
        names = channels if isinstance(channels, list) else [channels]
        return server.exec("python3", CONSUME_SCRIPT, host, str(port), str(seconds),
                           *names, background=True, log_name="consume")

    @keyword("Burst ${count} Frames Through Socketcand On ${server} ${channel}")
    def burst_through_socketcand(self, count, server, channel, hold=0.0, host="127.0.0.1", port="29536"):
        process = server.exec("python3", PRODUCE_SCRIPT, host, str(port), channel, str(count),
                              str(hold), background=True, log_name="produce")
        process.wait(timeout=float(hold) + 30)
        return json.loads(process.read_log().strip().splitlines()[-1])

    @keyword("Wait Until ${count} Channels Open On ${hub}")
    def wait_until_channels_open(self, count, hub, timeout=8):
        deadline = time.monotonic() + float(timeout)
        while time.monotonic() < deadline:
            if len([r for r in hub.clients() if r.channel != "-"]) >= int(count):
                return
            time.sleep(0.1)
        raise AssertionError(f"fewer than {count} channels opened on the hub")

    @keyword("Send ${count} Frames On ${server} ${interface}")
    def send_frames_on(self, count, server, interface, gap=0.1, can_id="123"):
        for i in range(int(count)):
            server.exec("cansend", interface, f"{can_id}#{i:016X}")
            time.sleep(float(gap))

    @keyword("Burst ${count} Frames On ${server} ${interface}")
    def burst_frames_on(self, count, server, interface):
        process = server.cangen(interface, 0, int(count))
        process.wait(timeout=30)
        return int(count)

    @keyword("Frames Captured By ${process}")
    def frames_captured_by(self, process):
        return len(parse_candump(process.read_log()))

    @keyword("Wait Until Frames Captured By ${process} Reaches ${count}")
    def wait_frames_captured(self, process, count, timeout=10):
        deadline = time.monotonic() + float(timeout)
        while time.monotonic() < deadline:
            if self.frames_captured_by(process) >= int(count):
                return
            time.sleep(0.2)

    @keyword("Channel Drops On ${hub}")
    def channel_drops_on(self, hub):
        return {r.interface: r.dropped for r in hub.clients() if r.channel != "-"}

    @keyword("Drain Result Of ${process}")
    def drain_result_of(self, process):
        process.wait(timeout=30)
        return json.loads(process.read_log().strip().splitlines()[-1])


VARIED_INTERFACE_PATTERN = [1, 2, 4, 1, 3]


def _varied_interface_count(index: int) -> int:
    """Interfaces for agent ${index}: a small repeating pattern so agents are
    heterogeneous (some single-bus, some multi-bus). Capped at REGISTER_INTERFACES_MAX."""
    return min(VARIED_INTERFACE_PATTERN[index % len(VARIED_INTERFACE_PATTERN)], 16)


def _aggregate(per_client: list[dict], agents: int, interfaces: int) -> dict:
    received = sum(metric["received"] for metric in per_client)
    weighted_latency = sum(metric["latency_avg_ms"] * metric["received"] for metric in per_client)
    return {
        "agents": agents,
        "clients": len(per_client),
        "interfaces": interfaces,
        "received": received,
        "lost": sum(metric["lost"] for metric in per_client),
        "out_of_order": sum(metric["out_of_order"] for metric in per_client),
        "rate_fps": sum(metric["rate_fps"] for metric in per_client),
        "latency_avg_ms": round(weighted_latency / received) if received else 0,
        "latency_p95_ms": max((metric["latency_p95_ms"] for metric in per_client), default=0),
        "jitter_ms": max((metric["jitter_ms"] for metric in per_client), default=0),
    }


def _normalise(kwargs: dict) -> dict:
    """Robot passes everything as strings; coerce the obvious booleans/lists."""
    out = {}
    for key, value in kwargs.items():
        if isinstance(value, str) and value.lower() in ("true", "false"):
            out[key] = value.lower() == "true"
        else:
            out[key] = value
    return out


def _canhub_library() -> str:
    return f"CANHUB_LIBRARY={BIN_DIR}/libcanhub.so"
