*** Settings ***
Documentation     Transactions against lockstep (#94): the same 50 UDS requests to
...               an ECU (ecu_sim.py uds), once run by the agent as a transaction
...               and once sent by the client one by one, each waiting for its
...               answer, as a tester without transactions does. The agent and the
...               ECU sit behind a link of the given round trip; the client and the
...               hub do not. Results go to the console:
...               txn-bench rtt=<ms> mode=<mode> seconds=<s> per_request_ms=<ms>
Library           BenchKeywords
Variables         bench_variables.py
Suite Setup       Setup Bench ${BENCH} With The Python Package On ${LOCAL_SERVER}
Suite Teardown    Teardown Bench    ${BENCH}
Test Teardown     Reset Bench    ${BENCH}
Test Template     Time Requests Against The ECU

*** Test Cases ***            MODE           ROUND_TRIP_MS    NEAR               WAN
transaction rtt 0             transaction    0    ${LOCAL_SERVER}    ${WAN_SERVER}
lockstep rtt 0                lockstep       0    ${LOCAL_SERVER}    ${WAN_SERVER}
transaction rtt 50            transaction    50    ${LOCAL_SERVER}    ${WAN_SERVER}
lockstep rtt 50               lockstep       50    ${LOCAL_SERVER}    ${WAN_SERVER}
transaction rtt 150           transaction    150    ${LOCAL_SERVER}    ${WAN_SERVER}
lockstep rtt 150              lockstep       150    ${LOCAL_SERVER}    ${WAN_SERVER}
transaction rtt 300           transaction    300    ${LOCAL_SERVER}    ${WAN_SERVER}
lockstep rtt 300              lockstep       300    ${LOCAL_SERVER}    ${WAN_SERVER}
