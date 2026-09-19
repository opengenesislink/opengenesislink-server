#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
rm -rf data/crossing-v3-smoke

read -r CORE_PORT ADMIN_PORT SCENE_PORT < <(python3 - <<'PY'
import socket
ports=[]
for _ in range(3):
    s=socket.socket(); s.bind(('127.0.0.1',0)); ports.append(s.getsockname()[1]); s.close()
print(*ports)
PY
)
export CORE_PORT ADMIN_PORT SCENE_PORT

SECRET="crossing-v3-smoke-scene-secret-0123456789abcdef"
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
admin_api_key = "crossing-v3-admin-key-0123456789abcdef"
[storage]
crossings = "data/crossing-v3-smoke/crossings.db"
scripts = "data/crossing-v3-smoke/scripts.db"
script_world_actions = "data/crossing-v3-smoke/script-world-actions.db"
[hypergrid]
enabled = false
EOF_CORE

cat > "$WORLD_CFG" <<EOF_WORLD
[node]
id = "crossing-world"
name = "Crossing World"
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
root = "data/crossing-v3-smoke/world"
parcels = "data/crossing-v3-smoke/parcels.db"
moderation = "data/crossing-v3-smoke/moderation.db"
save_interval_seconds = 1
[regions]
count = 2
[region0]
id = "crossing-west"
name = "Crossing West"
grid_x = 1000
grid_y = 1000
[region1]
id = "crossing-east"
name = "Crossing East"
grid_x = 1001
grid_y = 1000
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
    echo "--- crossing core ---" >&2
    cat /tmp/ogl-crossing-v3-core.log >&2 || true
    echo "--- crossing world ---" >&2
    cat /tmp/ogl-crossing-v3-world.log >&2 || true
  fi
  exit "$status"
}
trap cleanup EXIT
sleep 3

python3 - <<'PY'
import json,os,urllib.error,urllib.request

ADMIN=f"http://127.0.0.1:{os.environ['ADMIN_PORT']}"

def api(path,method='GET',body=None,token=None,expected=None):
    data=None if body is None else json.dumps(body).encode()
    headers={}
    if data is not None: headers['Content-Type']='application/json'
    if token: headers['Authorization']='Bearer '+token
    req=urllib.request.Request(ADMIN+path,data=data,headers=headers,method=method)
    try:
        with urllib.request.urlopen(req,timeout=6) as response:
            raw=response.read()
            result=json.loads(raw) if raw else {}
            if expected is not None: assert response.status==expected,(response.status,result)
            return response.status,result
    except urllib.error.HTTPError as error:
        raw=error.read()
        result=json.loads(raw) if raw else {}
        if expected is not None:
            assert error.code==expected,(error.code,result)
            return error.code,result
        raise

status,user=api('/v1/auth/register','POST',{
    'username':'crossing.v3.smoke',
    'display_name':'Crossing V3 Smoke',
    'password':'correct horse battery staple'},expected=201)
token=user['token']

handoff_body={
    'from_region':'crossing-west',
    'to_region':'crossing-east',
    'vx':4.0,'vy':1.0,'vz':0.5,
    'rx':5.0,'ry':10.0,'rz':15.0,
    'avx':0.1,'avy':0.2,'avz':0.3
}
_,handoff=api('/v1/viewer/handoff','POST',handoff_body,token,200)
crossing_id=handoff['crossing_id']
assert crossing_id

_,reserved=api('/v1/viewer/handoff/reserve','POST',{
    'crossing_id':crossing_id,
    'region':'crossing-east'},token,200)
assert reserved['state']=='reserved',reserved
reservation_token=reserved['reservation_token']
assert len(reservation_token)>=32,reserved
assert reserved['rotation']['z']==15.0,reserved
assert reserved['angular_velocity']['z']==0.3,reserved
assert reserved['physical'] is True,reserved

status,bad=api('/v1/viewer/handoff/complete','POST',{
    'crossing_id':crossing_id,
    'region':'crossing-east',
    'reservation_token':'wrong-token'},token,409)
assert bad['error']=='crossing-reservation-token-invalid',bad

_,completed=api('/v1/viewer/handoff/complete','POST',{
    'crossing_id':crossing_id,
    'region':'crossing-east',
    'reservation_token':reservation_token},token,200)
assert completed['state']=='completed' and completed['completed_unix']>0,completed

_,handoff2=api('/v1/viewer/handoff','POST',{
    'from_region':'crossing-west',
    'to_region':'crossing-east',
    'vx':2.0},token,200)
crossing2=handoff2['crossing_id']

_,reserved2=api('/v1/viewer/handoff/reserve','POST',{
    'crossing_id':crossing2,
    'region':'crossing-east'},token,200)
assert reserved2['state']=='reserved'

_,rolled=api('/v1/viewer/handoff/rollback','POST',{
    'crossing_id':crossing2,
    'reason':'destination-import-failed'},token,200)
assert rolled['state']=='rolled_back',rolled
assert rolled['rollback_reason']=='destination-import-failed',rolled
assert rolled['rolled_back_unix']>0,rolled

status,rejected=api('/v1/viewer/handoff/complete','POST',{
    'crossing_id':crossing2,
    'region':'crossing-east',
    'reservation_token':reserved2['reservation_token']},token,409)
assert rejected['error']=='crossing-already-consumed',rejected

print('OpenGenesisLINK Crossing v3 reservation/rollback smoke: PASS')
PY
