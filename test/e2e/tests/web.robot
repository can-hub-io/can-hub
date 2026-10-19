*** Settings ***
Documentation     End-to-end for can-hub-web <-> can-hub: a real hub + agent on
...               LOCAL, the web daemon in front, driven over REST. Every body is
...               validated against web/openapi.yaml, so the suite asserts the
...               contract rather than a hand-copy of it.
Library           BenchKeywords
Library           WebKeywords
Library           Collections
Variables         bench_variables.py
Suite Setup       Setup Web Bench
Suite Teardown    Teardown Bench    ${BENCH}

*** Variables ***
${CONNECT}        tcp://127.0.0.1:7228
${ADMIN}          admin
${PASSWORD}       hunter2pass
${FINGERPRINT}    00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff

*** Test Cases ***
Unauthenticated Requests Are Rejected
    ${anonymous}=    New Unauthenticated Client For ${WEB}

    ${status}=    GET /api/status On ${anonymous}

    Status Of ${status} Should Be    401

Bootstrap Produced An Authenticated Admin
    ${auth_state}=    Auth State Of ${WEB}

    Status Of ${auth_state} Should Be    200
    Body Of ${auth_state} Should Match    AuthState
    Should Be True    ${auth_state.json}[authenticated]
    Should Be Equal    ${auth_state.json}[user]    ${ADMIN}

Repeated Setup Conflicts
    ${body}=    Create Dictionary    name=other    password=${PASSWORD}

    ${setup}=    POST /api/setup On ${WEB} With ${body}

    Status Of ${setup} Should Be    409

Bad Login Is Unauthorized
    ${anonymous}=    New Unauthenticated Client For ${WEB}

    ${login}=    Login ${anonymous} As    ${ADMIN}    wrongpassword

    Status Of ${login} Should Be    401

The Status View Matches The Schema
    ${status}=    GET /api/status On ${WEB}

    Status Of ${status} Should Be    200
    Body Of ${status} Should Match    Status

The Peers View Matches The Schema
    ${peers}=    GET /api/peers On ${WEB}

    Status Of ${peers} Should Be    200
    Each Item Of ${peers} Should Match    Peer

The Agents View Matches The Schema
    ${agents}=    GET /api/agents On ${WEB}

    Status Of ${agents} Should Be    200
    Each Item Of ${agents} Should Match    Agent

The Interfaces View Matches The Schema
    ${interfaces}=    GET /api/interfaces On ${WEB}

    Status Of ${interfaces} Should Be    200
    Each Item Of ${interfaces} Should Match    Interface

The Clients View Matches The Schema
    ${clients}=    GET /api/clients On ${WEB}

    Status Of ${clients} Should Be    200
    Each Item Of ${clients} Should Match    Client

Mutating Without CSRF Is Forbidden
    [Teardown]    Login ${WEB} As    ${ADMIN}    ${PASSWORD}
    Clear CSRF On ${WEB}
    ${body}=    Create Dictionary    name=nocsrf

    ${refused}=    POST /api/groups On ${WEB} With ${body}

    Status Of ${refused} Should Be    403

Bitrate Zero Is Rejected
    ${settings}=    Create Dictionary    bitrate=${0}
    ${body}=    Create Dictionary    agentName=truck42    interfaceName=vcan0    settings=${settings}

    ${configured}=    POST /api/interfaces/config On ${WEB} With ${body}

    Status Of ${configured} Should Be    400

The Settings Of A vcan Match The Schema
    ${settings}=    GET /api/interfaces/truck42/vcan0/settings On ${WEB}

    Status Of ${settings} Should Be    200
    Body Of ${settings} Should Match    InterfaceSettings
    Should Be True    ${settings.json}[up]

A TX Queue Length Set Over REST Is What The Bus Reports
    ${settings}=    Create Dictionary    txqueuelen=${500}
    ${body}=    Create Dictionary    agentName=truck42    interfaceName=vcan0    settings=${settings}

    ${configured}=    POST /api/interfaces/config On ${WEB} With ${body}

    Status Of ${configured} Should Be    200
    Body Of ${configured} Should Match    InterfaceSettings
    Should Be Equal As Integers    ${configured.json}[txqueuelen]    500

The Pins List Matches The Schema
    ${pins}=    GET /api/pins On ${WEB}

    Status Of ${pins} Should Be    200
    Each Item Of ${pins} Should Match    Pin

An Added Pin Is Listed
    ${body}=    Create Dictionary    agentName=van7    fingerprintHex=${FINGERPRINT}

    ${added}=    POST /api/pins On ${WEB} With ${body}
    ${pins}=    GET /api/pins On ${WEB}

    Status Of ${added} Should Be    200
    Body Of ${added} Should Match    ActionResult
    Should Not Be Empty    ${pins.json}

A Pin Can Be Deleted
    ${body}=    Create Dictionary    agentName=bus3    fingerprintHex=${FINGERPRINT}
    POST /api/pins On ${WEB} With ${body}

    ${deleted}=    DELETE /api/pins/bus3 On ${WEB}

    Status Of ${deleted} Should Be    200

An ACL Grant Is Listed
    ${grant}=    Create Dictionary    fingerprintHex=*    agentName=truck42    interfaceName=vcan0    level=r

    ${granted}=    POST /api/acls On ${WEB} With ${grant}
    ${acls}=    GET /api/acls On ${WEB}

    Status Of ${granted} Should Be    200
    Status Of ${acls} Should Be    200
    Each Item Of ${acls} Should Match    Acl

An ACL Grant Can Be Revoked
    ${grant}=    Create Dictionary    fingerprintHex=*    agentName=truck42    interfaceName=can9    level=r
    ${revoke}=    Create Dictionary    fingerprintHex=*    agentName=truck42    interfaceName=can9
    POST /api/acls On ${WEB} With ${grant}

    ${revoked}=    POST /api/acls/revoke On ${WEB} With ${revoke}

    Status Of ${revoked} Should Be    200

A Bridge Route Is Listed With Its Filters
    ${route}=    Set Variable    ${{ {"source": {"agentName": "truck42", "interfaceName": "vcan0"}, "destination": {"agentName": "lab", "interfaceName": "vcan0"}, "filters": [{"canId": 256, "canMask": 1792}]} }}

    ${added}=    POST /api/bridges On ${WEB} With ${route}
    ${bridges}=    GET /api/bridges On ${WEB}

    Status Of ${added} Should Be    200
    Status Of ${bridges} Should Be    200
    Each Item Of ${bridges} Should Match    Bridge
    Should Be Equal As Integers    ${bridges.json}[0][filters][0][canMask]    1792

A Bridge Route Into Its Own Bus Is A Bad Request
    ${route}=    Set Variable    ${{ {"source": {"agentName": "truck42", "interfaceName": "vcan0"}, "destination": {"agentName": "truck42", "interfaceName": "vcan0"}} }}

    ${refused}=    POST /api/bridges On ${WEB} With ${route}

    Status Of ${refused} Should Be    400

A Bridge Route Can Be Deleted Once
    ${route}=    Set Variable    ${{ {"source": {"agentName": "truck42", "interfaceName": "vcan0"}, "destination": {"agentName": "bench", "interfaceName": "can9"}} }}
    POST /api/bridges On ${WEB} With ${route}

    ${deleted}=    POST /api/bridges/delete On ${WEB} With ${route}
    ${deleted_again}=    POST /api/bridges/delete On ${WEB} With ${route}

    Status Of ${deleted} Should Be    200
    Status Of ${deleted_again} Should Be    404

A Bridge Group Is Listed With Its Members
    ${group}=    Set Variable    ${{ {"name": "fleet", "filters": [{"canId": 256, "canMask": 1792}]} }}
    ${member}=    Set Variable    ${{ {"name": "fleet", "member": {"agentName": "truck42", "interfaceName": "vcan0"}} }}
    POST /api/bridge-groups On ${WEB} With ${group}

    ${joined}=    POST /api/bridge-groups/members On ${WEB} With ${member}
    ${groups}=    GET /api/bridge-groups On ${WEB}

    Status Of ${joined} Should Be    200
    Each Item Of ${groups} Should Match    BridgeGroup
    Should Be Equal    ${groups.json}[0][members][0][agentName]    truck42
    Should Be True    ${groups.json}[0][members][0][online]

A Bridge Group With A Bad Name Is A Bad Request
    ${group}=    Set Variable    ${{ {"name": "my fleet"} }}

    ${refused}=    POST /api/bridge-groups On ${WEB} With ${group}

    Status Of ${refused} Should Be    400

A Bridge Group Can Be Deleted Once
    ${group}=    Set Variable    ${{ {"name": "spare"} }}
    POST /api/bridge-groups On ${WEB} With ${group}

    ${deleted}=    POST /api/bridge-groups/delete On ${WEB} With ${group}
    ${deleted_again}=    POST /api/bridge-groups/delete On ${WEB} With ${group}

    Status Of ${deleted} Should Be    200
    Status Of ${deleted_again} Should Be    404

Kick By Peer Id
    ${peers}=    GET /api/peers On ${WEB}
    ${peer}=    Get From List    ${peers.json}    0

    ${kicked}=    POST /api/peers/${peer}[peerId]/kick On ${WEB}

    Status Of ${kicked} Should Be    200

A Group Is Created And Listed
    ${body}=    Create Dictionary    name=operators

    ${created}=    POST /api/groups On ${WEB} With ${body}
    ${groups}=    GET /api/groups On ${WEB}

    Status Of ${created} Should Be    200
    Status Of ${groups} Should Be    200
    Each Item Of ${groups} Should Match    ManagedGroup

A Group Gets Its Permissions
    ${body}=    Create Dictionary    name=auditors
    POST /api/groups On ${WEB} With ${body}
    ${groups}=    GET /api/groups On ${WEB}
    ${group_id}=    Id Of group auditors In ${groups}
    ${permissions}=    Create List    views.read    peers.kick
    ${permissions_body}=    Create Dictionary    permissions=${permissions}

    ${updated}=    PUT /api/groups/${group_id}/permissions On ${WEB} With ${permissions_body}

    Status Of ${updated} Should Be    200

A User Is Created And Listed
    ${body}=    Create Dictionary    name=ops    password=${PASSWORD}

    ${created}=    POST /api/users On ${WEB} With ${body}
    ${users}=    GET /api/users On ${WEB}

    Status Of ${created} Should Be    200
    Status Of ${users} Should Be    200
    Each Item Of ${users} Should Match    ManagedUser

A User Joins A Group
    ${group_body}=    Create Dictionary    name=night-shift
    ${user_body}=    Create Dictionary    name=night    password=${PASSWORD}
    POST /api/groups On ${WEB} With ${group_body}
    POST /api/users On ${WEB} With ${user_body}
    ${groups}=    GET /api/groups On ${WEB}
    ${users}=    GET /api/users On ${WEB}
    ${group_id}=    Id Of group night-shift In ${groups}
    ${user_id}=    Id Of user night In ${users}
    ${membership}=    Create Dictionary    groupId=${group_id}

    ${joined}=    POST /api/users/${user_id}/groups On ${WEB} With ${membership}

    Status Of ${joined} Should Be    200

An Admin Resets A User Password
    ${user_body}=    Create Dictionary    name=forgetful    password=${PASSWORD}
    POST /api/users On ${WEB} With ${user_body}
    ${users}=    GET /api/users On ${WEB}
    ${user_id}=    Id Of user forgetful In ${users}
    ${reset_body}=    Create Dictionary    password=anotherpass

    ${reset}=    POST /api/users/${user_id}/password On ${WEB} With ${reset_body}

    Status Of ${reset} Should Be    200

A User Can Be Deleted
    ${user_body}=    Create Dictionary    name=leaving    password=${PASSWORD}
    POST /api/users On ${WEB} With ${user_body}
    ${users}=    GET /api/users On ${WEB}
    ${user_id}=    Id Of user leaving In ${users}

    ${deleted}=    DELETE /api/users/${user_id} On ${WEB}

    Status Of ${deleted} Should Be    200

A Group Can Be Deleted
    ${body}=    Create Dictionary    name=disbanded
    POST /api/groups On ${WEB} With ${body}
    ${groups}=    GET /api/groups On ${WEB}
    ${group_id}=    Id Of group disbanded In ${groups}

    ${deleted}=    DELETE /api/groups/${group_id} On ${WEB}

    Status Of ${deleted} Should Be    200

Cannot Delete The Last Admin
    ${users}=    GET /api/users On ${WEB}
    ${admin_id}=    Id Of user ${ADMIN} In ${users}

    ${deleted}=    DELETE /api/users/${admin_id} On ${WEB}

    Status Of ${deleted} Should Be    409

Self Service Password Change
    [Teardown]    Change Password Of ${WEB} From changedpass To ${PASSWORD}
    ${body}=    Create Dictionary    currentPassword=${PASSWORD}    newPassword=changedpass

    ${changed}=    POST /api/auth/password On ${WEB} With ${body}

    Status Of ${changed} Should Be    200

Audit Log Records Authentication
    ${audit}=    GET /api/audit On ${WEB}

    Status Of ${audit} Should Be    200
    Each Item Of ${audit} Should Match    AuditEntry
    Should Not Be Empty    ${audit.json}

*** Keywords ***
Setup Web Bench
    Setup Bench    ${BENCH}
    Create VCAN On ${LOCAL_SERVER}    vcan0
    Reset Hub State On ${LOCAL_SERVER}
    ${hub_cfg}=    Hub Configuration
    ${hub}=    Start CAN HUB On ${LOCAL_SERVER} With ${hub_cfg}
    Set Suite Variable    ${HUB}    ${hub}
    ${agent_cfg}=    Agent Configuration    ${CONNECT}    truck42    vcan0
    ${agent}=    Start CAN Agent On ${LOCAL_SERVER} With ${agent_cfg}
    Wait Until Agent ${agent} Registered On ${hub}
    ${web}=    Start CAN HUB Web On ${LOCAL_SERVER} Against ${hub}
    Set Suite Variable    ${WEB}    ${web}
    ${bootstrap}=    Bootstrap Admin ${web}    ${ADMIN}    ${PASSWORD}
    Status Of ${bootstrap} Should Be    200
