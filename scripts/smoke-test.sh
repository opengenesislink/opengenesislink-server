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
sleep 0.6
./build/dev/opengenesis-world config/world.toml > /tmp/ogl-world.log 2>&1 & WORLD=$!
sleep 2

python3 - <<'PY'
import json, socket, struct, time, urllib.request
HOST, PORT = '127.0.0.1', 19100

def recv_exact(sock, size):
    out = b''
    while len(out) < size:
        part = sock.recv(size - len(out))
        if not part: raise RuntimeError('scene socket closed')
        out += part
    return out

def send_frame(sock, msg_type, request_id, payload=''):
    data = payload.encode()
    sock.sendall(struct.pack('>4sHHII', b'OGL1', 1, msg_type, request_id, len(data)) + data)

def recv_frame(sock):
    header = recv_exact(sock, 16)
    magic, version, msg_type, request_id, size = struct.unpack('>4sHHII', header)
    assert magic == b'OGL1' and version == 1
    payload = recv_exact(sock, size).decode() if size else ''
    return msg_type, request_id, payload

def fields(payload):
    result = {}
    for line in payload.splitlines():
        if '=' in line:
            key, value = line.split('=', 1); result[key] = value
    return result

sock = socket.create_connection((HOST, PORT), timeout=5)
req = 1
send_frame(sock, 1, req, 'client=ogl-smoke\nprotocol=1\n'); assert recv_frame(sock)[0] == 2
req += 1
send_frame(sock, 100, req, 'region=genesis-central\navatar=Smoke Avatar\nx=128\ny=128\nz=23\n')
t, _, payload = recv_frame(sock); assert t == 101
joined = fields(payload); start_sequence = int(joined['sequence'])
req += 1
send_frame(sock, 102, req, '')
t, _, snapshot = recv_frame(sock); assert t == 103 and 'entity_count=1' in snapshot and '|avatar|Smoke Avatar|' in snapshot
req += 1
send_frame(sock, 110, req, 'name=Smoke Cube\nx=130\ny=128\nz=30\nsx=1\nsy=1\nsz=1\nphysical=true\n')
t, _, payload = recv_frame(sock); assert t == 111; object_id = int(fields(payload)['id'])
req += 1
send_frame(sock, 112, req, f'id={object_id}\nx=132\ny=129\nz=35\n'); assert recv_frame(sock)[0] == 113
req += 1
send_frame(sock, 120, req, 'text=Hello OpenGenesis scene\n')
t, _, payload = recv_frame(sock); assert t == 121 and int(fields(payload)['sequence']) > start_sequence
req += 1
send_frame(sock, 140, req, 'x=128\ny=128\n')
t, _, payload = recv_frame(sock); terrain = fields(payload); assert t == 141 and abs(float(terrain['height']) - 21.0) < 0.001
req += 1
send_frame(sock, 130, req, f'since={start_sequence}\n')
t, _, events = recv_frame(sock); assert t == 131 and 'entity_created' in events and '|chat|' in events
req += 1
send_frame(sock, 102, req, '')
t, _, snapshot = recv_frame(sock); assert t == 103 and 'entity_count=2' in snapshot and '|object|Smoke Cube|' in snapshot

time.sleep(3)
with urllib.request.urlopen('http://127.0.0.1:18080/v1/status') as response: status = json.load(response)
assert status['version'] == '0.3.0'
assert status['world_nodes'][0]['state'] == 'online'
region = status['regions'][0]
assert region['state'] == 'online' and region['ticks'] > 0
assert region['entities'] >= 2 and region['avatars'] >= 1 and region['physics_bodies'] >= 2
assert region['scene_events'] > 0 and region['terrain_revision'] >= 1
open('/tmp/ogl-generation', 'w').write(str(status['world_nodes'][0]['generation']))
open('/tmp/ogl-ticks', 'w').write(str(region['ticks']))
req += 1
send_frame(sock, 114, req, f'id={object_id}\n'); assert recv_frame(sock)[0] == 115
req += 1
send_frame(sock, 42, req, '')
sock.close()
PY

GEN1=$(cat /tmp/ogl-generation)
TICKS1=$(cat /tmp/ogl-ticks)
kill "$CORE"; wait "$CORE" 2>/dev/null || true
sleep 3
./build/dev/opengenesis-core config/core.toml > /tmp/ogl-core2.log 2>&1 & CORE=$!
sleep 5
python3 - "$GEN1" "$TICKS1" <<'PY'
import json,sys,urllib.request
d=json.load(urllib.request.urlopen('http://127.0.0.1:18080/v1/status'))
assert d['world_nodes'][0]['state']=='online'
assert d['world_nodes'][0]['generation']>int(sys.argv[1])
r=d['regions'][0]
assert r['state']=='online' and r['ticks']>int(sys.argv[2]) and r['terrain_revision']>=1
PY
kill "$WORLD"; wait "$WORLD" 2>/dev/null || true
sleep 1
python3 - <<'PY'
import json,urllib.request
d=json.load(urllib.request.urlopen('http://127.0.0.1:18080/v1/status'))
assert d['regions'][0]['state']=='offline'
PY
echo "OpenGenesisLINK 0.3.0 scene/recovery smoke test: PASS"
