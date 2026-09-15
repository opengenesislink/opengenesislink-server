#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
rm -rf data
cmake --preset dev >/dev/null
cmake --build --preset dev >/dev/null
./build/dev/opengenesis-core config/core.toml > /tmp/ogl-core.log 2>&1 & CORE=$!
cleanup(){ kill "$CORE" 2>/dev/null || true; kill "${WORLD:-}" 2>/dev/null || true; }
trap cleanup EXIT
sleep 0.5
./build/dev/opengenesis-world config/world.toml > /tmp/ogl-world.log 2>&1 & WORLD=$!
sleep 3
python3 - <<'PY'
import json, urllib.request
with urllib.request.urlopen('http://127.0.0.1:18080/v1/status') as r:
    d=json.load(r)
assert d['version']=='0.2.0'
assert d['world_nodes'][0]['state']=='online'
assert d['regions'][0]['state']=='online'
assert d['regions'][0]['ticks']>0
assert d['regions'][0]['entities']>=2
assert d['regions'][0]['avatars']>=1
assert d['regions'][0]['physics_bodies']>=2
PY
GEN1=$(python3 - <<'PY'
import json,urllib.request
print(json.load(urllib.request.urlopen('http://127.0.0.1:18080/v1/status'))['world_nodes'][0]['generation'])
PY
)
kill "$CORE"; wait "$CORE" 2>/dev/null || true
sleep 3
./build/dev/opengenesis-core config/core.toml > /tmp/ogl-core2.log 2>&1 & CORE=$!
sleep 5
python3 - "$GEN1" <<'PY'
import json,sys,urllib.request
d=json.load(urllib.request.urlopen('http://127.0.0.1:18080/v1/status'))
assert d['world_nodes'][0]['state']=='online'
assert d['world_nodes'][0]['generation']>int(sys.argv[1])
assert d['regions'][0]['state']=='online'
PY
kill "$WORLD"; wait "$WORLD" 2>/dev/null || true
sleep 1
python3 - <<'PY'
import json,urllib.request
d=json.load(urllib.request.urlopen('http://127.0.0.1:18080/v1/status'))
assert d['regions'][0]['state']=='offline'
PY
echo "OpenGenesisLINK recovery smoke test: PASS"
