*** Settings ***
Documentation     ISO-TP transaction steps end to end: python-can-hub flashes an
...               image in UDS TransferData blocks of 4 KiB, each one step the
...               agent segments and whose answer it reassembles, into an ECU
...               simulated on the kernel's own ISO-TP stack (isotp_ecu.py). The
...               two CRCs match only if every block arrived whole and in order.
Library           BenchKeywords
Variables         bench_variables.py
Suite Setup       Setup Bench ${BENCH} With The Python Package On ${LOCAL_SERVER}
Suite Teardown    Teardown Bench    ${BENCH}
Test Teardown     Reset Bench    ${BENCH}

*** Variables ***
${FLASH}          /work/test/e2e/scripts/isotp_flash.py
${ECU}            /work/test/e2e/scripts/isotp_ecu.py
${UDS_FLASH}      /work/test/e2e/scripts/uds_flash.py
${QUIC}           quic://127.0.0.1:7227
${IMAGE_CRC}      debda163

*** Test Cases ***
A Firmware Image Flashes Over ISO-TP Block By Block
    ${hub}=    Start CAN HUB Open To Writers On ${LOCAL_SERVER}
    ${client_state}=    Fresh State Directory For client
    Start CAN Agent truck42 On ${LOCAL_SERVER} Exporting vcan0 Into ${hub}
    ${ecu}=    Start Python ${ECU} On ${LOCAL_SERVER}    vcan0
    Log Of ${ecu} Should Contain ready

    ${result}=    Run Python ${FLASH} On ${LOCAL_SERVER}    ${QUIC}    truck42/vcan0    --size    20000    --state-dir    ${client_state}

    Should Be True    ${result.ok}    ${result.stdout}${result.stderr}
    Should Contain    ${result.stdout}    done steps=7 crc=${IMAGE_CRC}
    Log Of ${ecu} Should Contain image bytes=20000 crc=${IMAGE_CRC}

A Flash Runs Across The WAN Link
    ${hub}=    Start CAN HUB Open To Writers On ${LOCAL_SERVER}
    ${client_state}=    Fresh State Directory For client
    Start CAN Agent truck42 On ${WAN_SERVER} Exporting vcan0 Into ${hub}
    ${ecu}=    Start Python ${ECU} On ${WAN_SERVER}    vcan0
    Log Of ${ecu} Should Contain ready

    ${result}=    Run Python ${FLASH} On ${LOCAL_SERVER}    ${QUIC}    truck42/vcan0    --size    20000    --state-dir    ${client_state}

    Should Be True    ${result.ok}    ${result.stdout}${result.stderr}
    Log Of ${ecu} Should Contain image bytes=20000 crc=${IMAGE_CRC}

The ECU's Flow Control Paces The Transfer
    ${hub}=    Start CAN HUB Open To Writers On ${LOCAL_SERVER}
    ${client_state}=    Fresh State Directory For client
    Start CAN Agent truck42 On ${LOCAL_SERVER} Exporting vcan0 Into ${hub}
    ${ecu}=    Start Python ${ECU} On ${LOCAL_SERVER}    vcan0    --block-size    4    --st-min    1
    Log Of ${ecu} Should Contain ready

    ${result}=    Run Python ${FLASH} On ${LOCAL_SERVER}    ${QUIC}    truck42/vcan0    --size    20000    --state-dir    ${client_state}

    Should Be True    ${result.ok}    ${result.stdout}${result.stderr}
    Log Of ${ecu} Should Contain image bytes=20000 crc=${IMAGE_CRC}

A Response Pending Keeps The Block Waiting For Its Answer
    ${hub}=    Start CAN HUB Open To Writers On ${LOCAL_SERVER}
    ${client_state}=    Fresh State Directory For client
    Start CAN Agent truck42 On ${LOCAL_SERVER} Exporting vcan0 Into ${hub}
    ${ecu}=    Start Python ${ECU} On ${LOCAL_SERVER}    vcan0    --pending
    Log Of ${ecu} Should Contain ready

    ${result}=    Run Python ${FLASH} On ${LOCAL_SERVER}    ${QUIC}    truck42/vcan0    --size    20000    --state-dir    ${client_state}

    Should Be True    ${result.ok}    ${result.stdout}${result.stderr}
    Log Of ${ecu} Should Contain image bytes=20000 crc=${IMAGE_CRC}

Extended Addressing Puts The Addresses In Every Frame
    ${hub}=    Start CAN HUB Open To Writers On ${LOCAL_SERVER}
    ${client_state}=    Fresh State Directory For client
    Start CAN Agent truck42 On ${LOCAL_SERVER} Exporting vcan0 Into ${hub}
    ${ecu}=    Start Python ${ECU} On ${LOCAL_SERVER}    vcan0    --extended    0x10    0xF1
    Log Of ${ecu} Should Contain ready

    ${result}=    Run Python ${FLASH} On ${LOCAL_SERVER}    ${QUIC}    truck42/vcan0    --size    20000    --extended    0x10    0xF1    --state-dir    ${client_state}

    Should Be True    ${result.ok}    ${result.stdout}${result.stderr}
    Log Of ${ecu} Should Contain image bytes=20000 crc=${IMAGE_CRC}

A Flash Over CAN FD Moves 64-Byte Frames
    Create VCAN On ${LOCAL_SERVER}    vcan0
    Enable CAN FD On ${LOCAL_SERVER}    vcan0
    ${hub}=    Start CAN HUB Open To Writers On ${LOCAL_SERVER}
    ${client_state}=    Fresh State Directory For client
    Start CAN Agent truck42 On ${LOCAL_SERVER} Exporting vcan0 Into ${hub}
    ${ecu}=    Start Python ${ECU} On ${LOCAL_SERVER}    vcan0    --fd
    Log Of ${ecu} Should Contain ready
    ${bus}=    Start Candump On ${LOCAL_SERVER}    vcan0

    ${result}=    Run Python ${FLASH} On ${LOCAL_SERVER}    ${QUIC}    truck42/vcan0    --size    20000    --fd    --state-dir    ${client_state}

    Should Be True    ${result.ok}    ${result.stdout}${result.stderr}
    Log Of ${ecu} Should Contain image bytes=20000 crc=${IMAGE_CRC}
    ${full_frames}=    CAN FD Frames Of 64 Bytes With Id 7E0 Captured By ${bus}
    Should Be True    ${full_frames} > 300

A Refused Block Aborts The Flash At Its Step
    ${hub}=    Start CAN HUB Open To Writers On ${LOCAL_SERVER}
    ${client_state}=    Fresh State Directory For client
    Start CAN Agent truck42 On ${LOCAL_SERVER} Exporting vcan0 Into ${hub}
    ${ecu}=    Start Python ${ECU} On ${LOCAL_SERVER}    vcan0
    Log Of ${ecu} Should Contain ready

    ${result}=    Run Python ${FLASH} On ${LOCAL_SERVER}    ${QUIC}    truck42/vcan0    --size    20000    --skip-counter    2    --state-dir    ${client_state}

    Should Be Equal As Integers    ${result.returncode}    2
    Should Contain    ${result.stdout}    aborted reason=abort_match step=3

The UDS Helper Flashes An Image Without Writing A Template
    ${hub}=    Start CAN HUB Open To Writers On ${LOCAL_SERVER}
    ${client_state}=    Fresh State Directory For client
    Start CAN Agent truck42 On ${LOCAL_SERVER} Exporting vcan0 Into ${hub}
    ${ecu}=    Start Python ${ECU} On ${LOCAL_SERVER}    vcan0    --pending
    Log Of ${ecu} Should Contain ready

    ${result}=    Run Python ${UDS_FLASH} On ${LOCAL_SERVER}    ${QUIC}    truck42/vcan0    --size    20000    --state-dir    ${client_state}

    Should Be True    ${result.ok}    ${result.stdout}${result.stderr}
    Should Contain    ${result.stdout}    done steps=7 crc=${IMAGE_CRC}
    Log Of ${ecu} Should Contain image bytes=20000 crc=${IMAGE_CRC}

The UDS Helper Names The Refused Service And Its Code
    ${hub}=    Start CAN HUB Open To Writers On ${LOCAL_SERVER}
    ${client_state}=    Fresh State Directory For client
    Start CAN Agent truck42 On ${LOCAL_SERVER} Exporting vcan0 Into ${hub}
    ${ecu}=    Start Python ${ECU} On ${LOCAL_SERVER}    vcan0
    Log Of ${ecu} Should Contain ready

    ${result}=    Run Python ${UDS_FLASH} On ${LOCAL_SERVER}    ${QUIC}    truck42/vcan0    --size    20000    --skip-counter    2    --state-dir    ${client_state}

    Should Be Equal As Integers    ${result.returncode}    2
    Should Contain    ${result.stdout}    refused service=0x36 code=0x73 step=3
