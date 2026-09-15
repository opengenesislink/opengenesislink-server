# OpenGenesisLINK – Entwicklungsbasis

> **OpenGenesisLINK** ist eine eigenständige, moderne Plattform für föderierte virtuelle Welten.

Dieses Paket beschreibt den finalisierten Konzeptstand für die langfristige Neuentwicklung von OpenGenesisLINK.

Wichtig: **NexVerse ist nicht die Plattform selbst**, sondern eine konkrete virtuelle Welt, die später auf OpenGenesisLINK betrieben werden kann.

## Zielhorizont

Geplant ist eine Entwicklung über etwa **24 Monate inklusive intensiver Testphasen**.

## Kernziele

- vollständig eigene Plattformarchitektur
- klare Trennung von Core, World Nodes und externen Diensten
- eigene Physik-Engine
- eigenes Federation-Protokoll
- OpenSimulator-Hypergrid-Kompatibilität über Bridge
- globales Atlas-/Map-System
- eigenes, einfaches Addon-System
- eigener nativer Viewer
- Legacy-Viewer-Kompatibilität als Übergang
- modulare Datenhaltung
- saubere, verständliche Konfiguration
- offene, dokumentierbare Schnittstellen

## Leitprinzipien

1. OpenGenesisLINK darf intern nicht von OpenSimulator abhängig sein.
2. Legacy-Kompatibilität ist immer ein Adapter.
3. Konfiguration muss verständlich und zentral organisiert sein.
4. Addons sollen auch für Anfänger einfach entwickelbar sein.
5. Federation muss sicher, versioniert und dokumentiert sein.
6. Die globale Karte darf nur ausdrücklich veröffentlichte Regionen zeigen.
7. Physik soll als eigenständige Engine und klarer Subsystem-Vertrag entwickelt werden.

## Technische Basis

OpenGenesisLINK wird nativ in **C++23** entwickelt. Die erste Foundation-Implementierung ist Bestandteil dieses Pakets und enthält bereits einen kompilierbaren Core, World Node, OGL-Wire-Foundation-Handshake, Registry, Konfiguration und Tests.

Schnellstart:

```bash
./scripts/build.sh
./scripts/smoke-test.sh
```

Details: `19-CXX23-TECHNIKBASIS.md`, `20-IMPLEMENTIERUNGSSTAND.md` und `docs/OGL-WIRE-FOUNDATION-v0.md`.
