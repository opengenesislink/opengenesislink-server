#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
rm -rf data/production-foundation-smoke

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

export OGL_SCENE_TICKET_SECRET="production-foundation-scene-ticket-secret-0123456789abcdef"
export OGL_ADMIN_API_KEY="production-foundation-admin-key-0123456789abcdef"
export OGL_WORLD_NODE_SECRET="production-foundation-world-node-secret-0123456789abcdef"

CORE_CFG=/tmp/ogl-production-foundation-core.toml
WORLD_CFG=/tmp/ogl-production-foundation-world.toml
DB_PATH="$ROOT/data/production-foundation-smoke/core.sqlite"

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
production_mode = true
scene_ticket_secret = ""
scene_ticket_secret_env = "OGL_SCENE_TICKET_SECRET"
admin_api_key = ""
admin_api_key_env = "OGL_ADMIN_API_KEY"
world_node_secret = ""
world_node_secret_env = "OGL_WORLD_NODE_SECRET"
login_attempts_per_minute = 12
registration_attempts_per_minute = 30

[database]
backend = "sqlite"
sqlite_path = "$DB_PATH"
pool_size = 2
connect_timeout_seconds = 5

[storage]
appearance = "data/production-foundation-smoke/appearance.db"
assets_metadata = "data/production-foundation-smoke/assets.db"
assets_blobs = "data/production-foundation-smoke/assets"
inventory = "data/production-foundation-smoke/inventory.db"
friends = "data/production-foundation-smoke/friends.db"
messages = "data/production-foundation-smoke/messages.db"
groups = "data/production-foundation-smoke/groups.db"
parcels = "data/production-foundation-smoke/parcels.db"
estates = "data/production-foundation-smoke/estates.db"
landmarks = "data/production-foundation-smoke/landmarks.db"
notifications = "data/production-foundation-smoke/notifications.db"
group_channels = "data/production-foundation-smoke/group-channels.db"
crossings = "data/production-foundation-smoke/crossings.db"
object_crossings = "data/production-foundation-smoke/object-crossings.db"
scripts = "data/production-foundation-smoke/scripts.db"
script_world_actions = "data/production-foundation-smoke/script-world-actions.db"
federation_identity = "data/production-foundation-smoke/federation-identity.db"
federation_trust = "data/production-foundation-smoke/federation-trust.db"
federation_sessions = "data/production-foundation-smoke/federation-sessions.db"
hypergrid_sessions = "data/production-foundation-smoke/hypergrid-sessions.db"

[assets]
max_bytes = 1048576

[crossing]
object_max_attempts = 5

[scripting]
max_pending_world_actions = 128
world_action_max_attempts = 4
world_action_lease_ms = 750
world_action_ttl_ms = 30000

[federation]
grid_id = "foundation.test"
base_url = "http://127.0.0.1:$ADMIN_PORT"

[hypergrid]
enabled = false
EOF_CORE

cat > "$WORLD_CFG" <<EOF_WORLD
[node]
id = "production-foundation-world"
name = "Production Foundation World"

[network]
public_endpoint = "127.0.0.1:$SCENE_PORT"
scene_listen_address = "127.0.0.1"
scene_port = $SCENE_PORT

[core]
endpoint = "127.0.0.1:$CORE_PORT"
reconnect_seconds = 1
lease_seconds = 2
auth_secret = ""
auth_secret_env = "OGL_WORLD_NODE_SECRET"

[security]
production_mode = true
scene_ticket_secret = ""
scene_ticket_secret_env = "OGL_SCENE_TICKET_SECRET"

[runtime]
tick_hz = 30.0
terrain_base_height = 21.0
water_height = 20.0

[storage]
root = "data/production-foundation-smoke/world"
parcels = "data/production-foundation-smoke/parcels.db"
moderation = "data/production-foundation-smoke/moderation-world.db"
save_interval_seconds = 1

[regions]
count = 1

[region0]
id = "production-foundation-region"
name = "Production Foundation Region"
grid_x = 4200
grid_y = 4200
EOF_WORLD

./build/dev/opengenesis-core "$CORE_CFG" >/tmp/ogl-production-foundation-core.log 2>&1 &
CORE=$!
./build/dev/opengenesis-world "$WORLD_CFG" >/tmp/ogl-production-foundation-world.log 2>&1 &
WORLD=$!

cleanup() {
  status=$?
  kill "$WORLD" "$CORE" 2>/dev/null || true
  wait "$WORLD" 2>/dev/null || true
  wait "$CORE" 2>/dev/null || true
  if [ "$status" -ne 0 ]; then
    echo "--- production foundation core ---" >&2
    cat /tmp/ogl-production-foundation-core.log >&2 || true
    echo "--- production foundation world ---" >&2
    cat /tmp/ogl-production-foundation-world.log >&2 || true
  fi
  exit "$status"
}
trap cleanup EXIT

python3 - <<'PY'
import json,os,time,urllib.request,urllib.error

ADMIN=f"http://127.0.0.1:{os.environ['ADMIN_PORT']}"
ADMIN_KEY=os.environ['OGL_ADMIN_API_KEY']

def request(path,method='GET',body=None,token=None,admin=False,expect=None):
    data=None if body is None else json.dumps(body).encode()
    headers={}
    if data is not None:
        headers['Content-Type']='application/json'
    if token:
        headers['Authorization']='Bearer '+token
    if admin:
        headers['X-OpenGenesis-Admin-Key']=ADMIN_KEY
    req=urllib.request.Request(ADMIN+path,data=data,headers=headers,method=method)
    try:
        with urllib.request.urlopen(req,timeout=8) as response:
            status=response.status
            raw=response.read()
    except urllib.error.HTTPError as error:
        status=error.code
        raw=error.read()
    parsed=json.loads(raw or b'{}')
    if expect is not None:
        assert status==expect,(method,path,status,parsed)
    return status,parsed

for _ in range(20):
    try:
        status,health=request('/health')
        if status==200 and health.get('storage',{}).get('backend')=='sqlite':
            break
    except Exception:
        pass
    time.sleep(0.25)
else:
    raise AssertionError('Core did not become healthy')

assert health['status']=='ok',health
assert health['storage']['ready'] is True,health
assert health['storage']['pool_size']==2,health

for _ in range(20):
    status,state=request('/v1/status')
    worlds=state.get('world_nodes',[])
    regions=state.get('regions',[])
    if any(w['id']=='production-foundation-world' and w['state']=='online' for w in worlds) and        any(r['id']=='production-foundation-region' and r['state']=='online' for r in regions):
        break
    time.sleep(0.25)
else:
    raise AssertionError(('authenticated World Node did not register',state))

status,registered=request('/v1/auth/register','POST',{
    'username':'foundation.admin',
    'display_name':'Foundation Admin',
    'password':'correct horse battery staple'
},expect=201)
token=registered['token']
user_id=registered['user']['id']

request('/v1/admin/roles/grant','POST',{
    'user_id':user_id,
    'role':'moderator'
},admin=True,expect=200)

status,roles=request('/v1/admin/roles',admin=True,expect=200)
assert any(r['user_id']==user_id and r['role']=='moderator' for r in roles['roles']),roles

request('/v1/admin/moderation',token=token,expect=200)
request('/v1/admin/audit',token=token,expect=200)
request('/v1/federation/peers',token=token,expect=403)

request('/v1/admin/roles/grant','POST',{
    'user_id':user_id,
    'role':'operator'
},admin=True,expect=200)
request('/v1/federation/peers',token=token,expect=200)

for attempt in range(12):
    status,_=request('/v1/auth/login','POST',{
        'username':'rate.limit.target',
        'password':'wrong password'
    })
    assert status==401,(attempt,status)
status,limited=request('/v1/auth/login','POST',{
    'username':'rate.limit.target',
    'password':'wrong password'
},expect=429)
assert limited['error']=='login-rate-limited',limited

status,storage=request('/v1/storage/status',expect=200)
assert storage['backend']=='sqlite' and storage['ready'] is True,storage
assert storage['successful_operations']>0,storage

status,api=request('/v1',expect=200)
for capability in [
    'sql-storage-v1','sqlite-storage-v1','postgresql-storage-v1',
    'mariadb-storage-v1','storage-health-v1','admin-rbac-v1',
    'auth-rate-limit-v1'
]:
    assert capability in api['capabilities'],capability

print('OpenGenesisLINK 10.0 production foundation HTTP/RBAC/HMAC smoke: PASS')
PY

./build/dev/opengenesis-storage status "$CORE_CFG" | tee /tmp/ogl-storage-status.log
grep -q '^backend=sqlite$' /tmp/ogl-storage-status.log
grep -q '^ready=true$' /tmp/ogl-storage-status.log
grep -q '^schema_version=2$' /tmp/ogl-storage-status.log
