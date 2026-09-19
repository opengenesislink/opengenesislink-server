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

SECRET="script-engine-smoke-scene-ticket-secret-0123456789abcdef"
CORE_CFG=/tmp/ogl-core-script-engine.toml
WORLD_CFG=/tmp/ogl-world-script-engine.toml

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
admin_api_key = "script-engine-smoke-admin-key-0123456789abcdef"
[storage]
scripts = "data/scripts-engine-smoke.db"
script_world_actions = "data/script-actions-engine-smoke.db"
parcels = "data/parcels-engine-smoke.db"
moderation = "data/moderation-engine-smoke.db"
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
id = "script-engine-world"
name = "ScriptEngine World"
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
root = "data/world-script-engine"
parcels = "data/parcels-engine-smoke.db"
moderation = "data/moderation-engine-smoke.db"
save_interval_seconds = 1
[regions]
count = 1
[region0]
id = "script-engine-region"
name = "ScriptEngine Region"
grid_x = 4100
grid_y = 4100
EOF_WORLD

./build/dev/opengenesis-core "$CORE_CFG" >/tmp/ogl-script-engine-core.log 2>&1 &
CORE=$!
./build/dev/opengenesis-world "$WORLD_CFG" >/tmp/ogl-script-engine-world.log 2>&1 &
WORLD=$!

cleanup() {
  status=$?
  kill "$WORLD" "$CORE" 2>/dev/null || true
  wait "$WORLD" 2>/dev/null || true
  wait "$CORE" 2>/dev/null || true
  if [ "$status" -ne 0 ]; then
    echo "--- ScriptEngine core ---" >&2
    cat /tmp/ogl-script-engine-core.log >&2 || true
    echo "--- ScriptEngine world ---" >&2
    cat /tmp/ogl-script-engine-world.log >&2 || true
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
    try:
        with urllib.request.urlopen(req,timeout=8) as response:
            return response.status,json.loads(response.read())
    except urllib.error.HTTPError as error:
        raise AssertionError(
            f'{method} {path} HTTP {error.code}: '
            f'{error.read().decode(errors="replace")}') from error

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

status,caps=api('/v1/scripts/capabilities')
assert status==200
assert caps['engine']=='OGL ScriptEngine',caps
assert set(caps['languages'])=={'legacy','lsl','ogl'},caps
assert len(caps['lsl_functions'])>=500,caps
assert len(caps['lsl_events'])==44,caps
assert len(caps['ogl'])>=30,caps

status,user=api('/v1/auth/register','POST',{
    'username':'script.engine.smoke',
    'display_name':'Script Engine Smoke',
    'password':'correct horse battery staple'})
assert status==201
token=user['token']

status,viewer=api('/v1/viewer/session','POST',{
    'region':'script-engine-region'},token)
assert status==200

scene=socket.create_connection(('127.0.0.1',SCENE),timeout=5)
send(scene,1,1,'client=script-engine-smoke\nprotocol=1\n')
assert recv(scene)[0]==2
send(scene,100,2,
     f"region=script-engine-region\nticket={viewer['scene_ticket']}\n"
     "x=128\ny=128\nz=23\n")
assert recv(scene)[0]==101

send(scene,110,3,
     'name=Script Engine Cube\n'
     'x=130\ny=130\nz=30\n'
     'physical=false\n')
msg,_,payload=recv(scene)
assert msg==111,payload
entity=int(dict(
    line.split('=',1) for line in payload.splitlines()
    if '=' in line)['id'])
binding=f'script-engine-region/{entity}'

ogl=(
    '@ogl 1\n'
    'state default\n'
    'on touch\n'
    'let count = 1\n'
    'inc count by 2\n'
    'world.move 140 141 31\n'
    'world.text OGL online\n'
    'goto active\n'
    'end\n'
    'state active\n'
    'on timer\n'
    'world.say OGL timer\n'
    'end\n'
)
status,ogl_script=api('/v1/scripts','POST',{
    'object_id':binding,
    'language':'ogl',
    'source':ogl},token)
assert status==201,ogl_script
assert ogl_script['language']=='ogl',ogl_script

status,ogl_run=api('/v1/scripts/event','POST',{
    'script_id':ogl_script['id'],
    'event':'touch'},token)
assert status==200,ogl_run
assert ogl_run['state']=='active',ogl_run
assert ogl_run['host_applied']==2,ogl_run
assert ogl_run['host_errors']==[],ogl_run

time.sleep(1.0)
send(scene,102,4,'')
msg,_,snapshot=recv(scene)
assert msg==103
line=next(
    x for x in snapshot.splitlines()
    if x.startswith(f'entity={entity}|object|Script Engine Cube|'))
parts=line.split('|')
assert parts[3]=='140.000' and parts[4]=='141.000' and parts[5]=='31.000',parts
assert parts[20]=='OGL online',parts

lsl=(
    'default {\n'
    '  state_entry() {\n'
    '    llSetTimerEvent(1.0);\n'
    '    llSetText("LSL ready", <1,1,1>, 1.0);\n'
    '  }\n'
    '  listen(integer channel, string name, key id, string message) {\n'
    '    integer length = llStringLength(message);\n'
    '    string upper = llToUpper(message);\n'
    '    float magnitude = llVecMag(<3,4,0>);\n'
    '    llSay(0, upper);\n'
    '  }\n'
    '  touch_start(integer count) {\n'
    '    llSetPos(<150,151,32>);\n'
    '    state active;\n'
    '  }\n'
    '}\n'
    'active {\n'
    '  timer() {\n'
    '    llSetText("LSL active", <1,1,1>, 1.0);\n'
    '  }\n'
    '}\n'
)
status,lsl_script=api('/v1/scripts','POST',{
    'object_id':binding,
    'language':'lsl',
    'source':lsl},token)
assert status==201,lsl_script
assert lsl_script['language']=='lsl',lsl_script

status,entry=api('/v1/scripts/event','POST',{
    'script_id':lsl_script['id'],
    'event':'state_entry'},token)
assert status==200,entry
assert entry['host_applied']==2,entry

status,listen=api('/v1/scripts/event','POST',{
    'script_id':lsl_script['id'],
    'event':'listen',
    'payload':'7\nAlice\nagent-key\nhello ogl'},token)
assert status==200,listen
assert any(
    action['type']=='say' and action['value']=='HELLO OGL'
    for action in listen['actions']),listen

status,touch=api('/v1/scripts/event','POST',{
    'script_id':lsl_script['id'],
    'event':'touch_start',
    'payload':'1'},token)
assert status==200,touch
assert touch['state']=='active',touch
assert touch['host_applied']==1,touch

time.sleep(1.2)
status,scripts=api('/v1/scripts',token=token)
records={item['id']:item for item in scripts['scripts']}
assert records[ogl_script['id']]['language']=='ogl'
assert records[lsl_script['id']]['language']=='lsl'
assert records[lsl_script['id']]['state']=='active'

send(scene,102,5,'')
msg,_,snapshot=recv(scene)
assert msg==103
line=next(
    x for x in snapshot.splitlines()
    if x.startswith(f'entity={entity}|object|Script Engine Cube|'))
parts=line.split('|')
assert parts[3]=='150.000' and parts[4]=='151.000' and parts[5]=='32.000',parts
assert parts[20]=='LSL active',parts

send(scene,42,6,'')
scene.close()
print('OpenGenesisLINK 9.0 OGL/LSL ScriptEngine end-to-end smoke: PASS')
PY
