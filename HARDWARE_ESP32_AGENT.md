# Agente ESP32 CAN-over-network — nota de diseño

> Documento interno, untracked, no publicar. Idea de producto/hardware.
> Fecha: 2026-06-14.

## 1. Concepto

Placa pequeña = **agente can-hub físico, plug-and-play**: ESP32 + transceiver CAN
aislado + clavija RJ45 macho. Se enchufa al bus CAN (VE.Can de Victron, NMEA2000,
CANopen…), se alimenta del propio bus, levanta WiFi, conecta al hub y exporta el
bus. Convierte "el core del agente compila en microcontrolador" en una **demo
física** — diferenciador único frente a toda la competencia (cannelloni,
socketcand, Vector…), todos atados a un PC/gateway.

Mapea a las issues #12 (MCU agent) y #24 (agent-lite).

## 2. Diagrama de bloques

```
   RJ45 (8 pines)
      │
      ├─ NET power (~12V) ──► DC-DC aislado 12V→3V3 ──► ESP32 + lado lógico
      │
      ├─ CAN-H / CAN-L ────► transceiver CAN AISLADO ──► TWAI (ESP32)
      │                         (ISO1050 / similar)
      ├─ GND (NET-C) ───────► ref. lado bus
      │
      └─ [front-end de sense: divisores + ADC scan]  (auto-descubrimiento)

   ESP32 ── WiFi ──► hub (TCP/TLS, mTLS)
```

## 3. Componentes (BOM nivel alto)

| Bloque | Elección | Notas |
|--------|----------|-------|
| MCU | **ESP32-S3 + PSRAM** | TWAI nativo + WiFi. PSRAM para buffers QUIC + estado TLS1.3 (WROOM 520KB va justo para QUIC) |
| Transceiver | **ISO1050 / ISO1042 (aislado)** | Aislamiento galvánico obligatorio: te enchufas a equipo caro |
| Alimentación | DC-DC **aislado** 12V→3V3 (p.ej. serie isolated o flyback) | Alimentar del bus = plug-and-play real (un cable) |
| Conector | RJ45 macho + latiguillo, o jack RJ45 + cable | Ver §6 (pinout) |
| Protección | TVS en CAN-H/L, clamp/serie en pines de sense, fusible en NET power | Bus de batería = transitorios |
| Terminación | 120Ω **conmutable** (jumper/MOSFET) | Ver §5 |
| Sense (opcional) | divisores + clamp por pin → ADC/mux | Ver §7 |

## 4. Alimentación

**No depender del RJ45**: no todos los buses CAN-RJ45 llevan power (VE.Can/NMEA2000
a veces sí, CANopen no), el presupuesto NET (LEN) es limitado y los picos WiFi del
ESP32 (300-500mA) son golosos.

Fuentes, rankeadas:

| Fuente | Uso | Veredicto |
|--------|-----|-----------|
| **DC-DC aislado wide-input (8-60V)** al DC del sistema (batería 12/24/48V) | Permanente | **Mejor** — en setup Victron/solar siempre hay DC cerca |
| **USB-C** (cargador/powerbank) | Cerca de USB | Universal, barato |
| **Batería (18650/LiPo)** | Diagnóstico portátil | Solo sesiones cortas |
| RJ45 NET power | Si el bus lo da | **Fallback oportunista**, no primaria |

**Batería**: WiFi siempre on (el agente exporta continuo, no deep-sleep) ≈ 100-160mA
medios → 18650 (3000mAh) ≈ **20-30h**. Sirve para **herramienta de diagnóstico
portátil**, no para agente permanente.

**Diode-OR + auto-detect**: el scan DC (§7) ya mide si hay ~12V en el RJ45.
Entradas en diode-OR (NET-power-bus ∥ DC-jack/USB-C): si hay NET power y presupuesto
OK → úsalo (un cable); si no → exige DC externo. Mejor de ambos mundos.

**Dos SKUs probables**: agente fijo (DC cableado del sistema) vs herramienta
portátil (batería/USB-C). Alimentar aparte rompe el "un solo cable", aceptable en
instalación fija.

## 5. Terminación

CAN necesita **120Ω en los dos extremos** del bus. La placa puede ser:
- **nodo final** → lleva 120Ω,
- **tap intermedio** → NO debe terminar.

→ terminador **conmutable** (jumper físico v1; MOSFET/switch controlado por
firmware v2, decidible tras el auto-descubrimiento). Terminar de más mata el bus.

## 6. ⚠️ El RJ45 — la trampa grande

**No hay un único pinout CAN-sobre-RJ45.** VE.Can/NMEA2000, CANopen (CiA 303-1) y
otros ponen CAN-H/CAN-L/GND/power en **pines distintos e incompatibles**. Meterlo
mal = freír equipo.

- **Verificar el pinout oficial de Victron VE.Can** contra su documentación antes
  de rutar la PCB. No fiarse de memoria.
- Plug-and-play **entre estándares distintos** es justo lo difícil. Realista:
  - **v1**: targetear **un** estándar (VE.Can/NMEA2000) con pinout fijo.
  - auto-descubrimiento (§7) = la ambición "adaptador universal", más cara/arriesgada.

## 7. Auto-descubrir CAN-H / CAN-L / GND — análisis

Pregunta: ¿puede la placa detectar sola qué pin es qué? **Parcialmente sí.**

### 7.1 Qué se puede medir (scan DC pasivo) — FÁCIL y robusto
Niveles en reposo de un bus CAN activo y alimentado:
- **NET power**: ~12V
- **GND (NET-C)**: ~0V
- **CAN-H / CAN-L (recesivo)**: ambos ≈ **2.5V** (sesgo de modo común)
- **sin usar**: flotante / ruido

Front-end: cada pin → **resistencia serie + divisor + clamp a 3V3** → ADC del
ESP32 (directo, o vía mux analógico + 1 ADC). 12V satura el clamp ("pin de
potencia"), 2.5V pasa ("candidato CAN"), 0V ("GND"), flotante ("sin usar").
→ **Clasificación pin→tipo factible y barata.** Los dos pines ~2.5V = el par CAN.

### 7.2 Polaridad H vs L — FÁCIL por ensayo
Si intercambias CAN-H/CAN-L, el receptor diferencial lee invertido → **tramas
inválidas / tormenta de errores**. Detectable:
1. TWAI en **listen-only**, 250k (VE.Can) primero.
2. ¿Tramas válidas sin errores? → polaridad+par+baud correctos.
3. ¿Errores? → swap de polaridad y reintenta.
4. Nada → prueba otro emparejamiento / otro baudrate.

### 7.3 Rutado de pines — el punto CARO
Para conectar "cualquier pin" al CANH/CANL del transceiver hace falta
**conmutación**:
- **Mux/crosspoint analógico** (ADG/MAX): barato/compacto, pero su Ron (~100Ω) y
  Coff degradan la integridad de señal CAN. A 250k es tolerable, a 1M dudoso.
- **Relés** (DPDT para swap de polaridad + matriz para selección): eléctricamente
  limpios (~0Ω), pero voluminosos, lentos y con clic. **Recomendado para la
  conexión final** una vez identificado; mux solo para el sense.

### 7.4 Veredicto
- **Scan DC (power/GND/par-CAN)** + **polaridad por listen-only**: factible, robusto.
- **Auto-rutado universal**: posible pero añade mux/relés, coste y riesgo de SI.
- **Punto medio recomendado (v1)**: pinout VE.Can **fijo** (sin rutado) + **scan DC
  de seguridad**: si el patrón no cuadra con VE.Can esperado, **no energiza el
  transceiver y avisa** (LED/estado). Red de seguridad barata contra enchufar al
  estándar equivocado, sin mux ni relés. El auto-rutado completo = v2.

## 8. Firmware

**QUIC es la tesis del proyecto** → el agente MCU lo mantiene end-to-end. No
fallback TCP.

- El **core del agente y el protocolo compilan** (freestanding). Solo falta el
  adaptador de plataforma.
- Lo pesado no es QUIC, es **OpenSSL 3.5 static** (varios MB, no entra). Se cambia
  el **backend crypto**, no el transporte.
- **Stack**: **ngtcp2** (la *misma* lib que el hub → interop sin sorpresas) +
  **wolfSSL** (ngtcp2 tiene backend wolfSSL; wolfSSL soporta ESP-IDF + accel AES
  por HW; ED25519 + QUIC TLS API). I/O por **lwIP UDP**.
- **Superficie de port acotada**: el transporte QUIC del repo ya está separado del
  crypto. Reusables tal cual: `quic_egress.c`, `quic_udp_endpoint.c`,
  `quic_control_channel.c`, máquina ngtcp2. Solo hay que **reimplementar
  `quic_client_security.c`** (callbacks ngtcp2_crypto) **sobre wolfSSL**.
- Identidad: ED25519 mTLS + TOFU pinning en NVS/flash, igual que el agente Linux.
- Nuevo `src/platform/esp32/`: `CanPort` sobre **TWAI** + `TransportPort` ngtcp2 +
  wolfSSL sobre lwIP. Dominio, `src/protocol/` y la mecánica QUIC se reutilizan.
- **Coste real**: empaquetar ngtcp2+wolfSSL como componente ESP-IDF + reescribir
  `quic_client_security` = el ítem de ingeniería gordo, pero **acotado** (una pieza).
- Identidad: ED25519 en NVS/flash, fingerprint estable (como el agente Linux).

## 9. Provisioning — el reto real de UX

"Plug and play" se rompe aquí: ¿cómo recibe **WiFi creds + URL del hub +
enrolamiento de identidad**?
- **BLE provisioning** (ESP-IDF) o **captive portal** (AP temporal). SmartConfig es
  frágil, evitar.
- Flujo: enchufar → AP/BLE → app/web mete WiFi+hub → fingerprint se muestra para
  `can-hub-cli pins add`. Una vez pineado, reconecta solo.

## 10. Compatibilidad de protocolo

Protocolo **v0 sin congelar** — el firmware ESP32 debe ir a la **misma versión que
el hub**. Un agente embebido en campo no se reflashea fácil → esto **refuerza el
caso de #15** (version negotiation + capability bits en HELLO) antes de meter
hardware en producción.

## 11. Riesgos

- Pinout equivocado → equipo frito (§6). Mitiga: scan DC de seguridad (§7.4).
- Aislamiento insuficiente → daño cruzado bus↔WiFi. Mitiga: transceiver + DC-DC
  aislados.
- Terminación mal → bus caído. Mitiga: conmutable.
- QUIC en ESP32 → inviable. Mitiga: TCP/TLS.
- Firmware sin actualizar vs hub nuevo → incompatibilidad wire. Mitiga: #15 + OTA.

## 12. Plan por fases

0. **Spike transporte** (independiente del HW): empaquetar ngtcp2+wolfSSL como
   componente ESP-IDF y reimplementar `quic_client_security` sobre wolfSSL.
   Validar handshake QUIC + mTLS ED25519 contra el hub desde un ESP32-S3. Es el
   ítem de riesgo; aislarlo primero.
1. **PoC mesa**: devkit ESP32-S3 + transceiver aislado breadboard + RJ45 VE.Can.
   Valida: pinout real Victron, TWAI listen-only @250k, CanPort sobre TWAI +
   transporte QUIC del spike. Sin PCB.
2. **PCB v1**: pinout VE.Can fijo, alimentado del bus, terminación conmutable,
   scan DC de seguridad (avisa si no es VE.Can). Provisioning BLE. OTA.
3. **PCB v2 ("universal")**: auto-rutado (mux sense + relés), auto-baud, soporte
   multi-estándar. Solo si v1 valida el caso.

## 13. Estimación de coste

Estimaciones (€, orientativas). Driver de coste = **aislamiento** (transceiver +
DC-DC aislados ≈ 5-8€ del total).

### BOM v1 (pinout fijo, DC + diode-OR, sin mux)
| Parte | Proto (q5-10) | Volumen (q500-1k) |
|-------|---------------|-------------------|
| ESP32 módulo (WROOM/C3) | 3-4 | 1.8-2.5 |
| Transceiver **aislado** (ISO1042/1050) | 3-4 | 2-3 |
| DC-DC aislado lado bus | 2-3 | 1.5-2 |
| DC-DC wide-input 8-60V→3V3 | 2-3 | 1-2 |
| RJ45 + protección (TVS) + term conmutable | 2-3 | 1-2 |
| Sense front-end (divisores+clamp) | 1-2 | 0.5-1 |
| PCB + ensamblado | 5-10 | 2-4 |
| Caja | 2-5 | 1.5-3 |
| **Total/unidad** | **~25-45 €** | **~12-20 € COGS** |

NRE (una vez): PCB/stencil/setup ~50-150€; firmware = tiempo; **certificación** el
gran salto si se vende (CE/FCC). **Usar módulo ESP32 pre-certificado** (cert modular
de radio) ahorra la re-cert del radio; aún así EMC del producto ~1-5k€ idealmente.

### Palancas de reducción
- **Quitar aislamiento** (TJA1051 no aislado ~0.5€) → ahorra 5-8€, pero arriesgado
  con equipo Victron caro. Tradeoff fuerte.
- ESP32-C3 < WROOM. Sin mux en v1. Sin caja (módulo desnudo) para diagnóstico.

### Precio venta orientativo
2-3× COGS → **agente fijo ~50-80€**, **herramienta diagnóstico ~60-100€**.
Para PoC en mesa: **devkit ESP32 (~8€) + breakout transceiver aislado (~10€) +
latiguillo RJ45** = **~20-30€**, cero PCB.
