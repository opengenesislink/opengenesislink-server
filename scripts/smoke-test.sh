#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
rm -rf data
cmake --preset dev >/dev/null
cmake --build --preset dev >/dev/null
./build/dev/opengenesis-core config/core.toml > /tmp/ogl-core.log 2>&1 & CORE=$!
cleanup(){
  status=$?
  trap - EXIT
  if [ "$status" -ne 0 ]; then
    echo "--- OpenGenesis Core log ---" >&2
    cat /tmp/ogl-core.log 2>/dev/null >&2 || true
    cat /tmp/ogl-core2.log 2>/dev/null >&2 || true
    echo "--- OpenGenesis World log ---" >&2
    cat /tmp/ogl-world.log 2>/dev/null >&2 || true
  fi
  kill "$CORE" 2>/dev/null || true
  kill "${WORLD:-}" 2>/dev/null || true
  exit "$status"
}
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
        if not part:
            raise RuntimeError('scene socket closed')
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
            key, value = line.split('=', 1)
            result[key] = value
    return result

sock = None
payload = ''
last_error = None
for attempt in range(20):
    candidate = None
    try:
        candidate = socket.create_connection((HOST, PORT), timeout=5)
        send_frame(candidate, 1, 1, 'client=ogl-smoke\nprotocol=1\n')
        if recv_frame(candidate)[0] != 2:
            raise RuntimeError('scene HELLO rejected')
        send_frame(candidate, 100, 2, 'region=genesis-central\navatar=Smoke Avatar\nx=128\ny=128\nz=23\n')
        t, _, payload = recv_frame(candidate)
        if t != 101:
            raise RuntimeError(f'scene join rejected: {t} {payload!r}')
        sock = candidate
        break
    except (OSError, RuntimeError) as error:
        last_error = error
        if candidate is not None:
            try:
                candidate.close()
            except Exception:
                pass
        time.sleep(0.25)
if sock is None:
    raise RuntimeError(f'scene endpoint did not become ready: {last_error}')
joined = fields(payload)
avatar_id = int(joined['avatar_id'])
start_sequence = int(joined['sequence'])
req = 2

req += 1
send_frame(sock, 102, req, '')
t, _, snapshot = recv_frame(sock)
assert t == 103 and 'entity_count=1' in snapshot and '|avatar|Smoke Avatar|' in snapshot

req += 1
send_frame(sock, 110, req, 'name=Smoke Cube\nx=130\ny=128\nz=30\nsx=1\nsy=1\nsz=1\nphysical=true\n')
t, _, payload = recv_frame(sock)
assert t == 111
object_id = int(fields(payload)['id'])

req += 1
send_frame(sock, 112, req, f'id={object_id}\nx=132\ny=129\nz=35\n')
assert recv_frame(sock)[0] == 113

req += 1
send_frame(sock, 120, req, 'text=Hello OpenGenesis scene\n')
t, _, payload = recv_frame(sock)
assert t == 121 and int(fields(payload)['sequence']) > start_sequence

req += 1
send_frame(sock, 140, req, 'x=128\ny=128\n')
t, _, payload = recv_frame(sock)
terrain = fields(payload)
assert t == 141 and abs(float(terrain['height']) - 21.0) < 0.001

req += 1
send_frame(sock, 130, req, f'since={start_sequence}\n')
t, _, events = recv_frame(sock)
assert t == 131 and 'entity_created' in events and '|chat|' in events

req += 1
send_frame(sock, 102, req, '')
t, _, snapshot = recv_frame(sock)
assert t == 103 and 'entity_count=2' in snapshot and '|object|Smoke Cube|' in snapshot

time.sleep(3)
with urllib.request.urlopen('http://127.0.0.1:18080/v1/status') as response:
    status = json.load(response)
assert status['version'] == '0.3.0'
assert status['world_nodes'][0]['state'] == 'online'
region = status['regions'][0]
assert region['state'] == 'online'
assert region['ticks'] > 0
assert region['entities'] >= 2
assert region['avatars'] >= 1
assert region['physics_bodies'] >= 2
assert region['scene_events'] > 0
assert region['terrain_revision'] >= 1
open('/tmp/ogl-generation', 'w').write(str(status['world_nodes'][0]['generation']))
open('/tmp/ogl-ticks', 'w').write(str(region['ticks']))

req += 1
send_frame(sock, 114, req, f'id={object_id}\n')
assert recv_frame(sock)[0] == 115
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
assert r['state']=='online'
assert r['ticks']>int(sys.argv[2]), (r['ticks'], sys.argv[2])
assert r['terrain_revision']>=1
PY

kill "$WORLD"; wait "$WORLD" 2>/dev/null || true
sleep 1
python3 - <<'PY'
import json,urllib.request
d=json.load(urllib.request.urlopen('http://127.0.0.1:18080/v1/status'))
assert d['regions'][0]['state']=='offline'
PY

echo "OpenGenesisLINK 0.3.0 scene/recovery smoke test: PASS"
