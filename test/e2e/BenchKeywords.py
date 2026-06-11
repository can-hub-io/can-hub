"""Robot keyword layer over the bench library.

Keywords return Can* instances so tests read like:

    ${hub}=     Start CAN HUB On ${LOCAL_SERVER} With ${config}
    ${agent}=   Start CAN Agent On ${LOCAL_SERVER} With ${config}
"""

import json
import time

from robot.api.deco import keyword, library

from lib import (
    AgentConfig, CanAgent, CanClient, CanCli, CanHub, ClientConfig, HubConfig,
)
from lib.rows import parse_candump

CONSUME_SCRIPT = "/work/test/e2e/scripts/consume.py"


@library(scope="GLOBAL")
class BenchKeywords:

    # ---------- bench lifecycle ----------

    @keyword("Setup Bench")
    def setup_bench(self, bench):
        bench.setup()

    @keyword("Teardown Bench")
    def teardown_bench(self, bench):
        bench.teardown()

    @keyword("Create VCAN On ${server}")
    def create_vcan_on(self, server, name="vcan0"):
        return server.make_vcan(name)

    # ---------- configuration builders ----------

    @keyword("Hub Configuration")
    def hub_configuration(self, **kwargs) -> HubConfig:
        return HubConfig(**_normalise(kwargs))

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

    @keyword("Flood ${interface} On ${server}")
    def flood_on(self, interface, server, gap_ms=0.05, count=120000, can_id="200"):
        return server.cangen(interface, float(gap_ms), int(count), can_id=can_id)

    @keyword("Start Draining ${channels} On ${server}")
    def start_draining(self, channels, server, seconds=10, host="127.0.0.1", port="29536"):
        names = channels if isinstance(channels, list) else [channels]
        return server.exec("python3", CONSUME_SCRIPT, host, str(port), str(seconds),
                           *names, background=True, log_name="consume")

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

    @keyword("Channel Drops On ${hub}")
    def channel_drops_on(self, hub):
        return {r.interface: r.dropped for r in hub.clients() if r.channel != "-"}

    @keyword("Drain Result Of ${process}")
    def drain_result_of(self, process):
        process.wait(timeout=30)
        return json.loads(process.read_log().strip().splitlines()[-1])


def _normalise(kwargs: dict) -> dict:
    """Robot passes everything as strings; coerce the obvious booleans/lists."""
    out = {}
    for key, value in kwargs.items():
        if isinstance(value, str) and value.lower() in ("true", "false"):
            out[key] = value.lower() == "true"
        else:
            out[key] = value
    return out
