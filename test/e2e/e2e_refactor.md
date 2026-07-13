# Re-arquitectura del keyword-layer e2e (por entidad) + suites burst

> Doc interno, untracked. Trabajo aplazado, fuera del scope actual.

## Context

Al reproducir en e2e el bug "solo recibe 1" (ráfaga de escritura socketcand a un
bus rate-limited → ~2/400, `--pace-rate` no ayuda), las dos suites burst que se
escribieron metían lógica de test en `*** Keywords ***` del Robot y habrían
necesitado un contador con estado oculto. Eso choca con la arquitectura deseada:

- **Regla 1**: en `*** Keywords ***` de un `.robot` solo Setup/Teardown. Cero
  lógica de negocio del test.
- **Arquitectura por entidad**: cada entidad = su módulo/clase modelo en `lib/`
  con su estado. Las *keyword classes* son glue-code fino: traducen a lenguaje
  humano y operan sobre instancias del modelo. `Start ...` devuelve `${entidad}`;
  las demás keywords reciben esa instancia explícita (instancias, simétrico —
  hub y cli incluidos).
- Hoy todo el keyword-layer es un monolito `BenchKeywords.py`. Hay que partirlo.

Decisiones tomadas: **migración total** (todas las suites), **instancias
simétricas**, y los casos burst grandes **caracterizan el estado actual roto**
(suite verde, no rojo).

## Approach

### Capa modelo (`lib/`) — una clase por entidad
Existen y se conservan (sin renombrar): `Bench`, `Server`, `CanHub`, `CanAgent`,
`CanClient`, `CanCli`, `CanHubWeb`, `Process`, `rows`, `configuration`.

Cambios/altas en modelo:
- `CanCli` (lib/can_cli.py): de estático a **instancia ligada a un hub**
  (`CanCli(hub)`, `.run(*args)`), para `${cli}= Start CLI On ${hub}`.
- **NEW** `lib/can_bus.py` `CanBus`: un vcan en un Server. `create`, `shape_sink`,
  `limit_egress`, `inject(frame)` (cansend), `flood`, `burst(count)` (cangen).
- **NEW** `lib/can_capture.py` `CanCapture`: envuelve un candump. `count()`,
  `frames()`, `wait_reaches(n)`, `settles()`, `captured(can_id, data)`.
- **NEW** `lib/socketcand_consumer.py` `SocketcandConsumer`: envuelve consume.py.
  `start(...)`, `results()` → dict de cuentas por canal.
- **NEW** `lib/socketcand_producer.py` `SocketcandProducer`: envuelve produce.py.
  `burst(count)` → enviados.
- `scripts/produce.py` ya creado.

### Capa keyword — una `*Keywords.py` por entidad (glue fino, sin estado oculto)
`Start ...` construye el modelo y devuelve la instancia; el resto recibe la
instancia por `${var}`. La config se pliega en los `Start` como args con nombre
(se eliminan los keywords `Hub/Agent/Client Configuration` sueltos).

| Keyword lib | Keywords (representativos) |
|---|---|
| `BenchKeywords` (slim) | Setup/Teardown/Reset Bench |
| `CanBusKeywords` | `Create CAN Bus ${name} On ${server}`→`${bus}`; `Shape Sink ${bus} To ${rate}`; `Limit Egress On ${server} To ${rate}`; `Inject ${id}#${data} On ${bus}`; `Flood ${bus}`; `Burst ${n} Frames On ${bus}` |
| `CanCaptureKeywords` | `Start Capture On ${bus}`→`${cap}`; `Frames Captured By ${cap}`; `Wait Until ${cap} Reaches ${n}`; `Wait Until ${cap} Settles`; `${cap} Should Capture ${id}#${data}` |
| `CanHubKeywords` | `Start CAN HUB On ${server}`→`${hub}` (kwargs listen/cert/...); `Wait Until ${n} Channels Open On ${hub}`; `TX Dropped On ${hub} For ${name}`; `Wait ... TX Dropped ... Exceeds/Settles/Stops Climbing`; `Channel Drops On ${hub}`; `Reset Hub State On ${server}` |
| `CanAgentKeywords` | `Start CAN Agent ${name} On ${server}` (kwargs connect/interfaces/pace_rate/extra)→`${agent}`; `Wait Until Agent ${agent} Registered On ${hub}` |
| `CanHubClientKeywords` | `Start CAN Client ${command} On ${server}`→`${client}` (dump/attach/socketcand kwargs); `Wait Until Client ${client} Has Open Channel`; `Client ${client} Should Receive/Not Receive ${id}#${data}`; `List Interfaces On ${server}`; `Send Frame ${target} ${frame} From ${server}` |
| `CanCliKeywords` | `Start CLI On ${hub}`→`${cli}`; `Run CLI ${cli} ${args}` |
| `SocketcandConsumerKeywords` | `Start Draining ${channels} On ${server}`→`${consumer}`; `Drain Result Of ${consumer}` |
| `SocketcandProducerKeywords` | `Start Socketcand Producer On ${server} For ${channel}`→`${producer}`; `Burst ${n} Frames Through ${producer}` |
| `PyCanKeywords` | `Install Python Package On ${server}`; `Run/Start Probe On ${server}`; `Fingerprint Of ${path} On ${server}` |
| `ProcessKeywords` | `Run/Start Binary ${name} On ${server}`; `Log Of ${process} Should Contain ${text}` |
| `WebKeywords` (existe) | sin cambios de API; importa libs nuevas donde haga falta |

Sin estado oculto: las keyword classes no guardan "el hub actual" ni contadores;
el estado vive en las instancias modelo que viajan por variables Robot.

### Suites: todas declarativas
Cada `.robot` importa solo las keyword libs que usa (varias `Library`). El cuerpo
del test (o un keyword de Suite Setup, permitido) orquesta con keywords de
entidad — nunca lógica de test en `*** Keywords ***`.

Burst (caracterización **verde**, nombres de agente explícitos por servidor, sin
contador):
- `socketcand_burst.robot` (RX): local y WAN → reciben 16/16.
- `socketcand_write_burst.robot` (write a bus shaped): 16→16; 400 unpaced→shed
  (assert `delivered < 10`); 400 paced→shed (assert `delivered < 10`); 400 attach
  paced→`delivered > 100` (el pacing del cliente engancha; el bypass del bridge no).

### Sin sleeps fijos (preferencia del proyecto)
Sustituir `Sleep` por polls acotados: readiness del bridge (poll hasta peer
presente en `${hub}` `clients/peers`), y delivered con `Wait Until ${cap} Settles`
(patrón de `_tx_dropped_growth`). Migrar también los `Sleep` heredados.

## Ficheros

NEW lib: `can_bus.py`, `can_capture.py`, `socketcand_consumer.py`,
`socketcand_producer.py`. MOD lib: `can_cli.py` (instancia).
NEW keyword libs: `CanBusKeywords.py`, `CanCaptureKeywords.py`,
`CanHubKeywords.py`, `CanAgentKeywords.py`, `CanHubClientKeywords.py`,
`CanCliKeywords.py`, `SocketcandConsumerKeywords.py`,
`SocketcandProducerKeywords.py`, `PyCanKeywords.py`, `ProcessKeywords.py`.
MOD: `BenchKeywords.py` (queda slim: lifecycle).
MOD suites: `smoke`, `client`, `python_can`, `libcanhub`, `fairness`,
`socketcand`, `backpressure`, `web`, `socketcand_burst`, `socketcand_write_burst`.

## Verification

`make e2e` corre todas las suites. Objetivo: **todas verdes**, heredadas (sin
regresión) y las dos burst caracterizando el bug. Validar suite a suite,
empezando por `smoke` para fijar el patrón de imports/instancias, y `make e2e`
completo al final.

## Estado actual del trabajo burst (antes del refactor)

Ya en el árbol (sin commitear), reproduce el bug pero con el anti-patrón a
corregir:
- `scripts/produce.py` — productor socketcand (reutilizable, se queda).
- `BenchKeywords.py` — añadido `PRODUCE_SCRIPT` + keyword `Burst N Frames
  Through Socketcand`.
- `tests/socketcand_burst.robot`, `tests/socketcand_write_burst.robot` — con
  lógica en `*** Keywords ***` (a migrar).

Resultados medidos (bus 20kbit, burst 400): socketcand 2/400; attach paced
120/400; 16-frame 16/16. agent tx_dropped=0 (el shed lo hace el qdisc/tbf sin
ENOBUFS, invisible a los contadores).

## Notas
- No se renombran clases modelo existentes (`CanClient` se queda; el binario es
  can-hub-client). El split es del keyword-layer.
- Cambio amplio (~25 ficheros) pero mecánico; riesgo de regresión en suites
  heredadas, mitigado corriéndolas una a una.
