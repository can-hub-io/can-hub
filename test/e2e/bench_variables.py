"""Robot variable file: one Bench, its Servers exported as variables.

Access from Robot with the extended syntax, e.g. ${LOCAL_SERVER.fqdn} or
${WAN_SERVER.latency_ms}. The network itself is built by `Setup Bench`
(Suite Setup), not at import.
"""

from lib import Bench

_bench = Bench()


def get_variables():
    variables = {"BENCH": _bench}
    for name, server in _bench.servers.items():
        variables[f"{name.upper().rstrip('1')}_SERVER"] = server
    return variables
