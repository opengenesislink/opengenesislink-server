# OpenGenesisLINK 1.0.0-alpha.1 — First Client Development Release

## Goal

This release establishes the first supported Server baseline for parallel development of the OpenGenesisLINK Viewer and OpenGenesisLINK Atlas.

It is intentionally an alpha. The release gate is contract coherence, cross-platform build/test health and client-development readiness — not completion of every long-term simulator feature.

## New release-level work

- semantic release label `1.0.0-alpha.1`
- machine-readable `ogl-release-v1` contract at `GET /v1/release`
- first dedicated `ogl-atlas-v1` contract
- Atlas bootstrap, Region list and Region detail endpoints
- explicit alpha compatibility rules
- CPack packaging foundation for Windows ZIP, Unix TGZ and Debian DEB
- integrated smoke coverage for release discovery and Atlas v1
- canonical Viewer handover for a separate Viewer repository
- canonical Atlas handover for a separate Atlas repository

## Client development baseline

Viewer:

`docs/VIEWER-HANDOVER-1.0.0-alpha.1.md`

Atlas:

`docs/ATLAS-HANDOVER-1.0.0-alpha.1.md`

Shared release compatibility:

`docs/RELEASE-CONTRACT-1.0.md`

## Known non-blocking gaps

- LSL compatibility remains partial
- advanced arbitrary mesh collision remains roadmap work
- imported mesh/material/animation content pipeline is not final
- final low-latency Scene transport is not frozen
- final Avatar controller/prediction is not frozen
- Atlas place/category/global-index contracts are future extensions
- Voice service is a separate future project

These gaps do not prevent beginning the first-party Viewer and Atlas implementations against the frozen alpha contracts.
