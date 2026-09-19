#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
rm -rf data

read -r CORE_PORT ADMIN_PORT SCENE_PORT < <(python3 - <<'PY'
import socket
ports=[]
for _ in range(3):
    s=socket.socket()
    s.bind(('127.0.0.1',0))
    ports.append(s.getsockname()[1])
    s.close()
print(*ports)
PY
)
export CORE_PORT ADMIN_PORT SCENE_PORT

SECRET="object-crossing-smoke-scene-ticket-secret-0123456789abcdef"
CORE_CFG=/tmp/ogl-core-object-crossing.toml
WORLD_CFG=/tmp/ogl-world-object-crossing.toml

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
admin_api_key = "object-crossing-smoke-admin-key-0123456789abcdef"
[storage]
object_crossings = "data/object-crossings-smoke.db"
crossings = "data/avatar-crossings-smoke.db"
scripts = "data/scripts-object-crossing-smoke.db"
script_world_actions = "data/script-actions-object-crossing-smoke.db"
parcels = "data/parcels-object-crossing-smoke.db"
moderation = "data/moderation-object-crossing-smoke.db"
[crossing]
object_max_attempts = 4
[scripting]
max_pending_world_actions = 64
[hypergrid]
enabled = false
EOF_CORE

cat > "$WORLD_CFG" <<EOF_WORLD
[node]
id = "object-crossing-world"
name = "Object Crossing World"
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
root = "data/world-object-crossing"
parcels = "data/parcels-object-crossing-smoke.db"
moderation = "data/moderation-object-crossing-smoke.db"
save_interval_seconds = 1
[regions]
count = 2
[region0]
id = "object-west"
name = "Object West"
grid_x = 3000
grid_y = 3000
[region1]
id = "object-east"
name = "Object East"
grid_x = 3001
grid_y = 3000
EOF_WORLD

./build/dev/opengenesis-core "$CORE_CFG" >/tmp/ogl-object-crossing-core.log 2>&1 &
CORE=$!
./build/dev/opengenesis-world "$WORLD_CFG" >/tmp/ogl-object-crossing-world.log 2>&1 &
WORLD=$!

cleanup() {
  status=$?
  kill "$WORLD" "$CORE" 2>/dev/null || true
  wait "$WORLD" 2>/dev/null || true
  wait "$CORE" 2>/dev/null || true
  if [ "$status" -ne 0 ]; then
    echo "--- object crossing core ---" >&2
    cat /tmp/ogl-object-crossing-core.log >&2 || true
    echo "--- object crossing world ---" >&2
    cat /tmp/ogl-object-crossing-world.log >&2 || true
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
    if data is not None:
        headers['Content-Type']='application/json'
    if token:
        headers['Authorization']='Bearer '+token
    req=urllib.request.Request(
        ADMIN+path,data=data,headers=headers,method=method)
    with urllib.request.urlopen(req,timeout=6) as response:
        return response.status,json.loads(response.read())

def read_exact(sock,size):
    out=b''
    while len(out)<size:
        part=sock.recv(size-len(out))
        if not part:
            raise RuntimeError('scene socket closed')
        out+=part
    return out

def send(sock,msg_type,request_id,payload=''):
    raw=payload.encode()
    sock.sendall(struct.pack(
        '>4sHHII',b'OGL1',1,msg_type,request_id,len(raw))+raw)

def recv(sock):
    magic,version,msg_type,request_id,size=struct.unpack(
        '>4sHHII',read_exact(sock,16))
    assert magic==b'OGL1' and version==1
    payload=read_exact(sock,size).decode() if size else ''
    return msg_type,request_id,payload

def join(region,ticket,request_base):
    sock=socket.create_connection(('127.0.0.1',SCENE),timeout=5)
    send(sock,1,request_base,'client=object-crossing-smoke\nprotocol=1\n')
    assert recv(sock)[0]==2
    send(sock,100,request_base+1,
         f'region={region}\nticket={ticket}\nx=128\ny=128\nz=23\n')
    msg,_,payload=recv(sock)
    assert msg==101,payload
    return sock

status,user=api('/v1/auth/register','POST',{
    'username':'object.crossing.smoke',
    'display_name':'Object Crossing Smoke',
    'password':'correct horse battery staple'})
assert status==201
token=user['token']
user_id=user['user']['id']

status,info=api('/v1')
assert status==200
assert 'object-crossing-v1' in info['capabilities'],info['capabilities']

status,viewer=api('/v1/viewer/session','POST',{'region':'object-west'},token)
assert status==200
west=join('object-west',viewer['scene_ticket'],1)

send(west,110,3,
     'name=Migrating Crate\n'
     'x=250\ny=128\nz=30\n'
     'rx=0\nry=0\nrz=45\n'
     'sx=2\nsy=2\nsz=2\n'
     'physical=false\n'
     'everyone_permissions=1\n')
msg,_,payload=recv(west)
assert msg==111,payload
source_id=int(dict(
    line.split('=',1) for line in payload.splitlines() if '=' in line)['id'])

send(west,102,4,'')
msg,_,snapshot=recv(west)
assert msg==103
assert f'entity={source_id}|object|Migrating Crate|' in snapshot

status,crossing=api('/v1/world/object-crossings','POST',{
    'source_region':'object-west',
    'destination_region':'object-east',
    'source_entity_id':source_id},token)
assert status==202,crossing
assert crossing['state']=='prepared'
destination_id=crossing['destination_entity_id']
assert destination_id!=source_id
assert destination_id & (1<<63)

terminal=None
for _ in range(80):
    _,listing=api('/v1/world/object-crossings',token=token)
    records={item['id']:item for item in listing['object_crossings']}
    terminal=records.get(crossing['id'])
    if terminal and terminal['state']=='completed':
        break
    time.sleep(0.1)

assert terminal is not None
assert terminal['state']=='completed',terminal
assert terminal['exported_unix']>0
assert terminal['imported_unix']>0
assert terminal['completed_unix']>0

send(west,102,5,'')
msg,_,west_snapshot=recv(west)
assert msg==103
assert f'entity={source_id}|object|Migrating Crate|' not in west_snapshot
send(west,42,6,'')
west.close()

status,east_viewer=api('/v1/viewer/session','POST',{'region':'object-east'},token)
assert status==200
east=join('object-east',east_viewer['scene_ticket'],10)
send(east,102,12,'')
msg,_,east_snapshot=recv(east)
assert msg==103
needle=f'entity={destination_id}|object|Migrating Crate|'
assert needle in east_snapshot,east_snapshot
line=next(line for line in east_snapshot.splitlines() if line.startswith(needle))
parts=line.split('|')
assert parts[3]=='1.000',parts
assert parts[4]=='128.000',parts
assert parts[5]=='30.000',parts
assert parts[8]=='45.000',parts
assert parts[9]=='2.000' and parts[10]=='2.000' and parts[11]=='2.000',parts
assert parts[12]==user_id,parts
assert parts[16]=='1',parts

send(east,42,13,'')
east.close()

print('OpenGenesisLINK Object Crossing end-to-end smoke: PASS')
PY
