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
    ${anon}=    New Unauthenticated Client For ${WEB}
    ${r}=    GET /api/status On ${anon}
    Status Of ${r} Should Be    401

Bootstrap Produced An Authenticated Admin
    ${r}=    Auth State Of ${WEB}
    Status Of ${r} Should Be    200
    Body Of ${r} Should Match    AuthState
    Should Be True    ${r.json}[authenticated]
    Should Be Equal    ${r.json}[user]    ${ADMIN}

Repeated Setup Conflicts
    ${body}=    Create Dictionary    name=other    password=${PASSWORD}
    ${r}=    POST /api/setup On ${WEB} With ${body}
    Status Of ${r} Should Be    409

Bad Login Is Unauthorized
    ${anon}=    New Unauthenticated Client For ${WEB}
    ${r}=    Login ${anon} As    ${ADMIN}    wrongpassword
    Status Of ${r} Should Be    401

Read Views Match The Schema
    ${status}=    GET /api/status On ${WEB}
    Status Of ${status} Should Be    200
    Body Of ${status} Should Match    Status

    ${peers}=    GET /api/peers On ${WEB}
    Status Of ${peers} Should Be    200
    Each Item Of ${peers} Should Match    Peer

    ${agents}=    GET /api/agents On ${WEB}
    Status Of ${agents} Should Be    200
    Each Item Of ${agents} Should Match    Agent

    ${interfaces}=    GET /api/interfaces On ${WEB}
    Status Of ${interfaces} Should Be    200
    Each Item Of ${interfaces} Should Match    Interface

    ${clients}=    GET /api/clients On ${WEB}
    Status Of ${clients} Should Be    200
    Each Item Of ${clients} Should Match    Client

Mutating Without CSRF Is Forbidden
    Clear CSRF On ${WEB}
    ${body}=    Create Dictionary    name=nocsrf
    ${r}=    POST /api/groups On ${WEB} With ${body}
    Status Of ${r} Should Be    403
    Login ${WEB} As    ${ADMIN}    ${PASSWORD}

Bitrate Zero Is Rejected
    ${body}=    Create Dictionary    agentName=truck42    interfaceName=vcan0    op=bitrate    bitrate=${0}
    ${r}=    POST /api/interfaces/config On ${WEB} With ${body}
    Status Of ${r} Should Be    400

Pins Lifecycle
    ${list}=    GET /api/pins On ${WEB}
    Status Of ${list} Should Be    200
    Each Item Of ${list} Should Match    Pin

    ${body}=    Create Dictionary    agentName=truck42    fingerprintHex=${FINGERPRINT}
    ${add}=    POST /api/pins On ${WEB} With ${body}
    Status Of ${add} Should Be    200
    Body Of ${add} Should Match    ActionResult

    ${after}=    GET /api/pins On ${WEB}
    Should Not Be Empty    ${after.json}

    ${delete}=    DELETE /api/pins/truck42 On ${WEB}
    Status Of ${delete} Should Be    200

ACL Grant And Revoke
    ${grant}=    Create Dictionary    fingerprintHex=*    agentName=truck42    interfaceName=vcan0    level=ro
    ${set}=    POST /api/acls On ${WEB} With ${grant}
    Status Of ${set} Should Be    200

    ${acls}=    GET /api/acls On ${WEB}
    Status Of ${acls} Should Be    200
    Each Item Of ${acls} Should Match    Acl

    ${revoke}=    Create Dictionary    fingerprintHex=*    agentName=truck42    interfaceName=vcan0
    ${r}=    POST /api/acls/revoke On ${WEB} With ${revoke}
    Status Of ${r} Should Be    200

Kick By Peer Id
    ${peers}=    GET /api/peers On ${WEB}
    ${peer}=    Get From List    ${peers.json}    0
    ${r}=    POST /api/peers/${peer}[peerId]/kick On ${WEB}
    Status Of ${r} Should Be    200

User And Group Management
    ${group_body}=    Create Dictionary    name=operators
    ${group}=    POST /api/groups On ${WEB} With ${group_body}
    Status Of ${group} Should Be    200

    ${groups}=    GET /api/groups On ${WEB}
    Status Of ${groups} Should Be    200
    Each Item Of ${groups} Should Match    ManagedGroup
    ${operators}=    Find Group    ${groups.json}    operators

    ${perms}=    Create List    views.read    peers.kick
    ${perm_body}=    Create Dictionary    permissions=${perms}
    ${set_perms}=    PUT /api/groups/${operators}[id]/permissions On ${WEB} With ${perm_body}
    Status Of ${set_perms} Should Be    200

    ${user_body}=    Create Dictionary    name=ops    password=${PASSWORD}
    ${user}=    POST /api/users On ${WEB} With ${user_body}
    Status Of ${user} Should Be    200

    ${users}=    GET /api/users On ${WEB}
    Status Of ${users} Should Be    200
    Each Item Of ${users} Should Match    ManagedUser
    ${ops}=    Find User    ${users.json}    ops

    ${add_member}=    Create Dictionary    groupId=${operators}[id]
    ${member}=    POST /api/users/${ops}[id]/groups On ${WEB} With ${add_member}
    Status Of ${member} Should Be    200

    ${reset_body}=    Create Dictionary    password=anotherpass
    ${reset}=    POST /api/users/${ops}[id]/password On ${WEB} With ${reset_body}
    Status Of ${reset} Should Be    200

    ${delete_user}=    DELETE /api/users/${ops}[id] On ${WEB}
    Status Of ${delete_user} Should Be    200
    ${delete_group}=    DELETE /api/groups/${operators}[id] On ${WEB}
    Status Of ${delete_group} Should Be    200

Cannot Delete The Last Admin
    ${users}=    GET /api/users On ${WEB}
    ${me}=    Find User    ${users.json}    ${ADMIN}
    ${r}=    DELETE /api/users/${me}[id] On ${WEB}
    Status Of ${r} Should Be    409

Self Service Password Change
    ${body}=    Create Dictionary    currentPassword=${PASSWORD}    newPassword=changedpass
    ${r}=    POST /api/auth/password On ${WEB} With ${body}
    Status Of ${r} Should Be    200
    ${revert}=    Create Dictionary    currentPassword=changedpass    newPassword=${PASSWORD}
    ${back}=    POST /api/auth/password On ${WEB} With ${revert}
    Status Of ${back} Should Be    200

Audit Log Records Authentication
    ${r}=    GET /api/audit On ${WEB}
    Status Of ${r} Should Be    200
    Each Item Of ${r} Should Match    AuditEntry
    Should Not Be Empty    ${r.json}

*** Keywords ***
Setup Web Bench
    Setup Bench    ${BENCH}
    Create VCAN On ${LOCAL_SERVER}    vcan0
    ${hub_cfg}=    Hub Configuration
    ${hub}=    Start CAN HUB On ${LOCAL_SERVER} With ${hub_cfg}
    Set Suite Variable    ${HUB}    ${hub}
    ${agent_cfg}=    Agent Configuration    ${CONNECT}    truck42    vcan0
    ${agent}=    Start CAN Agent On ${LOCAL_SERVER} With ${agent_cfg}
    Wait Until Agent ${agent} Registered On ${hub}
    ${web}=    Start CAN HUB Web On ${LOCAL_SERVER} Against ${hub}
    Set Suite Variable    ${WEB}    ${web}
    ${r}=    Bootstrap Admin ${web}    ${ADMIN}    ${PASSWORD}
    Status Of ${r} Should Be    200

Find Group
    [Arguments]    ${groups}    ${name}
    FOR    ${group}    IN    @{groups}
        Return From Keyword If    '${group}[name]' == '${name}'    ${group}
    END
    Fail    group ${name} not found

Find User
    [Arguments]    ${users}    ${name}
    FOR    ${user}    IN    @{users}
        Return From Keyword If    '${user}[name]' == '${name}'    ${user}
    END
    Fail    user ${name} not found
