#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
rm -rf data

read -r CORE_PORT ADMIN_PORT SCENE_PORT < <(python3 - <<'PY'
import socket
ports=[]
for _ in range(3):
    s=socket.socket(); s.bind(('127.0.0.1',0)); ports.append(s.getsockname()[1]); s.close()
print(*ports)
PY
)
export CORE_PORT ADMIN_PORT SCENE_PORT

SECRET="crossing-v3-smoke-scene-ticket-secret-0123456789abcdef"
CORE_CFG=/tmp/ogl-core-crossing-v3.toml
WORLD_CFG=/tmp/ogl-world-crossing-v3.toml

cat > "$CORE_CFG" <<EOF_CORE
[network]
listen_address = "127.0.0.1"
port = $CORE_PORT
[admin]
listen_address = "127.0.0.1"
port = $ADMIN_PORT
[lease]
timeout_seconds = 15
[identity]
session_lifetime_seconds = 3600
scene_ticket_lifetime_seconds = 60
[security]
scene_ticket_secret = "$SECRET"
admin_api_key = "crossing-v3-smoke-admin-key-0123456789abcdef"
[storage]
crossings = "data/crossings-v3.db"
scripts = "data/scripts-v3.db"
script_world_actions = "data/script-world-actions-v3.db"
parcels = "data/parcels-v3.db"
moderation = "data/moderation-v3.db"
[scripting]
max_pending_world_actions = 128
[hypergrid]
enabled = false
EOF_CORE

cat > "$WORLD_CFG" <<EOF_WORLD
[node]
id = "crossing-v3-world"
name = "Crossing v3 World"
[network]
public_endpoint = "127.0.0.1:$SCENE_PORT"
scene_listen_address = "127.0.0.1"
scene_port = $SCENE_PORT
[core]
endpoint = "127.0.0.1:$CORE_PORT"
reconnect_seconds = 1
lease_seconds = 2
[security]
scene_ticket_secret = "$SECRET"
[runtime]
tick_hz = 30.0
terrain_base_height = 21.0
[storage]
root = "data/world-crossing-v3"
parcels = "data/parcels-v3.db"
moderation = "data/moderation-v3.db"
save_interval_seconds = 1
[regions]
count = 2
[region0]
id = "crossing-west"
name = "Crossing West"
grid_x = 2000
grid_y = 2000
[region1]
id = "crossing-east"
name = "Crossing East"
grid_x = 2001
grid_y = 2000
EOF_WORLD

./build/dev/opengenesis-core "$CORE_CFG" >/tmp/ogl-crossing-v3-core.log 2>&1 &
CORE=$!
./build/dev/opengenesis-world "$WORLD_CFG" >/tmp/ogl-crossing-v3-world.log 2>&1 &
WORLD=$!

cleanup() {
  status=$?
  kill "$WORLD" "$CORE" 2>/dev/null || true
  wait "$WORLD" 2>/dev/null || true
  wait "$CORE" 2>/dev/null || true
  if [ "$status" -ne 0 ]; then
    echo "--- crossing v3 core ---" >&2
    cat /tmp/ogl-crossing-v3-core.log >&2 || true
    echo "--- crossing v3 world ---" >&2
    cat /tmp/ogl-crossing-v3-world.log >&2 || true
  fi
  exit "$status"
}
trap cleanup EXIT
sleep 3

python3 - <<'PY'
import json,os,urllib.error,urllib.request

A=f"http://127.0.0.1:{os.environ['ADMIN_PORT']}"

def api(path,method='GET',body=None,token=None):
    data=None if body is None else json.dumps(body).encode()
    headers={}
    if data is not None: headers['Content-Type']='application/json'
    if token: headers['Authorization']='Bearer '+token
    req=urllib.request.Request(A+path,data=data,headers=headers,method=method)
    with urllib.request.urlopen(req,timeout=6) as response:
        return response.status,json.loads(response.read())

status,registered=api('/v1/auth/register','POST',{
    'username':'crossing.v3.smoke',
    'display_name':'Crossing v3 Smoke',
    'password':'correct horse battery staple'})
assert status==201
token=registered['token']

status,handoff=api('/v1/viewer/handoff','POST',{
    'from_region':'crossing-west',
    'to_region':'crossing-east',
    'vx':6.0,'vy':1.0,'vz':0.5,
    'rx':0.0,'ry':0.0,'rz':45.0,
    'avx':0.0,'avy':0.0,'avz':0.75},token)
assert status==200
crossing_id=handoff['crossing_id']

status,reserved=api('/v1/viewer/handoff/reserve','POST',{
    'crossing_id':crossing_id,
    'region':'crossing-east'},token)
assert status==200
assert reserved['state']=='reserved'
assert reserved['reservation_token']
assert reserved['rotation']['z']==45.0
assert reserved['angular_velocity']['z']==0.75
assert reserved['physics_state'] is not None

status,reserved_again=api('/v1/viewer/handoff/reserve','POST',{
    'crossing_id':crossing_id,
    'region':'crossing-east'},token)
assert status==200
assert reserved_again['reservation_token']==reserved['reservation_token']

status,rolled=api('/v1/viewer/handoff/rollback','POST',{
    'crossing_id':crossing_id,
    'reason':'destination-scene-rejected'},token)
assert status==200
assert rolled['state']=='rolled_back'
assert rolled['rollback_reason']=='destination-scene-rejected'
assert rolled['rolled_back_unix']>0

status,rolled_again=api('/v1/viewer/handoff/rollback','POST',{
    'crossing_id':crossing_id,
    'reason':'ignored-retry'},token)
assert status==200
assert rolled_again['rollback_reason']=='destination-scene-rejected'

try:
    api('/v1/viewer/handoff/complete','POST',{
        'crossing_id':crossing_id,
        'region':'crossing-east',
        'reservation_token':reserved['reservation_token']},token)
    raise AssertionError('rolled-back crossing committed')
except urllib.error.HTTPError as error:
    assert error.code==409

print('OpenGenesisLINK Crossing v3 reserve/rollback smoke: PASS')
PY
