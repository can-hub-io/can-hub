# AEAD bench results — picotls stack

Measures every AEAD engine this build can negotiate, and checks each one against minicrypto
byte for byte. Both questions matter on a new target: how fast the engine is, and whether it
produces what a peer will accept.

## Reading the agreement columns

`one-shot` compares `ptls_aead_encrypt`, which is what QUIC uses. `vectored` compares
`ptls_aead_encrypt_v`, which is what the TLS record layer uses so the content-type byte need
not be copied next to the payload.

`fusion aes128gcm` reporting `NO` under `vectored` is **expected**: upstream leaves
`do_encrypt_v` as an assertion marked FIXME in that engine, which is why it is offered on QUIC
only. A `NO` under `one-shot`, or a `NO` under `vectored` from a non-temporal engine, is a
real defect — that is how the llvm-mingw miscompilation of the 256-bit VAES path was found.

## x86-64 Linux — 2026-09-08

Ubuntu, gcc 13, 8 cores, `-O2`. **Not a quiet host**: a browser at ~20 % CPU and unrelated
containers including one in a crash-restart loop (see #193). Numbers are given as observed
ranges over repeated runs; where the range is wide it is host noise, not engine variance.

An OpenSSL 3.0.13 reference was measured in the same session with the same loop shape, since
"ahead of v0.3.0" has to mean something measured rather than quoted.

| AEAD, one operation | 40 B (CAN frame) | 1200 B (MTU) |
|---|---|---|
| OpenSSL 3.0.13 AES-128-GCM — the reference | 0.182 | 0.284 |
| fusion AES-128-GCM — what QUIC negotiates | **0.025-0.029** | **0.168-0.199** |
| non-temporal AES-128-GCM — what TLS negotiates | **0.038-0.197** | **0.194-0.281** |
| fusion AES-256-GCM — QUIC | 0.028-0.031 | 0.196-0.218 |
| non-temporal AES-256-GCM — TLS | 0.040-0.434 | 0.243-0.676 |
| can-hub CHACHA20-POLY1305 (Monocypher POLY1305) | 0.325 | 2.739 |
| minicrypto CHACHA20-POLY1305 (cifra POLY1305) | 1.03-1.05 | 12.4-13.3 |
| minicrypto AES-128-GCM (cifra, no AES-NI) | 147-161 | 2248-2472 |
| minicrypto AES-256-GCM (cifra, no AES-NI) | 201-214 | 3091-3382 |

Every engine agreed with minicrypto except the two documented `vectored` gaps above.

Reading it: with AES-NI both transports are ahead of the OpenSSL reference — QUIC by about 7x
at CAN frame size, TLS by about 4x at its best and at parity at its worst. The AES-128 rows
are the ones that matter; AES-256 is offered for policies that require a 256-bit key.

cifra's AES is 5 800x slower than the AES-NI path at MTU size. That is the whole reason AES is
offered only where there is a fast AES, and why every target without one stays on CHACHA20.

## Windows x86-64 (llvm-mingw), under wine — 2026-09-08

Not timed; run for agreement only, which is what it found.

| Engine | one-shot | vectored |
|---|---|---|
| fusion AES-128-GCM | match | n/a (upstream FIXME) |
| non-temporal AES-128-GCM, `ptls_fusion_can_aesni256 = 1` | **MISMATCH** | **MISMATCH** |
| non-temporal AES-128-GCM, `ptls_fusion_can_aesni256 = 0` | match | match |

The same source built with gcc on Linux matches in every combination. `cmake/picotls.cmake`
therefore forces the 128-bit path on Windows.

## arm64 — Raspberry Pi 5, 2026-09-08

Cortex-A76, Debian bookworm, gcc, load average 0.37. **This CPU has the ARMv8 crypto
extensions** (`aes pmull sha1 sha2` in `/proc/cpuinfo`), so OpenSSL uses hardware AES here and
this stack does not — the worst case for us, and the right place to look first.

Four consecutive runs of our bench landed within 0.5 %: 0.740-0.744 µs and 6.80-6.85 µs. A
quiet host measures cleanly, which the development machine does not (#193).

| AEAD, one operation | 40 B (CAN frame) | 1200 B (MTU) |
|---|---|---|
| **can-hub CHACHA20-POLY1305** — what an ARM build negotiates | **0.742** | **6.82** |
| minicrypto CHACHA20-POLY1305 (cifra POLY1305) | 2.268 | 30.005 |
| minicrypto AES-128-GCM (cifra) | 257.6 | 3 953 |
| minicrypto AES-256-GCM (cifra) | 357.9 | 5 505 |
| OpenSSL 3.5.4 CHACHA20-POLY1305 | 0.220 | 2.053 |
| OpenSSL 3.5.4 AES-128-GCM (hardware) | 0.349 | 0.843 |
| OpenSSL 3.5.4 AES-256-GCM (hardware) | 0.358 | 0.978 |

OpenSSL figures are `openssl speed -evp <alg> -bytes <n>` converted from bytes/s, because the
target has no OpenSSL headers and nothing was installed on it. That harness does slightly less
per operation than `ptls_aead_encrypt`, so it is a lower bound on OpenSSL's cost — the gap
below is an upper bound on ours.

**Reading it:** against OpenSSL's best on this CPU we are **3.4x slower at CAN frame size** and
**8.1x at MTU**. The Monocypher POLY1305 binding is still what makes it bearable: it is 3.1x
faster than stock minicrypto at 40 B and 4.4x at 1200 B. cifra's AES is unusable, at 5 300x
OpenSSL's hardware AES for a 1200-byte record — which is why AES is never offered here.

**And the absolute numbers matter more than the ratio.** 0.742 µs/frame is 1.35 M frames/s per
core; a saturated 1 Mbit/s CAN bus is about 8 700 frames/s, so one Pi 5 core covers roughly 150
fully loaded buses' worth of AEAD. Being 3.4x behind OpenSSL is not a constraint on any CAN
workload. It would start to matter for a hub aggregating hundreds of buses, or for sustained
bulk transfer at MTU.

**The number that does have a consequence** is cifra's AES at 3 953 µs per 1200-byte record.
RFC 9001 fixes AES-128-GCM for QUIC Initial packets, so a hub on this hardware spends that on
every connection attempt, from any unauthenticated peer: one core is saturated by about 250
Initial packets per second. On an ARM hub, QUIC address validation is not an optimisation.

## Both ARM states on a Neoverse N2 — GitHub CI, every run

`ubuntu-24.04-arm` is a real ARM core — `CPU part 0xd49`, Neoverse N2 — and **it implements
AArch32 at EL0**, which server-class ARM cores often do not. So one free hosted runner times
both execution states on the same silicon, and `ci.yml` does it on every pull request: the
`cross-arm64` job gates on agreement (a `NO` fails the build) and `cross-armhf` runs the
32-bit build for information.

That it executes natively rather than under an emulator is visible in the clock: the same
armv7 binary takes 2.33 µs under `qemu-arm` and 0.584 µs here.

| AEAD, one operation | 40 B | 1200 B |
|---|---|---|
| `armv8 aes128gcm`, arm64 | **0.091** | **0.650** |
| `armv8 aes256gcm`, arm64 | 0.099 | 0.705 |
| `can-hub chacha20poly1305`, arm64 | 0.392 | 3.115 |
| `can-hub chacha20poly1305`, **armv7** | 0.584 | 4.770 |
| `minicrypto chacha20poly1305`, arm64 | 0.918 | 11.363 |
| `minicrypto aes128gcm`, arm64 (cifra) | 148.4 | 2 288 |

Every engine agreed with minicrypto, one-shot and vectored, in both states.

**Two things this settles.** The ARMv8 engine was only ever measured on one Cortex-A76; it
holds on a second microarchitecture, and an N2 is about 1.9x faster than the Pi 5 at frame
size. And the cost of the 32-bit ISA is isolated for the first time: on identical silicon
armv7 ChaCha20 is **1.49x** the arm64 figure at 40 B and **1.53x** at 1200 B, which is the
penalty for the state, not for the chip.

Timings from a shared runner are indicative — it is not a quiet host (#193) — but agreement
is host-independent, and agreement is what gates.

### armv7 against the OpenSSL v0.3.0 shipped with, same core, same state

The reference is Ubuntu's own `openssl` 3.0.13 for armhf, extracted and run through the armhf
loader so the runner's arm64 build is left alone. Same silicon, same execution state, so what
is left is the stack.

| | ours | OpenSSL | |
|---|---|---|---|
| CHACHA20-POLY1305, 40 B | 0.584 | 0.172 | **3.4x** |
| CHACHA20-POLY1305, 1200 B | 4.770 | 1.932 | **2.5x** |
| AES-128-GCM, 40 B | not offered | 0.059 | — |
| AES-128-GCM, 1200 B | not offered | 0.624 | — |
| AES-256-GCM, 1200 B | not offered | 0.736 | — |

**ChaCha20 against ChaCha20 is the honest comparison**, and it misses the 1.5x bar by a wide
margin — 3.4x at frame size. OpenSSL's 32-bit ChaCha20 is NEON, which every armv7 target has,
so that ratio is the one that carries to real hardware.

**The AES rows are a different measurement**, and they are the reason this core flatters
OpenSSL: 0.624 µs for a 1200-byte record is within 4 % of what our own ARMv8 engine does in
*64-bit* mode on the same chip (0.650). OpenSSL is not running bitsliced NEON here — it has
found the ARMv8 crypto extensions in AArch32 state and is using hardware AES. A Cortex-A7 or
A9, which is what most of the armv7 fleet is, has no such instructions, and OpenSSL there
falls back to software.

So this measures the gap on an ARMv8 core running 32-bit code, not on the armv7 fleet. Two
readings follow from it, and only the first transfers:

- Against the same algorithm, we are 2.5-3.4x off, and no part of that is the hardware.
- Against what v0.3.0 would actually negotiate on this core (AES-128-GCM), we are **9.9x**
  slower at frame size, because we offer no AES at all in 32-bit mode. On a core with the
  extensions in AArch32 that gap is real; the fleet's A7s and A9s do not have them, so the
  number there would be smaller and nobody has measured it.

## arm64 under qemu — agreement only, after the engine changed — 2026-09-15

The ARMv8 engine is the one part of this stack that no x86-64 machine compiles,
so a change to it lands unverified unless a target is available. qemu-aarch64
implements AESE, AESMC and PMULL and reports `aes pmull` in HWCAP, which is
enough for the agreement columns — and agreement is the question a key schedule
change raises. Timings below are omitted because they measure qemu.

Run after the key schedule moved from an S-box table to AESE against a zero
round key, and after `halveInGcmOrder` became branchless:

    docker run --rm --platform linux/arm64 -v "$PWD":/work -w /work/spike/aead-bench \
        debian:bookworm-slim sh -c 'apt-get update -qq && apt-get install -y -qq gcc make &&
        make BUILD=/work/build/arm64-native FUSION=0 ARMV8=1 && ./aead_bench'

| Engine | one-shot | vectored |
|---|---|---|
| `armv8 aes128gcm` | match | match |
| `armv8 aes256gcm` | match | match |
| `can-hub chacha20poly1305` | match | match |

`BUILD` points at an aarch64 build tree for the picotls and Monocypher archives
only; `tls_aead.c` and `tls_aes_armv8.c` are compiled from source by the bench,
so the tree may predate the change under test.

This does not replace a measurement on real hardware — the numbers in the Pi 5
section above still stand for speed, and nothing here re-measures them.

## armv7 — correct, speed not measured — 2026-09-15

**Agreement only** — emulated timings measure qemu, not the target.

Cross-built on an x86-64 container and run under an explicit `qemu-arm`, which needs no
binfmt handler on the host (a `--platform linux/arm/v7` container does, and fails with
`exec format error` when only `qemu-aarch64` is registered):

    docker run --rm --platform linux/amd64 -v "$PWD":/work -w /work/spike/aead-bench \
        debian:bookworm-slim sh -c 'apt-get update -qq &&
        apt-get install -y -qq gcc-arm-linux-gnueabihf make qemu-user &&
        make CC=arm-linux-gnueabihf-gcc BUILD=/work/build/armhf/release FUSION=0 &&
        qemu-arm -L /usr/arm-linux-gnueabihf ./aead_bench'

| Engine | one-shot | vectored |
|---|---|---|
| `can-hub chacha20poly1305` — the only suite an armv7 build offers | match | match |
| `minicrypto chacha20poly1305` | reference | reference |
| `minicrypto aes128gcm` | reference | reference |
| `minicrypto aes256gcm` | reference | reference |

cifra's AES is built and compared but never offered, the same as on every target without a
hardware AES.

The run opens with `built without a hardware aes engine`, which is the other half of what
this checks: an armv7 build must not pull in the ARMv8 engine, whose A64 intrinsics do not
port and whose `-march=armv8-a+crypto` a 32-bit compiler rejects. The engine is gated on the
compiler's own `__aarch64__` rather than on `CMAKE_SYSTEM_PROCESSOR` precisely so this holds
where uname cannot be trusted — the armv7l release-wheel container runs on an arm64 host:

| Compiler driving the build | Probe result |
|---|---|
| `arm-linux-gnueabihf-gcc` | not aarch64 — ARMv8 engine off |
| `aarch64-linux-gnu-gcc` | aarch64 — ARMv8 engine on |
| host `gcc` (x86-64) | not aarch64 — ARMv8 engine off |

### Measuring armv7 for real

A Cortex-A76 implements AArch32 at EL0, so a Raspberry Pi 5 times both execution states on
one core — `spike/aead-bench/run-on-pi.sh` builds and runs each. The bench links statically,
so no armhf runtime libraries have to be installed on the target.

That answers what the 32-bit ISA costs, not what the armv7 fleet does: an A76 running
32-bit code is still an A76. The figure that carries over to an A7 or an A53 is the **ratio**
against OpenSSL measured on the same core, which is why the script runs both.
