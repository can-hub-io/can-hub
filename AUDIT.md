# can-hub — Auditoría técnica

Fecha: 2026-07-04 · Versión auditada: 0.2.7 (`CMakeLists.txt:2`)

Análisis del proyecto completo (hub, agent, client, protocolo, plataforma linux/windows,
web daemon, python, tests, CI, build, packaging, docs) más las 37 issues abiertas.
Este documento recoge **problemas** (diseño, arquitectura, implementación, bugs, seguridad,
deuda técnica) y una **lista de tareas** para subsanarlos.

Los hallazgos ya cubiertos por una issue abierta se marcan con `#NN`. El resto es nuevo.

> Alcance de la revisión: lectura estática completa. No se ejecutó fuzzing ni PoC. Las
> severidades reflejan impacto potencial; varios ítems `high` son contract holes latentes
> (no explotables con los llamantes actuales) y se anotan como tales.

---

## Resumen ejecutivo

Base de código madura y bien estructurada (ports & adapters, core freestanding, codecs
overlay bounds-checked). Los problemas graves se concentran en cuatro clusters:

1. **Confianza en localhost es plana.** El listener `tcp://127.0.0.1:7228` está **activo por
   defecto**, no lleva identidad y da lectura + inyección total en todos los buses. El socket
   unix admin (`/run/can-hub/hub.sock`) no fija permisos → cualquier usuario local con umask
   permisivo llega a admin (kick, ACLs, pins, IFCONFIG). Ver SEC-1, SEC-2.
2. **DoS no autenticado.** Tabla global de 64 peers sin cap por IP; QUIC acepta un slot+sesión
   TLS por Initial sin Retry/token; TCP/TLS half-open no se recolectan. Ver SEC-4.
3. **Suministro y arranque.** ngtcp2 fijado por tag git mutable (único dep sin hash); deb del
   hub arranca listener QUIC 0.0.0.0 sin opt-in; runner de tests binario opaco sin procedencia.
   Ver SEC-10, PKG-1, CI-8.
4. **"Features a medias".** Canales reliable (#147) no existen en el libcanhub de Windows pero
   la doc los promete universales; contrato de timeouts/errores inconsistente en C y Python;
   `libcanhub.so` reexporta ~2000 símbolos de OpenSSL estático. Ver API-1, API-3, API-4.

Además, **deriva doc/impl del protocolo** (ADMIN_INTERFACES_REPLY 168 vs 160 documentado,
REGISTER status 3 sin documentar, LIST "filtrable" que no lo es) que hay que corregir **antes**
de congelar v1 (#18).

Conteo de hallazgos: **~90** (críticos/altos concentrados en seguridad y API pública).

---

## 1. Seguridad

### SEC-1 · Modelo de confianza plano en localhost (tcp por defecto) — **HIGH**
`hub_main.c:222` (default `tcp://127.0.0.1:7228`), `broker.c:1392,1409`
El listener plain-tcp está activo por defecto. Un peer sin fingerprint tiene `fingerprint_hex`
vacío, y `clientCanRead`/`clientCanWrite` cortocircuitan a `true`. Cualquier proceso local
(o cualquier proceso en un contenedor/host multiusuario) lee todo el tráfico CAN de todos los
agentes e inyecta frames arbitrarios en cualquier bus. Documentado en `doc/security.md`, pero
"on by default" convierte localhost en zona de confianza plana.

### SEC-2 · Socket unix admin sin permisos ni SO_PEERCRED — **HIGH**
`tcp_server_transport.c:72-101` (init unix), `hub_main.c:330` (`mkdir(...,0755)`)
El socket `/run/can-hub/hub.sock` se crea sin `fchmod`/`chmod` y sin comprobación de
`SO_PEERCRED`; el control de acceso depende solo del umask del proceso. El directorio padre es
`0755`. Con umask permisivo (systemd `UMask=0000`, o umask 002) el socket queda world-connectable.
Un peer local es tratado como confiable → `HELLO role=admin` aceptado (es `local`) → familia
admin completa: kick de cualquier peer/agente, reescritura de ACLs, alta/baja de pins,
reconfiguración de interfaces (IFCONFIG bitrate/up/down). `doc/design.md:76` afirma "permisos de
fichero como control de acceso" pero el código nunca los fija.

### SEC-3 · TOCTOU en escritura de clave privada — **MEDIUM**
`tls_identity.c:238-259` (`writePemWithMode`); análogo en `windows/shared/tls_identity.c`
Las claves ED25519 se escriben con `fopen(path,"w")` (modo umask, típ. 0644) y el `chmod 0600`
se aplica **después** de escribir los bytes. Ventana en la que `hub.key`/`agent.key`/`client.key`
son world-readable; un fd abierto durante la ventana sobrevive al chmod. Un atacante local con
inotify roba la identidad TLS → suplanta el fingerprint y hereda sus pins/ACLs. Fix:
`open(O_WRONLY|O_CREAT|O_EXCL,0600)` o `fchmod` antes de escribir. Patrón similar (dato público,
menor impacto) en `pin_store.c:42-53`.

### SEC-4 · DoS no autenticado: agotamiento de slots — **MEDIUM**
`peer_directory.h:9` (`PEER_DIRECTORY_MAX 64`), `quic_server_transport.c:357-413` (`acceptPeer`),
paths accept de tcp/tls
(a) Tabla global de 64 peers sin cap por IP/subred; 64 conexiones en cualquier transporte
bloquean todo peer nuevo hub-wide. (b) QUIC asigna slot+sesión TLS por Initial válido sin
Retry/token de validación de dirección (`recv_retry` solo lado cliente) → flood de Initials
spoofed fija los 64 slots QUIC hasta `handshake_timeout`. (c) TCP/TLS half-open no se recolectan:
un socket que conecta pero nunca completa TLS o nunca envía HELLO ocupa slot indefinidamente
(los half-open QUIC sí se limpian por idle 6s/handshake 10s). El anti-amplificación 3× de
ngtcp2 evita reflexión, pero el agotamiento de slots/tabla persiste.

### SEC-5 · Squatting de nombre de agente en TOFU (default) — **MEDIUM**
`broker.c:1013-1036` (`agentIdentityStatus`), `tls_defaults.c:66-71` (`acceptAnyClientCertificate`
devuelve 1)
El hub exige cert de cliente pero acepta **cualquiera** autofirmado; la identidad es su
fingerprint. En TOFU (sin `--require-known-agents`), el **primer** peer en registrar un nombre
de agente fija su fingerprint. Un atacante con acceso a `tls://`/`quic://` genera un cert
desechable y registra un nombre sin reclamar (p.ej. `can-agent`) antes que el dispositivo real
→ el agente real recibe `REGISTER_STATUS_IDENTITY_MISMATCH` y queda bloqueado, mientras el
squatter recibe suscripciones e inyecta en ese "interface". Relacionado #75. Mitigable solo con
`--require-known-agents` (opt-in).

### SEC-6 · Convivencia de mismo nombre de agente en transportes sin fingerprint — **MEDIUM**
`interface_registry.c:283-302` (`isRegistrationColliding`), `broker.c:1017-1019`
La unicidad se impone por par (agent_name, interface_name), no por agent_name, y las
comprobaciones de identidad se saltan para peers sin fingerprint. Dos máquinas distintas pueden
estar vivas como agente "foo" con conjuntos de interfaces disjuntos en transportes plaintext.
En un `tcp://` expuesto: atacante registra "foo" con "vcan9" mientras el real exporta "can0" →
los grants `foo/*` autorizan el bus del atacante; `admin kick foo` afecta a quien registró
primero.

### SEC-7 · ACL por defecto = read-open — **MEDIUM (postura de diseño)**
`doc/security.md:62-63`, `broker.c:1390-1405` (`clientCanRead`), `identity_database.c:360`
(`*can_read = true` base)
Sin regla que aplique, el default es lectura abierta: cualquier cliente con cert aceptado (TOFU,
cualquier autofirmado) lee todas las interfaces. Combinado con SEC-5, un atacante de red no
autenticado llega a lectura total del bus. Documentado, pero conviene flag como postura por
defecto peligrosa.

### SEC-8 · Fail-open latente en la capa de seguridad cliente — **MEDIUM**
`tls_client_security.c:20-24`, `quic_client_security.c:32-36`, `tls_defaults.c:19-35`
Si la config de seguridad es NULL o carece de campos de pin, no se instala verify callback y
`SSL_CTX_set_verify` nunca se llama → default OpenSSL `SSL_VERIFY_NONE` → acepta cualquier cert
de servidor. Hoy inalcanzable (libcanhub siempre apunta los campos de pin a buffers embebidos),
pero el default-on-misconfiguration es accept-any en vez de fail-to-init. Un llamante futuro
(port Windows, transporte nuevo, path de test promovido) construye la config sin pin → MITM
activo del plano de control + datos. Debe fallar init o instalar reject-all callback.

### SEC-9 · Ventana TOFU: pin persistido dentro del verify callback — **MEDIUM**
`pinned_server_verifier.c:61-64`, `pin_store.c`
(a) El pin se persiste a disco **dentro** del cert-verify callback, antes de completar el
handshake → un atacante on-path que presente un cert una vez (incluso en conexión que el usuario
considera "fallida") queda pineado permanentemente (MITM persistente, o DoS fail-closed contra
el hub real). (b) Los pins se indexan por `host:port` literal → hub-por-IP y hub-por-hostname
llevan ventanas de primer-contacto independientes. Fix: pinear solo tras handshake completo.

### SEC-10 · ngtcp2 fijado por tag git mutable (único dep sin hash) — **HIGH**
`CMakeLists.txt:37-40` (`GIT_TAG v1.23.0`, sin SHA de commit ni `URL_HASH`)
ngtcp2 controla el backend crypto de QUIC (máximo privilegio) y es el único dep nativo sin pin
criptográfico — OpenSSL y SQLite usan `EXPECTED_HASH`/`URL_HASH SHA256`. Un retag/compromiso de
upstream se compila en cada binario en el siguiente configure limpio (dev + CI). Fix: fijar el
SHA de commit completo o tarball + `URL_HASH`. También en `test/CMakeLists.txt`.

### SEC-11 · Runner de tests: ELF opaco sin procedencia — **MEDIUM**
`test/vendor/cest-runner_linux_x86_64` (invocado por `Makefile:26`)
ELF de 3.4 MB commiteado (sha256 9d66592457…) sin repo upstream, versión, instrucciones de build
ni hash esperado documentados. Corre en cada `make test`. Quien aterrice un commit bajo
`test/vendor/` puede cambiarlo por un troyano; el diff es un blob binario irrevisable. Fix:
documentar upstream + sha256 esperado, idealmente fetch-and-verify o build desde fuente.

### SEC-12 · Account-lockout DoS en el login web — **MEDIUM**
`web/daemon/src/login_limiter.rs:37-41`, `api/handlers.rs:255-268`
El lockout por nombre (5 fallos/min por `user:victim` desde cualquier IP, incluso spoofed)
bloquea la cuenta sin importar el origen. Con `--trusted-proxy` y un proxy que no strippee
`X-Forwarded-For`, la clave por-IP también es rotable, dejando el límite por-nombre como único
backstop. Atacante no autenticado bloquea admins/usuarios objetivo de forma permanente.

### SEC-13 · Inyección de líneas en el pin-store del cliente — **LOW**
`pin_store.c:44-53`, `connect_url.c:57-69`, `libcanhub/canhub.c:434`
`ConnectUrl_Parse` acepta bytes arbitrarios en el host (split solo por el último `:`); la clave
del pin es `host:port` literal y `PinStore_Append` escribe `"%s %s\n"` sin escapar. Un host con
espacio+newline inyecta una línea de pin forjada para otro host:port. Líneas >511 bytes también
se parten bajo `fgets`. Atacante que influye la URL (fichero de config / `CANHUB_URL`) preenvenena
el store TOFU. Fix: rechazar whitespace/control en host.

### SEC-14 · Oráculo de timing en enumeración de usuarios (web) — **LOW**
`web/daemon/src/api/handlers.rs:329-338` (`verify_credential`), `auth/store.rs:252-267`
Usuario desconocido/deshabilitado retorna sin ejecutar argon2 (~0 ms) vs. nombre válido (~100 ms).
El cuerpo es uniforme, el timing no. Atacante remoto distingue nombres válidos por latencia.

### SEC-15 · Telemetría WebSocket sin re-chequeo de authz — **LOW**
`web/daemon/src/api/mod.rs:87`, `api/handlers.rs:172-175`
El WS de telemetría pasa el gate de permiso en el upgrade y nunca se re-chequea; expiración de
sesión / deshabilitación de usuario a mitad de stream no termina el socket. Usuario
deshabilitado/expirado sigue recibiendo telemetría hasta desconectar.

### SEC-16 · Escape XML ausente en beacon socketcand — **LOW**
`beacon_xml.c:14-44`
device_name, url y nombres de bus se interpolan en XML sin escapar; los nombres de bus incrustan
nombres de agente, controlados remotamente (hasta 127 bytes arbitrarios). Agente registrado como
`a"><Bus name="fake` → el CANBeacon difundido contiene elementos inyectados / XML malformado.
Solo broadcast, bajo impacto.

### SEC-17 · Refresh de dirección QUIC antes de validar el paquete — **MEDIUM**
`quic_server_transport.c:150-163` (`refreshPeerAddress`)
`refreshPeerAddress` sobreescribe `peer->remote_address` desde el origen del paquete **antes** de
que `QuicConnection_ReadPacket` lo descifre/valide, e incondicionalmente en cada paquete que casa
DCID. Un atacante que observe/adivine un DCID vivo (20 bytes) manda un paquete con origen spoofed
→ ngtcp2 descarta el paquete indescifrable pero el destino de egress del peer ya se redirigió a
la dirección del atacante hasta el siguiente paquete real. Redirección/DoS transitorio.

### SEC-menores (verificadas)
- `broker.c:1395/1412` — `clientCanRead/Write` devuelven `true` si `authorization == NULL` (DB ACL
  no disponible): el default "nadie inyecta" se invierte a rw total para clientes pineados.
  Mecanismo tras **#128**.
- Comparaciones de fingerprint con `strcmp` no constant-time (`pinned_server_verifier.c:54,65`).
  No explotable: los fingerprints son SHA-256 públicos de certs. Solo por completitud.
- `/api/setup` (web `handlers.rs:308-311`): endpoint público de bootstrap guardado solo por el
  conteo `require_first` de cero usuarios en la transacción. Correcto hoy, frágil a refactor.
- `main.rs:195` imprime el admin creado a stdout (no secreto, puede acabar junto a
  `CANHUB_WEB_PASSWORD` en logs CI).

### Verificado OK (no hallazgos)
Admin plane correctamente gateado: los 13 handlers admin comprueban `role == ADMIN` y
`HubPeer_AdoptRole` solo concede admin si `peer->local` (solo el socket unix). SQLite totalmente
parametrizado (sin inyección). Decoders de protocolo bounds-safe en los paths revisados. Cliente
verifica el servidor por defecto (TOFU, no accept-any) y no es deshabilitable por API pública/
Python. Web: params argon2id, tokens de sesión CSPRNG 256-bit hasheados SHA-256, flags de cookie
(HttpOnly/SameSite=Strict/Secure-gated), CSRF double-submit constant-time, enforcement de permisos
por grupo, bind default `127.0.0.1`. TLS 1.3 AEAD-only fijado.

---

## 2. Hub core (broker + domain)

### HUB-1 · Sin guarda contra segundo REGISTER de un agente ya registrado — **HIGH**
`broker.c:386-433`, `interface_registry.c:39-55`
`InterfaceRegistry_RegisterAgent` reinicia `agent_channel` a 0 en cada registro → un segundo
REGISTER con nombres de interface distintos crea claves `(agent_peer_id, agent_channel)`
duplicadas; `FindByAgentChannel` siempre resuelve a la primera (vieja). Agente hostil o buggeado
manda REGISTER dos veces → frames en channel 0 rutean a la interface equivocada, LIST anuncia
interfaces que nunca llevan tráfico, y `peer->agent_name` puede desincronizarse del registro
(kick-by-name, ACLs, ghost displacement todos keyed en él).

### HUB-2 · `FRAME_ROUTES_MAX` no cuenta múltiples bindings por cliente — **HIGH**
`frame_routes.h:9`, `frame_routes.c:33-35`, `broker.c:285,312-319`
`FRAME_ROUTES_MAX = PEER_DIRECTORY_MAX` (64) no considera que cada cliente puede tener 32 bindings
y múltiples opens de la misma interface. `FrameRoutes_FromAgent` trunca en silencio en `routes_max`
sin contador ni error. >64 bindings suscritos a una interface (3 clientes × 32 channels, o 65
clientes) → los suscriptores tras la ruta 63 nunca reciben frames; ninguna métrica lo registra.
Relacionado #145.

### HUB-3 · Reattach no re-evalúa `can_write` — **HIGH (seguridad)**
`broker.c:1197-1207`, `client_session.c:187-214`
`reattachClientsToAgent` re-chequea solo `clientCanRead`; el `can_write` del binding es el
snapshot del OPEN original y nunca se re-evalúa en reattach. Admin revoca escritura
(`acl set ... ro`) con el agente desconectado → agente reconecta → binding reattacha con
`can_write=true` stale → el cliente sigue inyectando pese a la revocación. (Más en general: la
revocación de ACL nunca afecta a sesiones vivas; enforcement solo lee el snapshot del OPEN,
`broker.c:323` — pero reattach es un punto barato y esperado para re-chequear.)

### HUB-4 · Modo reliable de channel nunca se revierte — **MEDIUM**
`broker.c:504-509,514-528,1206`; no hay `set_channel_mode(..., false)` en ningún sitio
Al abrir reliable, el broker pone el channel del agente en reliable pero nunca revierte en CLOSE,
desconexión de cliente, o cuando se va el último suscriptor reliable. Un cliente abre `iface`
reliable y cierra → el uplink del agente queda en el stream QUIC reliable para siempre → se pierde
la propiedad latest-wins/lossy (defensa declarada contra bufferbloat) hasta que el agente
reconecta; los suscriptores live quedan tras un uplink lossless con HOL-blocking. (Follow-up de #22.)

### HUB-5 · Frame reliable duplicado bajo backpressure de fan-out — **MEDIUM**
`broker.c:336-345,279-346,1262-1268`
`onPeerFrame` devuelve `accepted=false` si CUALQUIER ruta reliable falla al enviar → el transporte
retiene crédito y el emisor retransmite el frame entero → se re-forwardea a cada ruta que ya tuvo
éxito. Frame del agente hacia cliente reliable A (ok) y B (ring lleno → false) → el agente
retransmite → A recibe un duplicado en un channel cuyo contrato es "in-order y lossless"
(`protocol.md:459`). Duplicación rompe las semánticas de reliable.

### HUB-6 · Agentes plaintext/unix no pueden desplazar su conexión zombie — **MEDIUM**
`broker.c:1131-1154` (`displaceGhostPeer`), `:1136-1138`
El ghost displacement exige que el nuevo peer lleve fingerprint → agentes en tcp/unix nunca
desplazan su conexión previa medio-muerta. Link plaintext cae sin FIN (corte de corriente, NAT)
y reconecta antes de que el hub lo note → REGISTER colisiona → ack status 1 → el hub desconecta la
NUEVA conexión → el agente reintenta en bucle hasta que el TCP stale expira; interface no
disponible durante minutos. Es el modo de fallo que PR #146/#149 arreglaron para QUIC, aún abierto
para transportes sin fingerprint.

### HUB-7 · `client_session.c` usa `<stdio.h>`/`snprintf` (viola freestanding) — **MEDIUM**
`client_session.c:3,32-33`
Única dependencia de libc hosted en el hub core, viola la regla "todo fuera de src/platform/ es
freestanding" e inconsistente con el hábito `strncpy`/`memcpy` del propio código. Cualquier reuso
en MCU (propósito declarado de la regla) falla al linkar. Mismo problema en cores agent/client/
mirror/protocol (ver AGT-6).

### HUB-8 · Supresión de eco confía en el token del agente sin verificar el slot — **MEDIUM (seguridad)**
`broker.c:320-321,1448-1463`
La supresión de eco confía en el token del agente en `route_flags` y lo mapea a un índice de slot
del directorio, sin comprobar que el slot aún tiene el peer que inyectó el frame original.
(a) Agente malicioso estampa ECHO + token arbitrario en cada frame → el cliente en ese slot (si
abrió con SUPPRESS_OWN_ECHO) no recibe nada de ese agente: starvation dirigido. (b) Race benigno:
el originador desconecta, el slot se reusa por un cliente nuevo antes de que vuelva el eco → el
nuevo se suprime erróneamente.

### HUB-9 · Espacio de tokens (63) menor que directorio (64) — **MEDIUM**
`broker.c:21,1437-1446` vs `peer_directory.h:9`
`FRAME_ROUTE_TOKEN_VALUES_MAX 63` (tokens 1..63 → slots 0..62) pero `PEER_DIRECTORY_MAX` es 64 →
el peer en slot 63 recibe `FRAME_ROUTE_NO_TOKEN`. A alta ocupación, el cliente en slot 63 con
SUPPRESS_OWN_ECHO que inyecta → sus inyecciones sin token → sus propios ecos le vuelven, rompiendo
el contrato (no determinista, dependiente de ocupación).

### HUB-10 · `onPeerFrame` acepta `size >=` y reenvía basura trailing — **MEDIUM**
`broker.c:298,1249,1254/1265/1287`
Acepta `size >= MESSAGE_HEADER_SIZE + header.length` (no `==`) y reenvía el `size` recibido
completo → basura tras el cuerpo declarado se copia y relaya a cada suscriptor. Cliente manda un
FRAME clásico válido de 28 bytes padded a 84 → el hub reparte 84 bytes por ruta → ~3× amplificación
de ancho de banda y canal encubierto de 56 bytes entre peers que no aparece en ningún campo
decodificado; además sesga el shaper (mide solo `payload_length`).

### HUB-11 · `EgressQueue_Push` miente con size sobredimensionado — **MEDIUM**
`egress_queue.c:38-40`
`EgressQueue_Push` con `size > EGRESS_FRAME_BYTES_MAX` devuelve `kEGRESS_PUSH_QUEUED` sin encolar
nada; el llamante cuenta drops solo para no-QUEUED → el frame desaparece sin contar. Hoy
inalcanzable (onPeerFrame capa a FRAME_BUFFER_SIZE) pero el valor de retorno es una mentira; cambio
de constante lo convierte en pérdida silenciosa. Relacionado #145.

### HUB-menores
- `handleList`/`handlePing` sin role guard: un peer sin HELLO (rol UNKNOWN) o un agente enumeran
  el catálogo completo; ACLs no gatean LIST (`broker.c:435-447,546-562`). **LOW/sec**.
- `Broker_Tick` puede `disconnectPeer` (memset del slot) y luego deref del `peer` liberado para
  `EgressQueue_HasPending` (`broker.c:151-169`). Seguro hoy solo porque memset pone `used=0`;
  UAF latente. **LOW/bug**.
- `send_failed` como *recipient* (replies admin, PONG) deja el peer flagged pero conectado hasta
  que envíe control; ocupa slot (`broker.c:1212-1221`). **LOW/bug**.
- Tabla pending-ifconfig llena → admin recibe "agent unreachable" (falso diagnóstico); correlación
  FIFO por nombre puede cruzar admins (`broker.c:907-916,1056-1076`). **LOW/bug**.
- `countInterfaceFrame`/`frames_received` se incrementan antes del check de write → inyecciones
  denegadas inflan `frames_received` (`broker.c:308-309` vs `322-326`). **LOW**, rel #145.
- `reply.count` del port sin clamp a `ADMIN_*_ENTRIES_MAX` antes del loop de copia → overflow de
  stack si un adapter foráneo devuelve count>16 (`broker.c:804-817,844-854`). **LOW/bug**.
- `PeerDirectory_Release` hace memset incluyendo la EgressQueue → frames encolados desaparecen sin
  tocar `frames_dropped` (`peer_directory.c:43-50`). **LOW**, rel #145.
- Nombres de interface no charset-checked: `*` y `/` aceptados, colisionan con la gramática ACL;
  `agent_name` puede ser vacío (invisible a filtros, unkickable) (`register_message.c:129-145`).
  **LOW/bug**.
- `disconnectPeer` y `onPeerDisconnected` duplican el teardown; el invariante documentado vive en
  la duplicación (`broker.c:1114-1129` vs `231-248`). **LOW/design**.
- Contadores por-peer `uint32_t` vs métricas hub-wide `uint64_t` → wrap a ~4.3G frames
  (`hub_peer.h:38-39`). **LOW/tech-debt**.
- Magic number `ack->status = 1` donde existe `REGISTER_STATUS_REJECTED` (`interface_registry.c:35`).
  **LOW/tech-debt**.

---

## 3. Agent / Client / Protocolo core

### AGT-1 · `ChannelMap_AssignFromAck` no valida el count contra lo registrado — **MEDIUM**
`channel_map.c:12-22`
El `interface_count` del ack solo se chequea contra REGISTER_INTERFACES_MAX, nunca contra las
interfaces que el agente realmente registró. Hub hostil/buggeado ackea `interface_count=16` con 2
registradas → `ChannelMap_InterfaceForChannel` resuelve channels a index 2..15 → el core exporta
tx_dropped/credit basura en INTERFACE_STATUS y depende de que cada adapter CanPort bound-checkee
(el de SocketCAN sí, `socketcan_adapter.c:113`). Guard correcto: `== registration.interface_count`.

### AGT-2 · Solo se decodifica un FRAME por datagrama (viola spec de packing) — **MEDIUM**
`agent.c:171-200`, `client.c:211-231`, `mirror_app.c:148-168` vs `protocol.md:453`
La spec dice "múltiples FRAME pueden empacarse back-to-back en un datagrama hasta el MTU", pero
los tres paths de recepción decodifican exactamente un FRAME por buffer y descartan el resto en
silencio. Cualquier emisor que use el packing permitido (optimización futura del egress, o
implementación independiente) → cada frame tras el primero se pierde en agent, client y mirror.

### AGT-3 · Retry WRITE_DENIED del mirror pierde el bit reliable — **MEDIUM**
`mirror_app.c:226-229,216-222` (`handleOpenAck`)
El retry WRITE_DENIED re-OPENs con `OPEN_FLAGS_READ_ONLY`, tirando el bit reliable aunque
`self->reliable` esté set; el ack OK subsiguiente aún hace `if (self->reliable)
set_channel_mode(channel, true)`. Mirror `--reliable` con ACL que deniega escritura → retry abre
lossy en el hub, pero el transporte local marca el channel reliable → modo de channel discrepa
entre los dos extremos.

### AGT-4 · Encoder ACL list sin guarda de terminación — **MEDIUM (bug latente)**
`admin_message.c:699-703` (`AdminAclListReplyMessage_Encode`)
Único reply encoder que no valida terminación de name/fingerprint antes de `strlen`+`memcpy`;
todos los hermanos (peers, pins, agents, clients, interfaces) validan y devuelven 0. Un
`agent_name` sin NUL (128 bytes) → `strlen` lee más allá del campo y `memcpy` escribe más allá del
slot de 212 bytes, overflow del buffer de encode en el hub. Los llamantes actuales pasan datos
NUL-terminados, así que es contract hole latente, no exploit vivo.

### AGT-5 · ADMIN_INTERFACES_REPLY: doc/impl divergen (168 vs 160) — **MEDIUM**
`admin_message.h:46`, `admin_message.c:64` vs `doc/protocol.md:346-356`
La spec dice entradas de 160 bytes sin `tx_dropped`; la impl usa 168 con `tx_dropped u64` en +160
(el mirror Rust `web/daemon/src/protocol/admin.rs:378-386` concuerda en 168). La prosa
(`protocol.md:212`) ya dice que tx_dropped se expone aquí, solo la tabla de layout está stale. Una
implementación independiente que use stride 160 misparsea cada entrada tras la primera. **Bloquea #18.**

### AGT-6 · `<stdio.h>`/`snprintf` en árboles freestanding — **MEDIUM (tech-debt)**
`interface_name.c:3,17` (¡en src/protocol/!), `client.c:3,57,99`, `mirror_app.c:3,36,46`
`#include <stdio.h>` + `snprintf` en tres árboles mandados freestanding. Todos los usos son copias
acotadas expresables con `strnlen`/`memcpy`. Además `Client_SetName(self, NULL)` /
`MirrorApp_SetName(NULL)` son UB (`%s` con NULL). Compilar los cores protocol/client para un MCU
sin libc arrastra stdio.

### AGT-menores
- `handleInterfaceStatus` sin check de estado/channel-validez: `self->channel` es 0 tras memset,
  que es un channel real → una status para channel 0 reprograma el shaper de un cliente que no
  abrió nada (`client.c:351-371`). **LOW/bug**.
- Wire ERROR no resetea `state` fuera de LISTING/RESOLVING/OPENING → cliente atascado para siempre
  si el hub responde ERROR en vez del reply tipado (`client.c:272-281`). **LOW/bug**.
- FRAMEs entregados no se chequean contra el channel de la sesión (`frame.channel != self->channel`)
  en client/mirror; el agente sí lo hace vía ChannelMap (`client.c:217-230`). **LOW/bug**.
- Paginación de resolución de nombres confía en el flag MORE sin bound; `list_offset` wrapea u16 →
  hub que siempre setea MORE hace ping-pong infinito (`client.c:314-317`, `mirror_app.c:189-192`).
  **LOW/bug**.
- Mirror sin recuperación de kMIRROR_FAILED; reconexión tras fallo abre OPEN con `interface_id`
  quizá 0 en vez de re-resolver (`mirror_app.c:97-107`). **LOW/bug**.
- `ReconnectBackoff_Init` no valida: `initial_delay_ms==0` → delay permanente 0 ms (bucle de
  reconexión); `max_delay_ms` patológico wrapea antes del clamp (`reconnect_backoff.c:19-28`).
  Latente (solo se usan defaults 1000/60000). **LOW/bug**.
- `tx_pacer` `credit += advertised_rate / DIVISOR` wrapea u32 con rate >~3.8 Gbit/s → crédito
  colapsa (`tx_pacer.c:38`). Inalcanzable con rates CAN reales. **LOW/bug**.
- EchoCorrelator descarta el token más viejo con >32 inyecciones en vuelo → supresión de eco falla
  para los desbordados, sin contador (`echo_correlator.c:22-24`, `agent.c:154-158`). **LOW/design**.
- socketcand: `AsciiFramer_Next` trunca comandos >255 bytes en vez de rechazar (buffer 256 vs
  framer 512) → posible smuggling semántico de un `send` (`ascii_framer.c:50-56`). **LOW/sec**.
- socketcand: `command_size==0` underflow → `memcpy(..., SIZE_MAX)` (inalcanzable con el caller
  actual) (`ascii_framer.c:51-53`). **LOW/bug**.
- socketcand: `parseDecimal` sin cap de longitud → `acc*10+digit` wrapea u32, dlc wrapea a ≤64 y
  pasa el guard (`socketcand_codec.c:285-302`). **LOW/bug**.
- socketcand: `RenderFrame` hace `can_id & MASK` solo → pierde el flag EFF para ids ≤0x7FF y
  renderiza RTR/ERR como data frames (`socketcand_codec.c:139`). **LOW/bug**, rel #10.
- `IfconfigMessage_Decode` no valida `op ∈ {0,1,2}` ni nombre no vacío (`ifconfig_message.c:42-53`).
  **LOW/bug**, rel #52.
- `HelloMessage_Encode` trunca nombre sin NUL con `strnlen(name,63)` en vez de rechazar
  (inconsistente con register/error) (`hello_message.c:40`). **LOW/bug**.
- `REGISTER_STATUS_UNKNOWN_AGENT 3` implementado/enviado pero ausente en la lista de la spec
  (`register_message.h:16` vs `protocol.md:125`). **LOW/design**, rel #18.
- Boilerplate de codecs duplicado (encode+memset body) en 3+ ficheros; `Protocol_EncodeBody`
  compartido borraría ~10 duplicaciones (`ifconfig_message.c:146-164` et al.). **LOW/tech-debt**.
- `control_buffer.h:23`: assert tautológico; los `*_REPLY` variables (LIST_REPLY 2380…) no caben en
  el buffer de 392 y no se asertan → falsa sensación de "todo cabe". **LOW/tech-debt**.
- Helpers genéricos de admin hardcodean la constante de un mensaje para todos los callers
  (`ADMIN_KICK_REPLY_BODY_SIZE` etc.) — correcto solo mientras coincidan (`admin_message.c:883,909,934`).
  **LOW/tech-debt**.
- Comentario stale en `interface_status_message.h:9-15` ("advertised_rate/credit reservados") — se
  rellenan desde el pacing. **LOW/tech-debt**.
- Estilo: `tx_pacer.c:32-33` rompe asignación multilínea (contra convención); mirror encodea FRAME
  en buffer de 392 donde 84 es lo correcto (`mirror_app.c:70`). **LOW/tech-debt**.

---

## 4. Plataforma (linux / windows)

### PLT-1 · #128 confirmado: hub tcp/unix-only deshabilita pin DB y ACLs — **MEDIUM**
`hub_main.c:227-258,351-374` (`identityStore`/`loadIdentity`)
`state_directory` solo se puebla dentro de `loadIdentity`, que corre solo cuando un listener
quic/tls necesita identidad. Un hub tcp/unix-only deja `state_directory[0]=='\0'` → `identityStore()`
devuelve NULL → pin DB de agentes y ACLs de clientes silenciosamente deshabilitados, sin warning.
**Ya trackeado en #128.** Fix: resolver el state dir incondicionalmente, con carga de cert TLS aún
gateada por tls/quic.

### PLT-2 · Puertos de escucha sin validar; solo IPv4 — **LOW**
`quic_server_transport.c:98`, `tcp_server_transport.c:61`, `tls_server_transport.c:78`,
`socketcand_server.c:64`
`htons((uint16_t)atoi(port))` sin validación: puerto no numérico o fuera de rango → 0 (puerto
efímero aleatorio) o valor truncado. Todos los binds son `AF_INET`/`inet_pton(AF_INET,...)` — sin
IPv6. IPv6 es #46; la falta de validación de puerto es nueva.

### PLT-3 · Resolución DNS una sola vez, sin re-resolver ni failover — **LOW**
`quic_udp_endpoint.c:42-67`, `tcp_client_transport.c:204-234`
El hostname se resuelve al conectar y el cliente `connect()`ea a una única dirección; en reconexión
se reusa sin re-resolver y solo se prueba `ai_addr[0]` (sin failover entre A records). Cambio de DNS
del hub o primera dirección inalcanzable → el agente nunca recupera. Es la mitad "re-resolve" de #46.

### PLT-4 · Fugas de recursos en fallo parcial de Init — **LOW**
`quic_server_transport.c:82-108`, `tls_server_transport.c:66-86`
Si `bind`/`inet_pton` falla tras crear udp_fd+timer_fd (QUIC) o listen socket + `SSL_CTX` (TLS),
`Init` devuelve false sin cerrar fds ni liberar el `SSL_CTX`. En el path sin listener explícito el
hub sigue corriendo con el fd/SSL_CTX fugado durante toda la vida del proceso. Solo en arranque.

### PLT-5 · Crédito de stream de control extendido aunque el framer esté lleno — **LOW**
`quic_client_transport.c:359-378`, `quic_server_transport.c:636-661` (`onStreamData`)
Para el stream de control se concede `ExtendStreamCredit(size)` completo aunque
`receiveControlData` no lo consuma (framer lleno — mensaje > 4 KiB `MESSAGE_FRAMER_BUFFER_SIZE`).
Peer manda un mensaje de control > 4 KiB → `MessageFramer_Push` devuelve 0 para siempre, bytes
descartados mientras el crédito sigue extendiéndose → ese stream de control se atasca (auto-inflingido,
acotado). En TCP/TLS un mensaje > 4 KiB desconecta el peer.

### PLT-6 · Más streams bidi entrantes que el máximo → aceptados y descartados — **LOW**
`quic_reliable_streams.c:85-103` (`Adopt`)
Un peer puede abrir más streams bidi que `QUIC_RELIABLE_STREAMS_MAX` (8). `Adopt` devuelve NULL
para el exceso y el código cae a `ExtendStreamCredit`, aceptando-y-descartando datos en vez de
rechazar/resetear. Cada stream adoptado hace malloc de 128 KiB (~1 MiB/peer, ~64 MiB total,
acotado). Cliente abre 9+ streams reliable → los frames del 9º desaparecen sin error.

### PLT-menores
- Duplicación grande entre `windows/libcanhub/canhub.c` y `linux/...` (struct de sesión, ring de
  frames, máquina list/open/recv/send copy-pasteadas); ya ha driftado (SIGPIPE solo en Linux).
  `windows/shared/tls_identity.c` escribe la clave con `fopen("w")` sin ACL (análogo Windows de
  SEC-3). **LOW/tech-debt**.
- `acceptAnyClientCertificate` devuelve 1 incondicional (`tls_defaults.c:66-72`): intencionado
  (identidad = fingerprint, enforcement en broker), pero un bug en `--require-known-agents` acepta
  cualquier autofirmado. TLS min/max correctamente fijado a 1.3. **Frontera de confianza, no defecto**.
- Peer-id ranges hardcodeados 0x40000000 aparte; `next_peer_id` sin manejo de wrap → tras ~1e9
  conexiones wrapea al rango del siguiente transporte y `portForPeer` misrutea (`hub_main.c:32-37,432-445`).
  Es el mecanismo tras **#26**. **LOW/tech-debt**.

---

## 5. Tests / CI / Build / Packaging

### TST-1 · e2e (Robot) nunca corre en CI — **HIGH**
`.github/workflows/ci.yml` (sin job e2e)
`make e2e` (smoke, client, socketcand, reconnect, fairness, quic_mtu, latency, backpressure,
reliable, web, libcanhub, python_can, firmware_integrity…) nunca corre en CI; ningún workflow
invoca robot. Regresiones de integración (la clase reconnect/re-attach/MTU que los unit tests no
pueden atrapar — #146/#148/#149 fueron todos fallos de integración) llegan a main y releases sin
gate automático.

### TST-2 · Tests de Python nunca corren en CI — **HIGH**
`.github/workflows/*` (sin pytest/tox)
Ninguna invocación de pytest en CI. `conftest.py:16-35` pone `collect_ignore` + `warnings.warn`
(no fail) cuando no hay `libcanhub.so`, así que un `pytest` pelado reporta "no tests ran" como
éxito. Deriva de struct ctypes/ABI vs `include/canhub.h`, o break de packaging de wheel, llega a
PyPI sin testear.

### TST-3 · No hay job de sanitizer/valgrind; la premisa del workaround ASLR es falsa — **HIGH**
`Makefile:91-95`, `test/CMakeLists.txt` (sin `-fsanitize`)
El Makefile deshabilita ASLR con `setarch -R` justificando "el runner es un build ASan (obligatorio:
memory/leak checks)", pero no hay `-fsanitize` en ningún CMake/toolchain de test, los ejecutables
llevan solo `-Wall -Wextra`, y el cest-runner vendido es un PIE normal sin ASan. La instrumentación
de memoria "obligatoria" no existe; `test_broker` no linka símbolos asan. Overflows/UAF/leaks en
domain corren limpios en CI. Todo el workaround ASLR (#28) se defiende con una premisa falsa.

### TST-4 · `create-release` no depende del job de tests — **MEDIUM**
`.github/workflows/release.yml:11-18`
`create-release` corre en cualquier tag `v*` sin `needs:` al job de tests. Un tag empujado sobre un
commit rojo aún corta un GitHub release y publica wheels a PyPI (`:114-128`, irreversible por versión).

### TST-5 · Ninguna arquitectura no-x86_64 corre tests; cache key ignora `src/` — **MEDIUM**
`.github/workflows/ci.yml:21-25`
El cache key se hashea solo sobre CMakeLists/cmake/**; cambios en `src/` no cambian la key y el árbol
`build` cacheado se restaura entero → un build incremental sobre cache stale puede saltarse aristas de
dependencia. armhf es build-only; ningún arch no-x86_64 corre tests → breakage cross-arch
(alineación, endianness, time_t 32-bit) nunca se cubre. Rel #124.

### TST-6 · Gaps de cobertura unitaria — **HIGH/MEDIUM**
- broker `on_peer_disconnected` solo se ejercita con AGENT_PEER; teardown de client/admin nunca
  aserto (misma clase que #149 en el lado no testeado). **HIGH**.
- Path reliable del agente nunca ejercitado (broker sí; agente es hueco). **HIGH**.
- `test_tls_channel` solo happy-path: sin partial-read/write, sin WANT_READ/WRITE, sin cola llena,
  sin framing partido, sin EOF a mitad. El record pump compartido cliente/servidor esencialmente sin
  verificar. **HIGH/quality**.
- Sin e2e para ACLs, pins/allowlist, cli admin, mirror, transacciones. **MEDIUM**, rel #94.
- `wire.c`, `pinned_server_verifier.c`, `connection_table.c`, `tls_defaults.c` sin tests directos.
  **MEDIUM**.
- `test_version` nunca define `CAN_HUB_VERSION` → asserta el fallback "0.0.0-dev"; un typo de
  `make bump` no se atrapa. **MEDIUM/quality**.
- `transport_port_mock` sin flag de fallo on-demand → comportamiento del agent/client cuando el write
  del link falla es intesteable. **MEDIUM**.

### TST-7 · e2e: sleeps fijos y estado oculto en Keywords — **MEDIUM**
Sleeps fijos en vez de poll de condición: `fairness.robot:32` (el flake #70), `socketcand.robot:29`,
`socketcand_write_burst.robot:35`, `latency.robot:41/69`, `reconnect.robot:32/55` (40s/15s), etc.
`fairness.robot:47` asserta igualdad exacta (quiet==60, drops==0) tras un Sleep fijo de 2s para que
el bridge conecte → si no cachó en 2s, 59/60 → fallo duro (raíz de #70). Lógica de negocio + estado
oculto (`Set Suite Variable ${HUB}`) en `*** Keywords ***` viola la regla del proyecto; el refactor
en vuelo (`e2e_refactor.md`) confirma que es sistémico.

### BLD-1 · OpenSSL rebuild gateado por existencia de fichero, no por versión — **MEDIUM**
`cmake/openssl.cmake:29`
El build one-off de OpenSSL se salta si `${prefix}/lib/libssl.a` existe. Bumpear
`CAN_HUB_OPENSSL_VERSION` en un árbol existente no dispara rebuild; un build previo interrumpido que
emitió libssl.a pero no headers/libcrypto.a se trata como completo. Guard debe keyear por versión.

### BLD-2 · Tests linkan OpenSSL del sistema, prod linka 3.5.7 de fuente — **MEDIUM**
`test/CMakeLists.txt:14` vs `cmake/openssl.cmake`
Los tests hacen `find_package(OpenSSL)` contra el sistema (distro 3.0/3.2) mientras prod compila y
linka 3.5.7. `test_tls_channel` valida contra una versión distinta de la que se envía; diferencias
de comportamiento entre 3.2 y 3.5 (record handling, verify callbacks) no se cubren.

### BLD-3 · Build networked en configure, sin fallback offline — **MEDIUM**
`cmake/openssl.cmake:34`, `CMakeLists.txt:26`
Compilación completa de OpenSSL + fetch de ngtcp2/sqlite en configure de cada árbol nuevo, sin
fallback a OpenSSL del sistema. Builds air-gapped o primer-configure-sin-red fallan. CI instala
`libssl-dev` que el path nativo nunca usa (dep muerta).

### BLD-menores
- Versiones/hashes pinneados con 2-3 copias a mano sin guard de lockstep (sqlite en dos CMakeLists;
  OpenSSL/toolchain SHAs en openssl.cmake + dos Dockerfiles; llvm-mingw en ci.yml + release.yml).
  **LOW**.
- Build de test sin optimizar + `-Wno-unused-parameter`; ningún `-Werror` en test ni prod → warnings
  nunca gatean, bugs solo-O2 no vistos. **LOW**.

### PKG-1 · El deb del hub arranca listener QUIC 0.0.0.0 sin opt-in — **MEDIUM**
`packaging/debian/hub/postinst:13-16`, `packaging/hub.conf:14`
Instalar el deb del hub habilita Y arranca `can-hub.service` inmediatamente con default
`--listen quic://0.0.0.0:7227` — listener QUIC en todas las interfaces sin opt-in del operador. El
deb del agente deliberadamente NO auto-arranca; la asimetría es el riesgo. mTLS mitiga pero el
puerto queda abierto por defecto.

### PKG-2 · `make install` y los deb divergen mucho (más allá de #19) — **MEDIUM**
`CMakeLists.txt:265-273` vs `cpack-deb.cmake`
`make install` no envía units systemd, ni `/etc/can-hub/*.conf`, ni completions, ni can-hub-web, no
crea usuario `can-hub`, y nunca instala can-hub-client (solo componente CPack). A la inversa, envía
artefactos dev que los deb nunca empaquetan (`canhub_shared`+`canhub.a`→/lib, header) sin un deb
`-dev`. Prefijos distintos (/usr/local vs /usr). Rel #19.

### PKG-menores
- Hardening systemd parcial: falta PrivateDevices, ProtectKernelModules/Logs, ProtectProc, RestrictRealtime,
  RestrictSUIDSGID, LockPersonality, SystemCallFilter=@system-service, SystemCallArchitectures,
  UMask=0077. **LOW**.
- Purge no borra el usuario `can-hub` creado por postinst (sin `userdel`); systemctl hand-rolled en
  vez de deb-systemd-helper. **LOW**.
- `.dockerignore` solo excluye build/dist/.git/spike → `COPY . /src` arrastra .venv, node_modules,
  web/daemon/target y `libcanhub.dll` stray a cada imagen; una DLL Windows puede acabar en un wheel
  Linux manylinux. **LOW**.
- Imágenes base pinneadas por tag (debian:bookworm-slim, manylinux) no por digest — único gap de
  reproducibilidad. **LOW**.

---

## 6. API pública / Python / Web / Docs

### API-1 · Flag reliable (#147) no aterrizó en el libcanhub de Windows — **HIGH**
`windows/libcanhub/canhub.c:234-239,584-597` vs linux `:258-260,579-581`
Windows `canhub_open` nunca mapea `CANHUB_OPEN_FLAG_RELIABLE → OPEN_FLAG_RELIABLE` ni
`OPEN_STATUS_RELIABLE_UNSUPPORTED`. El flag se ignora en silencio; un llamante Windows recibe
`CANHUB_OK` y un channel datagram lossy creyendo que es reliable, y nunca puede recibir
`CANHUB_ERR_RELIABLE_UNSUPPORTED`. `doc/libcanhub.md:44,64-69` documenta el flag sin caveat de
Windows y dice "todos los transportes incluido QUIC". Regresión de #147.

### API-2 · `libcanhub.so` reexporta ~2000 símbolos de OpenSSL 3.5 estático — **HIGH**
`CMakeLists.txt:257-264` (verificado: `libcanhub.so.0` tiene 9411 símbolos dinámicos)
`C_VISIBILITY_PRESET hidden` oculta solo fuentes del proyecto; sin version script / `--exclude-libs`
para los archivos estáticos de OpenSSL/ngtcp2. Cualquier proceso que cargue libcanhub junto a la
libssl del sistema (Python con `ssl` importado — el caso real del backend Python) sufre interposición
de símbolos entre dos OpenSSL distintos → corrupción de estructuras, crashes difíciles de
diagnosticar. Fix: version script que exporte solo `canhub_*`, o `--exclude-libs ALL`.

### API-3 · Contrato de timeouts inconsistente; `-1` significa dos cosas — **HIGH**
`canhub.c:563-566` (`effectiveTimeout`), `doc/libcanhub.md:21`, `examples/canhub_dump.c:61,79,109`
`effectiveTimeout` convierte `timeout_ms <= 0` en 5000 ms para connect/list/open, pero la doc dice
`-1 = bloquear siempre` para `connect_timeout_ms`, y el ejemplo enviado pasa `-1` esperando bloqueo
infinito. Solo `canhub_recv` trata `-1` como infinito. `0` significa "default 5s" en list/open pero
"non-blocking" en recv.

### API-4 · Fallos de `connect` opacos; `canhub_last_error(NULL)` segfaultea — **HIGH**
`canhub.c:97-166` (linux) / `:87-151` (win), `include/canhub.h:86-88`
`canhub_connect` devuelve NULL en cualquier fallo (URL mala, mismatch de fingerprint, timeout,
identidad ilegible) sin forma de saber por qué: `canhub_last_error` exige una sesión y la deref sin
NULL-check. `doc/libcanhub.md:42,48` anuncia `canhub_last_error` como "detalle del último fallo", pero
el fallo más común es el que no puede explicar, y el `canhub_last_error(NULL)` natural crashea.

### API-menores (C)
- Sin validación de sesión NULL en ninguna función pública; config y frame args sí se validan, la
  sesión no (`canhub.c:189-362`). **MEDIUM**.
- `canhub_send` descarta frames en silencio pero devuelve `CANHUB_OK` (shaper sin budget); sin señal
  de backpressure ni contador expuesto (`canhub.c:335-362`). **MEDIUM**, rel #145.
- `canhub_set_filters` fire-and-forget sin validar `filters==NULL`/`count>0`, sin timeout; pre-open
  cachea en silencio sin confirmar (`canhub.c:291-309`). **MEDIUM**.
- `canhub_recv(timeout=0)` con ring vacío nunca bombea el transporte → un poll no bloqueante no lee
  frames encolados en kernel (`canhub.c:311-333`). **MEDIUM**.
- `struct_size != sizeof` (igualdad estricta) rompe todo binario viejo al añadir un campo, contra la
  extensibilidad que promete la doc (`canhub.c:105`). **MEDIUM**.
- Structs de salida (`CanHubInterfaceInfo`) sin campo size/version → añadir un campo desincroniza
  binarios viejos bajo el mismo `.so.0` sin detección (`include/canhub.h:63-67`). **MEDIUM**.
- `CANHUB_API` siempre `dllexport` en `_WIN32`, sin rama import ni convención de llamada
  (`include/canhub.h:13-19`). **MEDIUM**.
- Sin thread-safety y sin documentarlo; `startWinsock` con `static bool` sin sincronizar ni WSACleanup;
  `ignoreSigpipeIfUnhandled` muta SIGPIPE process-wide con race check-then-act (`canhub.c`). **MEDIUM**.
- Fuga de fd si `EpollRegistry_Open` falla tras `initTransport` (sin `port->disconnect`)
  (`canhub.c:140-150`). **MEDIUM**.
- ~650 líneas duplicadas linux/windows canhub.c, ya driftando. **MEDIUM**.
- Cluster LOW: sin define de versión de release en el header; `list_truncated` trackeado pero nunca
  expuesto; `SOVERSION 0` sin version-script ni `.def` Windows; `libcanhub.a` exporta globales sin
  prefijo; `onOpenResult` genérico para todos los deny; ejemplo con `truck42/can0`.

### PY-1 · `_apply_filters` nunca limpia filtros hub-side → pérdida silenciosa — **HIGH**
`python/canhub/bus.py:170-173`
Con `filters` falsy (contrato "reset filters" de python-can) o >16 filtros, solo pone
`_hardware_filtered=False` y retorna; nunca llama `canhub_set_filters(session, NULL, 0)` aunque la C
API tiene semántica replace y count 0 limpia. Los filtros nativos viejos siguen activos;
`bus.set_filters()` para reabrir todo el tráfico sigue descartando.

### PY-2 · Sin thread-safety en el pump ctypes → rompe Notifier+send estándar — **HIGH**
`bus.py:126,156` + `canhub.c:311-360`
`canhub_recv`/`canhub_send` ambos bombean `pumpOnce` sobre la misma sesión sin lock, y Python no
añade lock. `can.Notifier` (hilo lector) + `bus.send()` corre carrera contra el pump epoll, el ring
de frames y last_error. El README no menciona restricción de un solo hilo.

### PY-menores
- Fuga de sesión si `super().__init__()` lanza (`_apply_filters` con `can_id` no-int) antes de flipear
  `_is_shutdown`; `__del__` salta `shutdown()` (`bus.py:64`). **MEDIUM**.
- Clave `"extended"` del dict de filtro descartada en silencio pero tratada como match hardware exacto
  (`bus.py:176-178`). **MEDIUM**.
- Sin chequeo de versión ABI al cargar; `canhub_api_version` bindeado pero nunca llamado (`_native.py`).
  **MEDIUM**.
- README dice que los tests corren sin build; se saltan sin él (ilusión de CI verde) (`README.md:64`
  vs `conftest.py:33-35`). **MEDIUM**.
- `send(timeout=...)` ignorado en silencio, sin documentar (`bus.py:134`). **MEDIUM**, rel #145.
- Cluster LOW: `msg.data` sobredimensionado lanza ValueError ctypes crudo; `recv(timeout=None)`
  no interrumpible por Ctrl-C; sin `py.typed` ni `__version__`; pyproject sin classifiers/authors,
  `license={text=}` legacy; cero cobertura unitaria de send/recv/open/shutdown.

### DOC-1 · protocol.md diverge de la impl (bloquea #18) — **HIGH**
- ADMIN_INTERFACES_REPLY: layout 160 vs 168 real (ver AGT-5). `doc/protocol.md:346-356`.
- REGISTER_ACK: falta status 3 UNKNOWN_AGENT (`:124-125`).
- LIST descrito como filtrable por nombre/fingerprint; el wire solo lleva offset de paginación
  (`:38`, `design.md:58` vs `list_message.c:8`).
Corregir la spec **antes** de cualquier freeze v1.

### DOC-2 · `make install` documentado con 4 binarios + libcanhub; instala 3 — **HIGH**
`doc/installation.md:90-91` vs `CMakeLists.txt:271`
Solo instala `can-hub can-hub-agent can-hub-cli`; `can-hub-client` solo bajo `CAN_HUB_PACKAGING=ON`.
Rel #19.

### DOC-menores
- Forma de packaging mal documentada: can-hub-web va DENTRO del deb `can-hub`, la doc dice "build
  from source" / "paquete opcional can-hub-web" (`installation.md`, `web.md`, `design.md:115` vs
  `cpack-deb.cmake:74-81`). **MEDIUM**.
- `agent.md` nunca documenta `--pace-rate <bps>` (implementado). **MEDIUM**, rel #52.
- `client.md` sin `--reliable` ni `--name` (ambos implementados). **MEDIUM**.
- OpenAPI congelado en 0.1.0 mientras el daemon es 0.2.7; `make bump` no lo toca (`web/openapi.yaml:4`).
  **MEDIUM**.
- Cluster LOW: security.md sobre el warn de tcp expuesto (solo con `--require-known-agents`);
  `web.conf` usa `unix://` pero `--connect` quiere path pelado; comentario stale de reservados en
  `interface_status_message.h`; `--complete` sin documentar; README omite el listener tcp default;
  `make bump` dice "tres manifiestos" pero edita cuatro y no cubre openapi.yaml ni package.json.

---

## 7. Higiene del repo

- **HIG-1 — `HARDWARE_ESP32_AGENT.md` untracked NO excluido — MEDIUM.** Auto-etiquetado "interno, no
  publicar" en la raíz del repo **público**, y a diferencia de ANALISIS_COMPETENCIA.md NO está en
  `.git/info/exclude` (aparece `??`) → un `git add` en bloque publica estrategia de producto interna.
  Su contenido software mapea a #12/#24. **Acción: añadir a exclude ya.**
- **HIG-2 — `test/e2e/e2e_refactor.md` untracked es trabajo diferido con decisiones tomadas, sin
  issue — MEDIUM.** Diseño de 121 líneas de la re-arquitectura del keyword layer e2e. Por la regla
  del proyecto va en una issue (ninguna lo cubre; #70/#94 no). Tampoco en exclude.
- **HIG-3 — CLAUDE.md referencia TODO.md como artefacto del repo, pero TODO.md es untracked/local —
  LOW.** El log de decisiones existe solo en esta máquina sin backup; un lector del repo público no
  lo encuentra.
- **HIG-menores:** ANALISIS_COMPETENCIA.md correctamente excluido pero protegido solo por
  `.git/info/exclude` local (sin backup); entrada muerta `LICENSING-NOTES.md` en exclude;
  `spike/e0-quic-datagram/` trackeado, referenciado en ningún sitio y ya no compila tras la migración
  a OpenSSL (aún pide GnuTLS); los tres manifiestos de ecosistema declaran `AGPL-3.0-only` sin
  expresión SPDX del brazo comercial que README/NOTICE/LICENSE.commercial sí indican.

---

## Lista de tareas

Ordenada por prioridad. `[#NN]` = issue existente; sin marca = crear issue nueva.

### P0 — Seguridad / corrección crítica (hacer ya)
- [ ] **SEC-1/SEC-2**: no habilitar plain-tcp por defecto (o exigir bind explícito no-loopback con
      warning), y fijar permisos del socket unix (`fchmod 0660` + grupo, o `SO_PEERCRED`). Documentar
      el modelo de confianza localhost de forma explícita.
- [ ] **SEC-10** `[relacionar #18/build]`: fijar ngtcp2 por SHA de commit o tarball + `URL_HASH SHA256`
      en `CMakeLists.txt` y `test/CMakeLists.txt`.
- [ ] **SEC-3**: escribir claves privadas con `open(O_CREAT|O_EXCL,0600)`/`fchmod` antes de los bytes
      (linux + windows). Igual para pin_store.
- [ ] **HUB-3**: re-evaluar `can_write` en reattach; evaluar propagar revocación de ACL a sesiones vivas.
- [ ] **HUB-1**: rechazar segundo REGISTER de un peer ya registrado (o resetear su registro previo).
- [ ] **AGT-4 / HUB-menor(clamp)**: añadir guarda de terminación en `AdminAclListReplyMessage_Encode`
      y clamp de `reply.count` a `ADMIN_*_ENTRIES_MAX` antes de los loops de copia.
- [ ] **PLT-1** `[#128]`: resolver `state_directory` incondicionalmente; warn si pin DB/ACLs quedan
      deshabilitados.

### P1 — Corrección / robustez alta
- [ ] **SEC-4**: cap de peers por IP/subred; Retry/token de validación de dirección en QUIC; timeout de
      handshake/idle para TCP/TLS half-open.
- [ ] **SEC-8**: la capa de seguridad cliente debe fail-to-init (o reject-all) si falta config de pin,
      nunca accept-any.
- [ ] **SEC-9**: pinear TOFU solo tras handshake completo, no dentro del verify callback.
- [ ] **SEC-17**: no refrescar `remote_address` hasta que ngtcp2 valide la ruta del paquete.
- [ ] **HUB-2**: dimensionar/contar rutas por bindings reales, con contador de drop por truncación
      `[relacionar #145]`.
- [ ] **HUB-4**: revertir `set_channel_mode(..., false)` en CLOSE/desconexión/último suscriptor reliable.
- [ ] **HUB-5**: no re-forwardear a rutas ya satisfechas en retransmisión reliable (per-route credit).
- [ ] **HUB-6**: permitir displacement de agentes sin fingerprint (extender el fix de #149 a tcp/unix).
- [ ] **HUB-8/HUB-9**: verificar que el slot del token de eco aún tiene al originador; alinear espacio
      de tokens con `PEER_DIRECTORY_MAX`.
- [ ] **HUB-10**: exigir `size == HEADER + length` en `onPeerFrame` y reenviar solo el cuerpo declarado.
- [ ] **AGT-1**: `ChannelMap_AssignFromAck` debe exigir `count == registration.interface_count`.
- [ ] **AGT-2**: decodificar múltiples FRAME por datagrama (o corregir la spec si se decide no soportar
      packing) — decidir junto a #18.
- [ ] **AGT-3**: preservar el bit reliable en el retry WRITE_DENIED del mirror, o abortar coherentemente.
- [ ] **API-2**: version script / `--exclude-libs ALL` para no reexportar símbolos de OpenSSL/ngtcp2.
- [ ] **API-4/API-3**: NULL-check en `canhub_last_error`; unificar la semántica de `-1`/`0` en timeouts
      y alinear doc + ejemplo.
- [ ] **API-1**: portar el mapeo del flag reliable + `RELIABLE_UNSUPPORTED` al libcanhub de Windows
      `[relacionar #147]`.
- [ ] **PY-1**: `_apply_filters` debe llamar `canhub_set_filters(session, NULL, 0)` al resetear/desbordar.
- [ ] **PY-2 / API(thread)**: documentar el modelo de hilos y/o añadir locking; soportar el patrón
      Notifier+send de python-can.
- [ ] **TST-1/TST-2/TST-3**: correr e2e en CI; correr pytest en CI (fallar si no hay lib); añadir job de
      ASan/UBSan real (o valgrind) `[relacionar #28]`.
- [ ] **DOC-1**: corregir protocol.md (ADMIN_INTERFACES 168, REGISTER status 3, LIST sin filtro)
      **antes** de `[#18]`.

### P2 — Diseño / deuda / DoS acotado
- [ ] **HUB-7 / AGT-6**: eliminar `<stdio.h>`/`snprintf` de los cores freestanding (hub client_session,
      protocol interface_name, client, mirror); reemplazar por `strnlen`/`memcpy`.
- [ ] **SEC-12/SEC-14/SEC-15/SEC-16**: lockout web por IP+nombre combinado con backoff; igualar timing
      de login; re-chequear authz en el WS de telemetría; escapar XML del beacon.
- [ ] **TST-4**: `create-release` debe `needs:` el job de tests; no publicar sobre rojo.
- [ ] **TST-5/TST-6**: cache key sobre `src/`; correr tests en al menos un arch no-x86_64; cubrir
      teardown client/admin, path reliable del agente, y los non-happy paths de tls_channel; e2e para
      ACLs/pins/cli/mirror.
- [ ] **TST-7** `[#70]`: sustituir sleeps fijos por poll de condición (empezar por fairness); mover
      lógica/estado fuera de `*** Keywords ***` `[relacionar e2e_refactor.md → issue nueva]`.
- [ ] **BLD-1/BLD-2/BLD-3**: keyear el rebuild de OpenSSL por versión; alinear la versión de OpenSSL de
      tests con prod; fallback offline/system-OpenSSL.
- [ ] **PKG-1**: el deb del hub no debe abrir un listener 0.0.0.0 sin opt-in del operador.
- [ ] **PKG-2** `[#19]`: alinear `make install` y los deb (units, conf, completions, usuario) o
      documentar la divergencia; publicar un deb `-dev` para lib+header.
- [ ] **SEC-11**: documentar procedencia + sha256 del cest-runner, idealmente fetch-and-verify o build
      desde fuente `[relacionar #28]`.
- [ ] **API-menores/PY-menores**: NULL-checks de sesión; exponer backpressure de `canhub_send`;
      versionado de structs de salida; macro de export Windows con rama import; consolidar la
      duplicación linux/windows canhub.c.
- [ ] **HIG-1/HIG-2/HIG-3**: añadir `HARDWARE_ESP32_AGENT.md` y `e2e_refactor.md` a `.git/info/exclude`
      (o `.gitignore`); abrir issue para el refactor e2e; decidir si TODO.md se trackea o se referencia
      distinto en CLAUDE.md.

### P3 — Menores / limpieza (agrupar en un barrido)
- [ ] Los ~40 ítems LOW de tech-debt listados por sección (magic numbers, contadores u32, boilerplate
      de codecs duplicado, asserts tautológicos, comentarios stale, validaciones de boundary en
      backoff/pacer/ifconfig/socketcand, escape/validación de nombres, hardening systemd, pins de
      imágenes base por digest, SPDX del brazo comercial). Abordar en un PR de limpieza por área.
- [ ] Retirar o etiquetar como histórico `spike/e0-quic-datagram/` (ya no compila).

---

## Apéndice · Módulos src sin test unitario

Core freestanding / domain (deberían ser testeables, sin test directo): `protocol/wire.c`,
`socketcand/domain/connection_table.c`, `socketcand/socketcand_app.c`, `agent/agent_app.c`,
`hub/hub_app.c`.

Adapters de plataforma (mayormente no testeables en host, sin test): `pinned_server_verifier.c`,
`tls_defaults.c`, `epoll_registry.c`, `cli_meta.c`, todo `quic/*`, `tcp/*`, `tls/*_transport.c`,
`socketcan/*`, `socketcand/*` (linux), `clock.c`, `libcanhub/canhub.c`, todos los `*_main.c`, y
**todo `src/platform/windows/**`** (compila en CI, nunca se testea funcionalmente).

Con cobertura pese a no tener fichero propio (no son gaps): `pin_store.c`, `tls_identity.c`,
`tls_client_security.c`, `tls_server_security.c` (vía test_tls_channel), `peer_directory.c`,
`egress_shaper.c`, `interface_name.c`.
