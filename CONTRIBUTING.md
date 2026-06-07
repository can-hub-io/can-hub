# Contributing

Thanks for considering a contribution.

## License of contributions (CLA)

can-hub is dual-licensed (AGPL-3.0 + commercial, see LICENSE and
LICENSE.commercial). To keep that model possible, every external
contribution requires a Contributor License Agreement granting the project
maintainer the right to license the contribution under both licenses. You
keep the copyright of your work and may use it however you want; the CLA
only grants the project the additional rights it needs.

The signing flow is automated on the first pull request. No CLA, no merge —
sorry, it protects the project's ability to exist.

## Code style

`CODE_STYLE.md` is mandatory and enforced in review. Read it before writing
C: public-API-first file layout, fixed-width types only, guard clauses, no
comments (names carry intent), no abbreviations.

## Tests

`make test` must pass. New code comes with CEST tests registered in
`test/unit/CMakeLists.txt`; each test links the real sources it exercises.

## Design

doc/design.md and doc/protocol.md are the source of truth. Wire or
architecture changes start as a design discussion (issue), not as a PR.
