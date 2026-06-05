# Code style — can-hub

Mandatory conventions for all C code. They apply to `src/` and `test/`.

---

## 1. Naming

| Element | Convention | Example |
|---|---|---|
| Public function | `Module_PascalFunction` | `Registry_AddInterface`, `FrameCodec_Decode` |
| Private function | `camelCase`, `static` | `clampToSafeRange`, `isWithinLimits` |
| Type / struct **entity** (§7) | `typedef` `PascalCase` | `Registry`, `AgentSession` |
| Struct instance | `snake_case` | `Registry registry;` |
| `enum` (tag / members / typedef) | `tname_e` / `kNAME_X` / `TNAME` | see §12 |
| Global variable (private `static` only) | `snake_case` | `static uint32_t last_run_ms;` |
| Local variable | `snake_case` | `uint8_t retry_count;` |
| Constant / macro | `UPPER_SNAKE_CASE` | `#define FRAME_PAYLOAD_MAX 64` |

- Public functions carry the **module prefix** (`Module_`); they are the module API, declared in the `.h`.
- Private functions are **`static`**, no module prefix, **never in the `.h`**.
- **Scalars are scalars.** A bounded value (index, dlc 0-64, port number) is a plain `uint8_t`/`uint16_t`, **not** a one-field struct wrapper nor a value object with a `*_Make` constructor. Range validation lives in guard clauses at the boundary (codec/handlers) and is covered by **unit tests**, not by the type system. `typedef` structs are reserved for real **entities** (registry, session, transport — identity and lifecycle), see §7.

## 2. Language

**Code 100% in English** (identifiers, file names, the few remaining comments). Documentation (`.md`) in English too.

## 3. Types

- **Forbidden** `int`, `long`, `short`, `unsigned`, etc. in our own declarations.
- Use **fixed widths** from `<stdint.h>`: `uint8_t`, `int8_t`, `uint16_t`, `uint32_t`, `int32_t`…
- Booleans: `bool` from `<stdbool.h>`.
- `char` only for text/ASCII, never as a numeric integer.
- **OS boundary exception**: `int` (file descriptors, errno, POSIX return values), `size_t`/`ssize_t` (sizes from the standard library and syscalls) are allowed where the system API imposes them. Convert to fixed-width types as soon as the value enters our domain.

## 4. No magic numbers

Every meaningful literal → named constant (`#define` or `static const`). Exception: `0`, `1` in trivial idioms (indexes, increments).

## 5. Comments

**Avoid them.** A comment explaining *what* a block does is resolved by **extracting a private function** whose name says it.

Acceptable comments: the non-obvious *why* (protocol quirk, kernel workaround), never the *what*.

## 6. Braces

- **`if` / `for` / `while` / `switch`** → **K&R** (brace on the same line):

```c
if (is_enabled) {
    activate();
} else {
    deactivate();
}
```

**`for` loops**: **compact** header — no spaces inside each clause (`i=0`, `i<n`, `i++`), the **three clauses separated** by `; ` (one space after each `;`). Counter declared inside or outside, both fine:

```c
for(i=0; i<interface_count; i++) { ... }
for(uint8_t i=0; i<interface_count; i++) { ... }
```

**No gratuitous `U`**: do not add the `U` suffix to literals or `#define` values "just because" (`0`, `5`, `1000`). Only when actually needed.

- **Functions** → **Allman** (brace on its own line):

```c
void Registry_AddInterface(Registry *self, CanInterface *interface)
{
    ...
}
```

## 7. Structs as "classes"

A `struct` modelling an **entity** (identity + lifecycle) → `typedef` `PascalCase`; its instance `snake_case`; its operations are public functions `Type_Operation(Type *self, …)`. **Entities only** — a bounded scalar is not a struct (§13).

## 8. Visibility — public API always first

In every `.c`:

1. Includes.
2. Private constants / types.
3. **Private function prototypes** (block at the top).
4. **Public function definitions** (the reader sees the API first).
5. **Private function definitions** (end of file).

## 9. Line length and arguments

- **120 columns max recommended** — not strict. Readability beats the limit.
- Call/signature with many arguments → one argument per line, closing parenthesis aligned.

## 10. Indentation and flow — flat happy path

- **Avoid indentation levels** as much as possible.
- Handle the **exceptional path first** with guard clauses (`if (...) return;`) and keep the **happy path unindented**.
- **Declarations on top**: local variables declared at the **start of the function** (C89 style), never inside an `if` or nested block. Declare on top, assign later if needed.

## 11. No column alignment

Do not align assignments, variable declarations, struct members or comments with padding. **One single space** around `=`. Exception: `#define` values may be column-aligned.

## 12. Enums

An `enum` **is not a class** (no §7 `PascalCase`):

- **Tag**: `tname_e` (snake, `t` prefix, `_e` suffix).
- **Members**: `kNAME_VALUE` (`k` prefix + `UPPER_SNAKE`, prefixed with the enum name).
- **Typedef**: `TNAME` (`UPPER_SNAKE`, `T` prefix).
- **Sentinel**: always a trailing `kNAME_MAX` (iteration bound / range validation).

```c
typedef enum ttransport_kind_e {
    kTRANSPORT_KIND_QUIC = 0,
    kTRANSPORT_KIND_TCP = 1,
    kTRANSPORT_KIND_MAX,
} TTRANSPORT_KIND;
```

K&R braces (same as `struct`).

## 13. Scalars vs structs — do not wrap integers

A bounded numeric value is a **plain fixed-width integer**, never a one-field `struct`. Range validation goes in guard clauses (§10) at the boundary and is verified with **unit tests** — no wrappers, no `*_Make` constructors.

## 14. No abbreviations

Identifiers use the **full word**. Do not abbreviate: `request` (not `req`), `config` (not `cfg`), `message` (not `msg`), `buffer` (not `buf`), `index` (not `idx`), `value` (not `val`).

**Exception**: well-known industry acronyms: `IP`, `CAN`, `CRC`, `TLS`, `QUIC`, `MTU`, `ACL`, `app`, `ms` (milliseconds), `id`. `i` as a loop counter is fine.

---

## Quick summary

| Rule | One line |
|---|---|
| Public | `Module_PascalFunction`, in the `.h` |
| Private | `camelCase`, `static`, prototype on top / impl at the end |
| Entity types | `typedef` `PascalCase`; instance `snake_case` |
| Enums | tag `tname_e` · members `kNAME_X` · typedef `TNAME` · `_MAX` sentinel |
| Scalars | plain fixed-width integers, **never** 1-field structs nor `*_Make` |
| Abbreviations | full word; exception: known acronyms (IP/CAN/TLS/app/id) |
| `for` loops | compact `for(i=0; i<n; i++)`, 3 clauses separated by `; ` |
| `U` suffix | only when actually needed |
| Globals | private `static` only, `snake_case` |
| Numeric types | `<stdint.h>` fixed width; `int`/`size_t` only at OS boundaries |
| Comments | avoid → extract a well-named private function |
| Magic numbers | forbidden → named constant |
| Language | 100% English |
| Braces | `if/for/while` K&R · functions Allman |
| Line | 120 cols max recommended · many args → one per line |
| Flow | guards first · flat happy path · avoid nesting |
| Alignment | forbidden (one space around `=`) · exception: `#define` |
