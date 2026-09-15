# Region Lifecycle v0

Status: **Foundation / unstable**

## Ziel

Der Core hält die autoritative Übersicht darüber, welche Region auf welchem World Node liegt, welche Grid-Koordinate belegt ist und welchen Runtime-Zustand die Region aktuell besitzt.

## Zustände

```text
registered
   |
   v
starting ---> error
   |
   v
 online ----> error
   |
   v
stopping ---> error
   |
   v
offline
```

Zusätzlich kann `offline` wieder nach `starting` wechseln. `error` kann über `starting`, `offline` oder eine erneute Registrierung verlassen werden.

## Persistenz

Foundation 0.1.1-dev schreibt zwei atomisch ersetzte Registry-Dateien:

- `data/worlds.registry`
- `data/regions.registry`

Das Format ist intern und nicht als öffentliches API festgeschrieben.

## Crash-Semantik

Ein Prozess-Neustart darf keinen alten `online`-Status übernehmen. Deshalb werden beim Laden der Registry alle transienten Region-Zustände auf `offline` gesetzt. Persistierte World Nodes werden ebenfalls grundsätzlich als offline geladen, bis sie sich erneut registrieren.

## Grid-Kollisionen

Im Foundation-Stand darf pro `(grid_x, grid_y)` nur eine Region registriert sein. Eine zweite Region auf derselben Koordinate wird mit `grid-coordinate-occupied` abgelehnt.

Spätere Megaregion-/Variable-Region-Unterstützung wird eine echte Flächenüberlappungsprüfung benötigen; die jetzige Prüfung betrachtet zunächst den Regionsursprung.
