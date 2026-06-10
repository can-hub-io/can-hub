"""Robot keyword layer over the bench library.

Keywords return Can* instances so tests read like:

    ${hub}=     Start CAN HUB On ${LOCAL_SERVER} With ${config}
    ${agent}=   Start CAN Agent On ${LOCAL_SERVER} With ${config}
"""

import time

from robot.api.deco import keyword, library

from lib import (
    AgentConfig, CanAgent, CanClient, CanCli, CanHub, ClientConfig, HubConfig,
)


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


def _normalise(kwargs: dict) -> dict:
    """Robot passes everything as strings; coerce the obvious booleans/lists."""
    out = {}
    for key, value in kwargs.items():
        if isinstance(value, str) and value.lower() in ("true", "false"):
            out[key] = value.lower() == "true"
        else:
            out[key] = value
    return out
