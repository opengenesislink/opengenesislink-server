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

SECRET="query-smoke-scene-ticket-secret-0123456789abcdef"
CORE_CFG=/tmp/ogl-core-script-world-query.toml
WORLD_CFG=/tmp/ogl-world-script-world-query.toml

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
admin_api_key = "query-smoke-admin-key-0123456789abcdef"
[storage]
scripts = "data/scripts.db"
script_world_actions = "data/script-world-actions.db"
parcels = "data/parcels.db"
moderation = "data/moderation.db"
[scripting]
max_pending_world_actions = 128
world_action_max_attempts = 4
world_action_lease_ms = 750
world_action_ttl_ms = 30000
[hypergrid]
enabled = false
EOF_CORE

cat > "$WORLD_CFG" <<EOF_WORLD
[node]
id = "query-world"
name = "Query World"
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
root = "data/world-query"
parcels = "data/parcels.db"
moderation = "data/moderation.db"
save_interval_seconds = 1
[regions]
count = 1
[region0]
id = "query-region"
name = "Query Region"
grid_x = 1000
grid_y = 1000
EOF_WORLD

./build/dev/opengenesis-core "$CORE_CFG" >/tmp/ogl-query-core.log 2>&1 &
CORE=$!
./build/dev/opengenesis-world "$WORLD_CFG" >/tmp/ogl-query-world.log 2>&1 &
WORLD=$!

cleanup() {
  status=$?
  kill "$WORLD" "$CORE" 2>/dev/null || true
  wait "$WORLD" 2>/dev/null || true
  wait "$CORE" 2>/dev/null || true
  if [ "$status" -ne 0 ]; then
    echo "--- query core ---" >&2
    cat /tmp/ogl-query-core.log >&2 || true
    echo "--- query world ---" >&2
    cat /tmp/ogl-query-world.log >&2 || true
  fi
  exit "$status"
}
trap cleanup EXIT
sleep 3

python3 - <<'PY'
import json,os,socket,struct,time,urllib.request

ADMIN=f"http://127.0.0.1:{os.environ['ADMIN_PORT']}"
SCENE=int(os.environ['SCENE_PORT'])

def api(path,method='GET',body=None,token=None):
    data=None if body is None else json.dumps(body).encode()
    headers={}
    if data is not None: headers['Content-Type']='application/json'
    if token: headers['Authorization']='Bearer '+token
    req=urllib.request.Request(ADMIN+path,data=data,headers=headers,method=method)
    with urllib.request.urlopen(req,timeout=6) as response:
        return response.status,json.loads(response.read())

def read_exact(sock,size):
    out=b''
    while len(out)<size:
        part=sock.recv(size-len(out))
        if not part: raise RuntimeError('scene socket closed')
        out+=part
    return out

def send(sock,msg_type,request_id,payload=''):
    raw=payload.encode()
    sock.sendall(struct.pack('>4sHHII',b'OGL1',1,msg_type,request_id,len(raw))+raw)

def recv(sock):
    magic,version,msg_type,request_id,size=struct.unpack('>4sHHII',read_exact(sock,16))
    assert magic==b'OGL1' and version==1
    return msg_type,request_id,read_exact(sock,size).decode() if size else ''

status,user=api('/v1/auth/register','POST',{
    'username':'query.smoke',
    'display_name':'Query Smoke',
    'password':'correct horse battery staple'})
assert status==201
token=user['token']

status,viewer=api('/v1/viewer/session','POST',{'region':'query-region'},token)
assert status==200
scene=socket.create_connection(('127.0.0.1',SCENE),timeout=5)
send(scene,1,1,'client=query-smoke\nprotocol=1\n')
assert recv(scene)[0]==2
send(scene,100,2,f"region=query-region\nticket={viewer['scene_ticket']}\nx=128\ny=128\nz=23\n")
assert recv(scene)[0]==101

send(scene,110,3,'name=Queryable Cube\nx=130\ny=131\nz=30\nphysical=false\n')
msg,_,payload=recv(scene)
assert msg==111,payload
entity=int(dict(line.split('=',1) for line in payload.splitlines() if '=' in line)['id'])

program=(
    'event touch\n'
    'move 140 141 31\n'
    'object_info obj\n'
    'region_info region\n'
    'terrain_height ground\n'
    'nearby_avatars nearby 32\n'
    'end\n'
    'event readback\n'
    'emit $obj.position\n'
    'emit $obj.name\n'
    'emit $region.id\n'
    'emit $ground.height\n'
    'emit $nearby.count\n'
    'end\n'
)
status,script=api('/v1/scripts','POST',{
    'object_id':f'query-region/{entity}',
    'source':program},token)
assert status==201
script_id=script['id']

status,run=api('/v1/scripts/event','POST',{'script_id':script_id,'event':'touch'},token)
assert status==200 and run['host_applied']==5 and run['host_errors']==[],run

time.sleep(1.5)
status,readback=api('/v1/scripts/event','POST',{'script_id':script_id,'event':'readback'},token)
assert status==200
values=[item['value'] for item in readback['actions']]
assert values[0]=='140.000 141.000 31.000',values
assert values[1]=='Queryable Cube',values
assert values[2]=='query-region',values
assert float(values[3])>0.0,values
assert int(values[4])>=1,values

send(scene,102,4,'')
msg,_,snapshot=recv(scene)
assert msg==103 and f'entity={entity}|object|Queryable Cube|140.000|141.000|31.000|' in snapshot
send(scene,42,5,'')
scene.close()

print('OpenGenesisLINK Script World query/ACK smoke: PASS')
PY
