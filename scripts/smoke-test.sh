#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
rm -rf data
cmake --preset dev >/dev/null
cmake --build --preset dev >/dev/null

read -r CORE_PORT ADMIN_PORT SCENE_PORT < <(python3 - <<'PY'
import socket
ports=[]
for _ in range(3):
    s=socket.socket(socket.AF_INET,socket.SOCK_STREAM)
    s.bind(('127.0.0.1',0))
    ports.append(s.getsockname()[1])
    s.close()
print(*ports)
PY
)
export CORE_PORT ADMIN_PORT SCENE_PORT

CORE_CFG=/tmp/ogl-core-smoke.toml
WORLD_CFG=/tmp/ogl-world-smoke.toml
cat > "$CORE_CFG" <<EOF
[network]
listen_address = "127.0.0.1"
port = $CORE_PORT

[admin]
listen_address = "127.0.0.1"
port = $ADMIN_PORT

[lease]
timeout_seconds = 15

[storage]
worlds = "data/worlds.db"
regions = "data/regions.db"
EOF

cat > "$WORLD_CFG" <<EOF
[node]
id = "world-01"
name = "OpenGenesis World 01"

[network]
public_endpoint = "127.0.0.1:$SCENE_PORT"
scene_listen_address = "127.0.0.1"
scene_port = $SCENE_PORT

[core]
endpoint = "127.0.0.1:$CORE_PORT"
reconnect_seconds = 1
lease_seconds = 2

[runtime]
tick_hz = 45.0
terrain_base_height = 21.0

[regions]
count = 1

[region0]
id = "genesis-central"
name = "Genesis Central"
grid_x = 1000
grid_y = 1000
EOF

./build/dev/opengenesis-core "$CORE_CFG" > /tmp/ogl-core.log 2>&1 & CORE=$!
cleanup(){
  status=$?
  trap - EXIT
  if [ "$status" -ne 0 ]; then
    echo "Smoke ports: core=$CORE_PORT admin=$ADMIN_PORT scene=$SCENE_PORT" >&2
    echo "--- OpenGenesis Core log ---" >&2
    cat /tmp/ogl-core.log 2>/dev/null >&2 || true
    cat /tmp/ogl-core2.log 2>/dev/null >&2 || true
    echo "--- OpenGenesis World log ---" >&2
    cat /tmp/ogl-world.log 2>/dev/null >&2 || true
  fi
  kill "$CORE" 2>/dev/null || true
  kill "${WORLD:-}" 2>/dev/null || true
  rm -f "$CORE_CFG" "$WORLD_CFG"
  exit "$status"
}
trap cleanup EXIT
sleep 0.4
./build/dev/opengenesis-world "$WORLD_CFG" > /tmp/ogl-world.log 2>&1 & WORLD=$!
sleep 1

python3 - <<'PY'
import json, os, socket, struct, time, urllib.request

HOST = '127.0.0.1'
PORT = int(os.environ['SCENE_PORT'])
ADMIN_PORT = int(os.environ['ADMIN_PORT'])

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
for _ in range(40):
    candidate = None
    try:
        candidate = socket.create_connection((HOST, PORT), timeout=2)
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
            try: candidate.close()
            except Exception: pass
        time.sleep(0.25)
if sock is None:
    raise RuntimeError(f'scene endpoint did not become ready: {last_error}')

joined = fields(payload)
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
with urllib.request.urlopen(f'http://127.0.0.1:{ADMIN_PORT}/v1/status') as response:
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
sleep 2
./build/dev/opengenesis-core "$CORE_CFG" > /tmp/ogl-core2.log 2>&1 & CORE=$!
sleep 4
python3 - "$GEN1" "$TICKS1" <<'PY'
import json,os,sys,urllib.request
admin_port=int(os.environ['ADMIN_PORT'])
d=json.load(urllib.request.urlopen(f'http://127.0.0.1:{admin_port}/v1/status'))
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
import json,os,urllib.request
admin_port=int(os.environ['ADMIN_PORT'])
d=json.load(urllib.request.urlopen(f'http://127.0.0.1:{admin_port}/v1/status'))
assert d['regions'][0]['state']=='offline'
PY

echo "OpenGenesisLINK 0.3.0 scene/recovery smoke test: PASS"
