#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
rm -rf data
cmake --preset dev >/dev/null
cmake --build --preset dev >/dev/null

read -r CORE_PORT ADMIN_PORT SCENE_PORT < <(python3 - <<'PORTS'
import socket
ports=[]
for _ in range(3):
    sock=socket.socket(); sock.bind(('127.0.0.1',0)); ports.append(sock.getsockname()[1]); sock.close()
print(*ports)
PORTS
)
export CORE_PORT ADMIN_PORT SCENE_PORT
CORE_CFG=/tmp/ogl-core-smoke.toml
WORLD_CFG=/tmp/ogl-world-smoke.toml
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
[storage]
worlds = "data/worlds.db"
regions = "data/regions.db"
users = "data/users.db"
sessions = "data/sessions.db"
EOF_CORE
cat > "$WORLD_CFG" <<EOF_WORLD
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
[storage]
root = "data/world"
save_interval_seconds = 1
[regions]
count = 1
[region0]
id = "genesis-central"
name = "Genesis Central"
grid_x = 1000
grid_y = 1000
EOF_WORLD

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
    cat /tmp/ogl-world2.log 2>/dev/null >&2 || true
  fi
  kill "${CORE:-}" 2>/dev/null || true
  kill "${WORLD:-}" 2>/dev/null || true
  exit "$status"
}
trap cleanup EXIT
sleep 0.5
./build/dev/opengenesis-world "$WORLD_CFG" > /tmp/ogl-world.log 2>&1 & WORLD=$!
sleep 2

python3 - <<'PY'
import json, os, socket, struct, time, urllib.request, urllib.error
ADMIN=f"http://127.0.0.1:{os.environ['ADMIN_PORT']}"
SCENE_PORT=int(os.environ['SCENE_PORT'])

def api(path, method='GET', body=None, token=None):
    data=None if body is None else json.dumps(body).encode()
    headers={}
    if data is not None: headers['Content-Type']='application/json'
    if token: headers['Authorization']='Bearer '+token
    request=urllib.request.Request(ADMIN+path, data=data, headers=headers, method=method)
    with urllib.request.urlopen(request, timeout=5) as response:
        content=response.read()
        return response.status, response.headers.get_content_type(), content

status, ctype, html=api('/')
assert status==200 and ctype=='text/html' and b'OpenGenesisLINK Core' in html and b'/v1/status' in html
status, _, info=api('/v1')
info=json.loads(info); assert info['version']=='0.4.0' and info['api_version']==1
status, _, registered=api('/v1/auth/register','POST',{
    'username':'smoke.user','display_name':'Smoke User','password':'correct horse battery staple'})
assert status==201
registered=json.loads(registered); token=registered['token']; user_id=registered['user']['id']
assert len(token)==64
status, _, me=api('/v1/auth/me', token=token)
me=json.loads(me); assert me['user']['id']==user_id and me['user']['username']=='smoke.user'
status, _, stats=api('/v1/identity/stats'); stats=json.loads(stats)
assert stats['users']==1 and stats['active_sessions']==1
open('/tmp/ogl-auth-token','w').write(token)

def recv_exact(sock,size):
    out=b''
    while len(out)<size:
        part=sock.recv(size-len(out))
        if not part: raise RuntimeError('scene socket closed')
        out+=part
    return out

def send_frame(sock,msg_type,request_id,payload=''):
    data=payload.encode(); sock.sendall(struct.pack('>4sHHII',b'OGL1',1,msg_type,request_id,len(data))+data)

def recv_frame(sock):
    header=recv_exact(sock,16)
    magic,version,msg_type,request_id,size=struct.unpack('>4sHHII',header)
    assert magic==b'OGL1' and version==1
    return msg_type,request_id,recv_exact(sock,size).decode() if size else ''

def fields(payload):
    return dict(line.split('=',1) for line in payload.splitlines() if '=' in line)

sock=socket.create_connection(('127.0.0.1',SCENE_PORT),timeout=5)
send_frame(sock,1,1,'client=ogl-smoke\nprotocol=1\n'); assert recv_frame(sock)[0]==2
send_frame(sock,100,2,'region=genesis-central\navatar=Smoke Avatar\nx=128\ny=128\nz=23\n')
t,_,payload=recv_frame(sock); assert t==101
start_sequence=int(fields(payload)['sequence']); req=2
req+=1; send_frame(sock,110,req,'name=Persistent Smoke Cube\nx=130\ny=128\nz=30\nsx=1\nsy=1\nsz=1\nphysical=false\n')
t,_,payload=recv_frame(sock); assert t==111; object_id=int(fields(payload)['id']); open('/tmp/ogl-object-id','w').write(str(object_id))
req+=1; send_frame(sock,142,req,'x=5\ny=6\nheight=27.5\n'); t,_,payload=recv_frame(sock); assert t==143
req+=1; send_frame(sock,140,req,'x=5\ny=6\n'); t,_,payload=recv_frame(sock); assert t==141 and abs(float(fields(payload)['height'])-27.5)<0.001
req+=1; send_frame(sock,120,req,'text=Hello OpenGenesis 0.4\n'); assert recv_frame(sock)[0]==121
req+=1; send_frame(sock,130,req,f'since={start_sequence}\n'); t,_,events=recv_frame(sock); assert t==131 and 'terrain_updated' in events and '|chat|' in events

time.sleep(2)
_,_,raw=api('/v1/status'); live=json.loads(raw)
assert live['version']=='0.4.0' and live['identities']==1 and live['active_sessions']==1
assert live['world_nodes'][0]['state']=='online'
r=live['regions'][0]; assert r['state']=='online' and r['entities']>=2 and r['avatars']>=1 and r['terrain_revision']>=2
open('/tmp/ogl-generation','w').write(str(live['world_nodes'][0]['generation']))
open('/tmp/ogl-ticks','w').write(str(r['ticks']))
req+=1; send_frame(sock,42,req,''); sock.close()
PY

GEN1=$(cat /tmp/ogl-generation)
TICKS1=$(cat /tmp/ogl-ticks)
kill "$CORE"; wait "$CORE" 2>/dev/null || true
sleep 2
./build/dev/opengenesis-core "$CORE_CFG" > /tmp/ogl-core2.log 2>&1 & CORE=$!
sleep 4
python3 - "$GEN1" "$TICKS1" <<'PY'
import json,os,sys,urllib.request
ADMIN=f"http://127.0.0.1:{os.environ['ADMIN_PORT']}"
token=open('/tmp/ogl-auth-token').read()
def get(path, token=None):
    headers={} if not token else {'Authorization':'Bearer '+token}
    with urllib.request.urlopen(urllib.request.Request(ADMIN+path,headers=headers),timeout=5) as r: return json.load(r)
d=get('/v1/status'); assert d['world_nodes'][0]['state']=='online'; assert d['world_nodes'][0]['generation']>int(sys.argv[1]); assert d['regions'][0]['ticks']>int(sys.argv[2])
me=get('/v1/auth/me',token); assert me['user']['username']=='smoke.user'
PY

# Stop and restart the World Node to prove object/terrain persistence.
kill "$WORLD"; wait "$WORLD" 2>/dev/null || true
sleep 2
./build/dev/opengenesis-world "$WORLD_CFG" > /tmp/ogl-world2.log 2>&1 & WORLD=$!
sleep 3
python3 - <<'PY'
import os,socket,struct,time,urllib.request,json
PORT=int(os.environ['SCENE_PORT']); object_id=int(open('/tmp/ogl-object-id').read())
def recv_exact(s,n):
    b=b''
    while len(b)<n:
        p=s.recv(n-len(b))
        if not p: raise RuntimeError('closed')
        b+=p
    return b
def send(s,t,r,p=''):
    b=p.encode(); s.sendall(struct.pack('>4sHHII',b'OGL1',1,t,r,len(b))+b)
def recv(s):
    h=recv_exact(s,16); m,v,t,r,n=struct.unpack('>4sHHII',h); return t,r,recv_exact(s,n).decode() if n else ''
def fields(p): return dict(line.split('=',1) for line in p.splitlines() if '=' in line)
s=socket.create_connection(('127.0.0.1',PORT),timeout=5); send(s,1,1,'client=persistence-check\nprotocol=1\n'); assert recv(s)[0]==2; send(s,100,2,'region=genesis-central\navatar=Restart Avatar\n'); assert recv(s)[0]==101
send(s,102,3,''); t,_,snap=recv(s); assert t==103 and f'entity={object_id}|object|Persistent Smoke Cube|' in snap
send(s,140,4,'x=5\ny=6\n'); t,_,p=recv(s); assert t==141 and abs(float(fields(p)['height'])-27.5)<0.001
send(s,42,5,''); s.close()

time.sleep(1)
admin=f"http://127.0.0.1:{os.environ['ADMIN_PORT']}"
with urllib.request.urlopen(admin+'/v1/regions') as r: regions=json.load(r)['regions']
assert regions[0]['terrain_revision']>=2
PY

# Revoke the session through the browser API.
python3 - <<'PY'
import json,os,urllib.request,urllib.error
admin=f"http://127.0.0.1:{os.environ['ADMIN_PORT']}"; token=open('/tmp/ogl-auth-token').read()
request=urllib.request.Request(admin+'/v1/auth/logout',data=b'{}',headers={'Authorization':'Bearer '+token,'Content-Type':'application/json'},method='POST')
with urllib.request.urlopen(request) as r: assert json.load(r)['status']=='logged-out'
request=urllib.request.Request(admin+'/v1/auth/me',headers={'Authorization':'Bearer '+token})
try:
    urllib.request.urlopen(request)
    raise AssertionError('revoked token accepted')
except urllib.error.HTTPError as error:
    assert error.code==401
PY

kill "$WORLD"; wait "$WORLD" 2>/dev/null || true
sleep 1
python3 - <<'PY'
import json,os,urllib.request
d=json.load(urllib.request.urlopen(f"http://127.0.0.1:{os.environ['ADMIN_PORT']}/v1/status"))
assert d['regions'][0]['state']=='offline' and d['identities']==1 and d['active_sessions']==0
PY

echo "OpenGenesisLINK 0.4.0 identity/persistence/web-api smoke test: PASS"
