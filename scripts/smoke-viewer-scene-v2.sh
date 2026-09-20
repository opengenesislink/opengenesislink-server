#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
rm -rf data/viewer-scene-v2 data/viewer-scene-v2-*

BUILD_DIR="${OGL_BUILD_DIR:-build/release}"
if [ ! -x "$BUILD_DIR/opengenesis-core" ]; then
  BUILD_DIR="build/dev"
fi

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

SECRET="viewer-scene-v2-smoke-ticket-secret-0123456789abcdef"
CORE_CFG=/tmp/ogl-core-viewer-scene-v2.toml
WORLD_CFG=/tmp/ogl-world-viewer-scene-v2.toml

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
admin_api_key = "viewer-scene-v2-admin-key-0123456789abcdef"
[storage]
identities = "data/viewer-scene-v2-identities.db"
sessions = "data/viewer-scene-v2-sessions.db"
assets = "data/viewer-scene-v2-assets.db"
asset_blobs = "data/viewer-scene-v2-assets"
appearance = "data/viewer-scene-v2-appearance.db"
inventory = "data/viewer-scene-v2-inventory.db"
parcels = "data/viewer-scene-v2-parcels.db"
moderation = "data/viewer-scene-v2-moderation.db"
[hypergrid]
enabled = false
EOF_CORE

cat > "$WORLD_CFG" <<EOF_WORLD
[node]
id = "viewer-scene-v2-world"
name = "Viewer Scene v2 World"
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
water_height = 20.0
[storage]
root = "data/viewer-scene-v2-world"
parcels = "data/viewer-scene-v2-parcels.db"
moderation = "data/viewer-scene-v2-moderation.db"
save_interval_seconds = 1
[regions]
count = 1
[region0]
id = "viewer-scene-v2-region"
name = "Viewer Scene v2 Region"
grid_x = 5000
grid_y = 5000
EOF_WORLD

"$BUILD_DIR/opengenesis-core" "$CORE_CFG" >/tmp/ogl-viewer-scene-v2-core.log 2>&1 &
CORE=$!
"$BUILD_DIR/opengenesis-world" "$WORLD_CFG" >/tmp/ogl-viewer-scene-v2-world.log 2>&1 &
WORLD=$!

cleanup() {
  status=$?
  kill "$WORLD" "$CORE" 2>/dev/null || true
  wait "$WORLD" 2>/dev/null || true
  wait "$CORE" 2>/dev/null || true
  if [ "$status" -ne 0 ]; then
    echo "--- Viewer Scene v2 core ---" >&2
    cat /tmp/ogl-viewer-scene-v2-core.log >&2 || true
    echo "--- Viewer Scene v2 world ---" >&2
    cat /tmp/ogl-viewer-scene-v2-world.log >&2 || true
  fi
  exit "$status"
}
trap cleanup EXIT
sleep 3

python3 - <<'PY'
import json,os,socket,struct,time,urllib.request,urllib.error

ADMIN=f"http://127.0.0.1:{os.environ['ADMIN_PORT']}"
SCENE=int(os.environ['SCENE_PORT'])

def api(path,method='GET',body=None,token=None):
    data=None if body is None else json.dumps(body).encode()
    headers={}
    if data is not None:
        headers['Content-Type']='application/json'
    if token:
        headers['Authorization']='Bearer '+token
    req=urllib.request.Request(ADMIN+path,data=data,headers=headers,method=method)
    with urllib.request.urlopen(req,timeout=8) as response:
        return response.status,json.loads(response.read())

def read_exact(sock,size):
    out=b''
    while len(out)<size:
        chunk=sock.recv(size-len(out))
        if not chunk:
            raise RuntimeError('scene socket closed')
        out+=chunk
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

def fields(payload):
    return dict(
        line.split('=',1)
        for line in payload.splitlines()
        if '=' in line and not line.startswith(('entity=','event=','parcel=')))

status,user=api('/v1/auth/register','POST',{
    'username':'viewer.scene.v2',
    'display_name':'Viewer Scene V2',
    'password':'correct horse battery staple'})
assert status==201,user
token=user['token']

# Wait for World/Region registry convergence.
for _ in range(20):
    try:
        status,bootstrap=api('/v1/viewer/bootstrap','POST',{
            'region':'viewer-scene-v2-region',
            'x':128.0,'y':128.0,'z':25.0},token)
        if status==200:
            break
    except urllib.error.HTTPError as error:
        if error.code != 409:
            raise
    time.sleep(0.25)
else:
    raise AssertionError('viewer bootstrap never became available')

assert bootstrap['viewer_contract']=='ogl-viewer-bootstrap-v1',bootstrap
assert bootstrap['scene_contract']=='scene-v2',bootstrap
assert bootstrap['session']['region']['id']=='viewer-scene-v2-region',bootstrap
assert bootstrap['appearance']['user_id']==bootstrap['session']['user']['id'],bootstrap
assert bootstrap['inventory']['root']['id'],bootstrap
assert bootstrap['assets']==[],bootstrap
assert bootstrap['parcels']==[],bootstrap
assert bootstrap['region_runtime']['grid_x']==5000,bootstrap
caps=set(bootstrap['session']['capabilities'].split(','))
for required in (
    'scene.sync','scene.avatar.reconcile',
    'scene.region.metadata','scene.parcel.read'):
    assert required in caps,(required,caps)

scene=socket.create_connection(('127.0.0.1',SCENE),timeout=5)
send(scene,1,1,'client=viewer-scene-v2-smoke\nprotocol=1\n')
msg,_,hello=recv(scene)
assert msg==2,hello
assert 'scene_contract=2' in hello,hello
assert 'sync=scene-sync-v1' in hello,hello
assert 'movement=avatar-reconcile-v1' in hello,hello

send(scene,100,2,
     'region=viewer-scene-v2-region\n'
     f"ticket={bootstrap['session']['scene_ticket']}\n")
msg,_,joined=recv(scene)
assert msg==101,joined
join=fields(joined)
assert join['scene_contract']=='2',join
assert join['sync']=='scene-sync-v1',join
avatar_id=int(join['avatar_id'])

send(scene,134,3,'')
msg,_,metadata=recv(scene)
assert msg==135,metadata
meta=fields(metadata)
assert meta['region']=='viewer-scene-v2-region',meta
assert int(meta['terrain_width'])==256,meta
assert float(meta['water_height'])==20.0,meta

send(scene,136,4,'')
msg,_,parcel_payload=recv(scene)
assert msg==137,parcel_payload
parcel=fields(parcel_payload)
assert parcel['mode']=='region' and parcel['count']=='0',parcel

send(scene,132,5,'since=0\nmax_events=32\n')
msg,_,sync=recv(scene)
assert msg==133,sync
sync_fields=fields(sync)
assert sync_fields['mode']=='snapshot',sync_fields
assert f'entity={avatar_id}|avatar|' in sync,sync
snapshot_sequence=int(sync_fields['sequence'])

send(scene,152,6,
     'client_sequence=1\n'
     'x=130\ny=131\nz=25\n'
     'rx=0\nry=0\nrz=45\n'
     'vx=1\nvy=0\nvz=0\n')
msg,_,reconciled=recv(scene)
assert msg==153,reconciled
ack=fields(reconciled)
assert ack['status']=='reconciled',ack
assert ack['client_sequence']=='1',ack
assert float(ack['x'])==130.0 and float(ack['y'])==131.0,ack
server_sequence=int(ack['server_sequence'])
assert server_sequence>snapshot_sequence,(snapshot_sequence,server_sequence)
assert int(ack['tick'])>=0,ack

send(scene,152,7,
     'client_sequence=1\n'
     'x=131\ny=131\nz=25\n')
msg,_,stale=recv(scene)
assert msg==255,stale
assert 'reason=stale-client-sequence' in stale,stale

send(scene,132,8,
     f'since={snapshot_sequence}\nmax_events=32\n')
msg,_,delta=recv(scene)
assert msg==133,delta
delta_fields=fields(delta)
assert delta_fields['mode']=='delta',delta_fields
assert 'avatar_move' in delta,delta

send(scene,136,9,'x=130\ny=131\n')
msg,_,parcel_point=recv(scene)
assert msg==137,parcel_point
point=fields(parcel_point)
assert point['mode']=='point' and point['count']=='0',point

send(scene,42,10,'')
scene.close()

print('OpenGenesisLINK 13.0 Viewer bootstrap / Scene v2 smoke: PASS')
PY
