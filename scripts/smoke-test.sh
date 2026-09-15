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
    s=socket.socket(); s.bind(('127.0.0.1',0)); ports.append(s.getsockname()[1]); s.close()
print(*ports)
PORTS
)
export CORE_PORT ADMIN_PORT SCENE_PORT
SECRET="smoke-scene-ticket-secret-0123456789abcdef-050"
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
scene_ticket_lifetime_seconds = 60
[security]
scene_ticket_secret = "$SECRET"
[storage]
worlds = "data/worlds.db"
regions = "data/regions.db"
users = "data/users.db"
sessions = "data/sessions.db"
assets_metadata = "data/assets.db"
assets_blobs = "data/assets"
inventory = "data/inventory.db"
[assets]
max_bytes = 1048576
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
[security]
scene_ticket_secret = "$SECRET"
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
  status=$?; trap - EXIT
  if [ "$status" -ne 0 ]; then
    echo "Smoke ports: core=$CORE_PORT admin=$ADMIN_PORT scene=$SCENE_PORT" >&2
    echo "--- Core ---" >&2; cat /tmp/ogl-core.log 2>/dev/null >&2 || true; cat /tmp/ogl-core2.log 2>/dev/null >&2 || true
    echo "--- World ---" >&2; cat /tmp/ogl-world.log 2>/dev/null >&2 || true; cat /tmp/ogl-world2.log 2>/dev/null >&2 || true
  fi
  kill "${CORE:-}" 2>/dev/null || true; kill "${WORLD:-}" 2>/dev/null || true
  exit "$status"
}
trap cleanup EXIT
sleep 0.5
./build/dev/opengenesis-world "$WORLD_CFG" > /tmp/ogl-world.log 2>&1 & WORLD=$!
sleep 2

python3 - <<'PY'
import base64,json,os,socket,struct,time,urllib.request,urllib.error
ADMIN=f"http://127.0.0.1:{os.environ['ADMIN_PORT']}"; PORT=int(os.environ['SCENE_PORT'])
def api(path,method='GET',body=None,token=None):
    data=None if body is None else json.dumps(body).encode(); headers={}
    if data is not None: headers['Content-Type']='application/json'
    if token: headers['Authorization']='Bearer '+token
    req=urllib.request.Request(ADMIN+path,data=data,headers=headers,method=method)
    with urllib.request.urlopen(req,timeout=5) as r: return r.status,r.headers.get_content_type(),r.read()
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
    h=recv_exact(s,16);m,v,t,r,n=struct.unpack('>4sHHII',h);assert m==b'OGL1' and v==1;return t,r,recv_exact(s,n).decode() if n else ''
def fields(p): return dict(line.split('=',1) for line in p.splitlines() if '=' in line)
def connect_join(ticket, expect_ok=True):
    s=socket.create_connection(('127.0.0.1',PORT),timeout=5);send(s,1,1,'client=ogl-smoke\nprotocol=1\n');assert recv(s)[0]==2
    send(s,100,2,f'region=genesis-central\nticket={ticket}\nx=128\ny=128\nz=23\n');t,_,p=recv(s)
    if expect_ok: assert t==101,(t,p)
    else: assert t==255 and 'invalid-scene-ticket' in p,(t,p)
    return s,p

status,ctype,html=api('/');assert status==200 and ctype=='text/html' and b'authenticated viewer flow' in html
_,_,raw=api('/v1');info=json.loads(raw);assert info['version']=='0.5.0' and 'scene-ticket-v1' in info['capabilities']
status,_,raw=api('/v1/auth/register','POST',{'username':'smoke.user','display_name':'Smoke User','password':'correct horse battery staple'});assert status==201
reg=json.loads(raw);token=reg['token'];uid=reg['user']['id'];open('/tmp/ogl-auth-token','w').write(token);open('/tmp/ogl-user-id','w').write(uid)
# content services
asset_data=b'OpenGenesis asset payload 0.5'
status,_,raw=api('/v1/assets','POST',{'name':'smoke.txt','mime_type':'text/plain','data_base64':base64.b64encode(asset_data).decode()},token);assert status==201
asset=json.loads(raw)['asset'];asset_id=asset['id'];open('/tmp/ogl-asset-id','w').write(asset_id)
_,_,raw=api('/v1/assets/'+asset_id,token=token);download=json.loads(raw);assert base64.b64decode(download['data_base64'])==asset_data
_,_,raw=api('/v1/inventory',token=token);inv=json.loads(raw);root=inv['root']['id']
status,_,raw=api('/v1/inventory/folders','POST',{'parent_id':root,'name':'Objects'},token);assert status==201;folder=json.loads(raw)['folder']['id']
status,_,raw=api('/v1/inventory/items','POST',{'parent_id':folder,'asset_id':asset_id,'name':'Smoke Asset'},token);assert status==201
_,_,raw=api('/v1/content/stats');stats=json.loads(raw);assert stats['assets']==1 and stats['inventory_items']==1
# scene rejects unsigned/no ticket
bad,_=connect_join('',False);bad.close()
# core-issued viewer ticket
status,_,raw=api('/v1/viewer/session','POST',{'region':'genesis-central'},token);assert status==200
viewer=json.loads(raw);ticket=viewer['scene_ticket'];assert viewer['scene_endpoint'].endswith(':'+str(PORT)) and ticket.startswith('ogst1.')
s,p=connect_join(ticket,True);j=fields(p);assert j['user_id']==uid;start=int(j['sequence']);req=2
send(s,102,3,'');t,_,snap=recv(s);assert t==103 and '|avatar|Smoke User|' in snap and ('|'+uid+'\n') in snap
req=3;req+=1;send(s,110,req,'name=Owned Persistent Cube\nx=130\ny=128\nz=30\nphysical=false\n');t,_,p=recv(s);assert t==111;oid=int(fields(p)['id']);open('/tmp/ogl-object-id','w').write(str(oid))
req+=1;send(s,142,req,'x=5\ny=6\nheight=28.5\n');assert recv(s)[0]==143
req+=1;send(s,120,req,'text=Authenticated hello 0.5\n');assert recv(s)[0]==121
# replay is rejected while original session remains active
replay,_=connect_join(ticket,False);replay.close()
time.sleep(2)
_,_,raw=api('/v1/status');live=json.loads(raw);assert live['version']=='0.5.0' and live['assets']==1 and live['inventory_items']==1
r=live['regions'][0];assert r['state']=='online' and r['avatars']>=1 and r['terrain_revision']>=2
open('/tmp/ogl-generation','w').write(str(live['world_nodes'][0]['generation']));open('/tmp/ogl-ticks','w').write(str(r['ticks']))
req+=1;send(s,42,req,'');s.close()
PY

GEN1=$(cat /tmp/ogl-generation); TICKS1=$(cat /tmp/ogl-ticks)
kill "$CORE"; wait "$CORE" 2>/dev/null || true; sleep 2
./build/dev/opengenesis-core "$CORE_CFG" > /tmp/ogl-core2.log 2>&1 & CORE=$!; sleep 4
python3 - "$GEN1" "$TICKS1" <<'PY'
import base64,json,os,sys,urllib.request
A=f"http://127.0.0.1:{os.environ['ADMIN_PORT']}";token=open('/tmp/ogl-auth-token').read();asset_id=open('/tmp/ogl-asset-id').read()
def api(path,method='GET',body=None):
 d=None if body is None else json.dumps(body).encode();h={'Authorization':'Bearer '+token};
 if d is not None:h['Content-Type']='application/json'
 with urllib.request.urlopen(urllib.request.Request(A+path,data=d,headers=h,method=method),timeout=5) as r:return json.load(r)
d=api('/v1/status');assert d['world_nodes'][0]['state']=='online' and d['world_nodes'][0]['generation']>int(sys.argv[1]) and d['regions'][0]['ticks']>int(sys.argv[2])
assert api('/v1/auth/me')['user']['username']=='smoke.user';assert len(api('/v1/inventory')['items'])==1;assert base64.b64decode(api('/v1/assets/'+asset_id)['data_base64'])==b'OpenGenesis asset payload 0.5'
viewer=api('/v1/viewer/session','POST',{'region':'genesis-central'});open('/tmp/ogl-ticket-after-core','w').write(viewer['scene_ticket'])
PY

# Full World restart proves object/terrain owner persistence and fresh tickets still work.
kill "$WORLD"; wait "$WORLD" 2>/dev/null || true; sleep 2
./build/dev/opengenesis-world "$WORLD_CFG" > /tmp/ogl-world2.log 2>&1 & WORLD=$!; sleep 3
python3 - <<'PY'
import json,os,socket,struct,time,urllib.request
A=f"http://127.0.0.1:{os.environ['ADMIN_PORT']}";PORT=int(os.environ['SCENE_PORT']);token=open('/tmp/ogl-auth-token').read();uid=open('/tmp/ogl-user-id').read();oid=int(open('/tmp/ogl-object-id').read())
def api(path,method='GET',body=None):
 d=None if body is None else json.dumps(body).encode();h={'Authorization':'Bearer '+token};
 if d is not None:h['Content-Type']='application/json'
 with urllib.request.urlopen(urllib.request.Request(A+path,data=d,headers=h,method=method),timeout=5) as r:return json.load(r)
def rx(s,n):
 b=b''
 while len(b)<n:
  p=s.recv(n-len(b));
  if not p:raise RuntimeError('closed')
  b+=p
 return b
def send(s,t,r,p=''):b=p.encode();s.sendall(struct.pack('>4sHHII',b'OGL1',1,t,r,len(b))+b)
def recv(s):h=rx(s,16);m,v,t,r,n=struct.unpack('>4sHHII',h);return t,r,rx(s,n).decode() if n else ''
viewer=api('/v1/viewer/session','POST',{'region':'genesis-central'});ticket=viewer['scene_ticket']
s=socket.create_connection(('127.0.0.1',PORT),timeout=5);send(s,1,1,'client=restart-check\nprotocol=1\n');assert recv(s)[0]==2;send(s,100,2,f'region=genesis-central\nticket={ticket}\n');assert recv(s)[0]==101
send(s,102,3,'');t,_,snap=recv(s);assert t==103 and f'entity={oid}|object|Owned Persistent Cube|' in snap and ('|'+uid+'\n') in snap
send(s,140,4,'x=5\ny=6\n');t,_,p=recv(s);assert t==141 and 'height=28.500' in p
send(s,42,5,'');s.close();time.sleep(1)
PY

python3 - <<'PY'
import json,os,urllib.request,urllib.error
A=f"http://127.0.0.1:{os.environ['ADMIN_PORT']}";token=open('/tmp/ogl-auth-token').read()
req=urllib.request.Request(A+'/v1/auth/logout',data=b'{}',headers={'Authorization':'Bearer '+token,'Content-Type':'application/json'},method='POST')
with urllib.request.urlopen(req) as r:assert json.load(r)['status']=='logged-out'
try: urllib.request.urlopen(urllib.request.Request(A+'/v1/auth/me',headers={'Authorization':'Bearer '+token})); raise AssertionError('revoked token accepted')
except urllib.error.HTTPError as e: assert e.code==401
PY

kill "$WORLD"; wait "$WORLD" 2>/dev/null || true; sleep 1
python3 - <<'PY'
import json,os,urllib.request
d=json.load(urllib.request.urlopen(f"http://127.0.0.1:{os.environ['ADMIN_PORT']}/v1/status"));assert d['regions'][0]['state']=='offline' and d['identities']==1 and d['active_sessions']==0 and d['assets']==1 and d['inventory_items']==1
PY

echo "OpenGenesisLINK 0.5.0 authenticated-viewer/content smoke test: PASS"
