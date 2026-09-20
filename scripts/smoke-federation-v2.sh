#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
rm -rf data/federation-v2-smoke*

BUILD_DIR="${OGL_BUILD_DIR:-build/release}"
if [ ! -x "$BUILD_DIR/opengenesis-core" ]; then
  BUILD_DIR="build/dev"
fi

read -r CORE_PORT ADMIN_PORT < <(python3 - <<'PY'
import socket
ports=[]
for _ in range(2):
    s=socket.socket()
    s.bind(('127.0.0.1',0))
    ports.append(s.getsockname()[1])
    s.close()
print(*ports)
PY
)
export CORE_PORT ADMIN_PORT

CFG=/tmp/ogl-core-federation-v2.toml
ADMIN_KEY="federation-v2-smoke-admin-key-0123456789abcdef"
cat > "$CFG" <<EOF
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
scene_ticket_secret = "federation-v2-scene-ticket-secret-0123456789abcdef"
admin_api_key = "$ADMIN_KEY"
login_attempts_per_minute = 100
registration_attempts_per_minute = 100
[storage]
worlds = "data/federation-v2-smoke-worlds.db"
regions = "data/federation-v2-smoke-regions.db"
users = "data/federation-v2-smoke-users.db"
sessions = "data/federation-v2-smoke-sessions.db"
assets_metadata = "data/federation-v2-smoke-assets.db"
assets_blobs = "data/federation-v2-smoke-assets"
appearance = "data/federation-v2-smoke-appearance.db"
inventory = "data/federation-v2-smoke-inventory.db"
friends = "data/federation-v2-smoke-friends.db"
messages = "data/federation-v2-smoke-messages.db"
groups = "data/federation-v2-smoke-groups.db"
parcels = "data/federation-v2-smoke-parcels.db"
moderation = "data/federation-v2-smoke-moderation.db"
audit = "data/federation-v2-smoke-audit.log"
admin_roles = "data/federation-v2-smoke-admin-roles.db"
estates = "data/federation-v2-smoke-estates.db"
landmarks = "data/federation-v2-smoke-landmarks.db"
notifications = "data/federation-v2-smoke-notifications.db"
group_channels = "data/federation-v2-smoke-group-channels.db"
crossings = "data/federation-v2-smoke-crossings.db"
object_crossings = "data/federation-v2-smoke-object-crossings.db"
scripts = "data/federation-v2-smoke-scripts.db"
script_world_actions = "data/federation-v2-smoke-script-actions.db"
federation_identity = "data/federation-v2-smoke-identity.db"
federation_trust = "data/federation-v2-smoke-trust.db"
federation_sessions = "data/federation-v2-smoke-foreign-sessions.db"
federation_service_grants = "data/federation-v2-smoke-service-grants.db"
hypergrid_sessions = "data/federation-v2-smoke-hg-sessions.db"
[federation]
grid_id = "smoke.home.example"
base_url = "https://smoke.home.example"
[hypergrid]
enabled = false
EOF

"$BUILD_DIR/opengenesis-core" "$CFG" >/tmp/ogl-federation-v2-core.log 2>&1 &
CORE=$!
cleanup() {
  status=$?
  kill "$CORE" 2>/dev/null || true
  wait "$CORE" 2>/dev/null || true
  if [ "$status" -ne 0 ]; then
    cat /tmp/ogl-federation-v2-core.log >&2 || true
  fi
  exit "$status"
}
trap cleanup EXIT
sleep 2

python3 - <<'PY'
import base64,json,os,urllib.request,urllib.error

BASE=f"http://127.0.0.1:{os.environ['ADMIN_PORT']}"
ADMIN="federation-v2-smoke-admin-key-0123456789abcdef"

def api(path,method='GET',body=None,token=None,admin=False,expect=None):
    data=None if body is None else json.dumps(body).encode()
    headers={}
    if data is not None:
        headers['Content-Type']='application/json'
    if token:
        headers['Authorization']='Bearer '+token
    if admin:
        headers['X-OpenGenesis-Admin-Key']=ADMIN
    req=urllib.request.Request(BASE+path,data=data,headers=headers,method=method)
    try:
        with urllib.request.urlopen(req,timeout=8) as r:
            status=r.status
            raw=r.read()
    except urllib.error.HTTPError as e:
        status=e.code
        raw=e.read()
    if expect is not None:
        assert status==expect,(path,status,raw)
    return status,json.loads(raw or b'{}')

def decode_travel(token):
    payload=token.split('.',1)[0]
    payload += '='*((4-len(payload)%4)%4)
    text=base64.urlsafe_b64decode(payload.encode()).decode()
    values=dict(line.split('=',1) for line in text.splitlines() if '=' in line)
    def hx(name):
        return bytes.fromhex(values.get(name,'')).decode()
    return {
        'version':values['v'],
        'grant_id':hx('grant'),
        'service_token':hx('service'),
        'scopes':hx('scopes'),
        'home_url':hx('home'),
        'audience':hx('aud'),
        'subject':hx('sub'),
    }

_,info=api('/v1',expect=200)
for cap in (
    'ogl-fed-v2','federation-service-grants-v1',
    'federation-remote-profile-v1',
    'federation-remote-inventory-v1',
    'federation-remote-assets-v1',
    'federation-remote-social-v1',
    'federation-remote-presence-v1'):
    assert cap in info['capabilities'],cap

_,fed=api('/v1/federation/info',expect=200)
assert fed['protocol']=='OGL-FED/2',fed
assert fed['grid_id']=='smoke.home.example',fed
assert 'inventory' in fed['remote_services'],fed

_,alice=api('/v1/auth/register','POST',{
    'username':'fed.alice',
    'display_name':'Federation Alice',
    'password':'correct horse battery staple'},expect=201)
_,bob=api('/v1/auth/register','POST',{
    'username':'fed.bob',
    'display_name':'Federation Bob',
    'password':'correct horse battery staple'},expect=201)
at=alice['token']; aid=alice['user']['id']
bt=bob['token']; bid=bob['user']['id']

_,asset=api('/v1/assets','POST',{
    'name':'Federated Shirt',
    'mime_type':'application/octet-stream',
    'data_base64':base64.b64encode(b'federated-asset-data').decode()},at,expect=201)
asset_id=asset['asset']['id']

_,inv=api('/v1/inventory',token=at,expect=200)
root_id=inv['root']['id']
_,item=api('/v1/inventory/items','POST',{
    'parent_id':root_id,
    'asset_id':asset_id,
    'name':'Federated Shirt Item'},at,expect=201)
item_id=item['item']['id']

api('/v1/avatar/appearance/wearable','POST',{
    'slot':'shirt','item_id':item_id,'asset_id':asset_id},at,expect=200)

api('/v1/social/friends/request','POST',{'user_id':bid},at,expect=201)
api('/v1/social/friends/accept','POST',{'user_id':aid},bt,expect=200)

remote_key='22'*32
api('/v1/federation/trust','POST',{
    'grid_id':'smoke.remote.example',
    'base_url':'https://smoke.remote.example',
    'public_key':remote_key},admin=True,expect=201)

_,issued=api('/v1/federation/travel/issue','POST',{
    'audience_grid':'smoke.remote.example',
    'origin_region':'home',
    'destination_region':'remote-welcome',
    'lifetime_seconds':120},at,expect=201)
assert issued['protocol']=='OGL-FED/2',issued
ctx=decode_travel(issued['travel_token'])
assert ctx['version']=='2',ctx
assert ctx['grant_id']==issued['service_grant_id'],ctx
assert ctx['home_url']=='https://smoke.home.example',ctx
assert ctx['audience']=='smoke.remote.example',ctx
assert ctx['subject']==aid,ctx
for scope in ('profile','appearance','inventory','assets','social','presence'):
    assert scope in ctx['scopes'].split(','),(scope,ctx)

auth={
    'grant_id':ctx['grant_id'],
    'service_token':ctx['service_token'],
    'audience_grid':'smoke.remote.example',
    'subject_user':aid,
}

_,profile=api('/v1/federation/service/profile','POST',auth,expect=200)
assert profile['user']['id']==aid,profile
assert any(w['asset_id']==asset_id for w in profile['appearance']['wearables']),profile

_,remote_inv=api('/v1/federation/service/inventory','POST',auth,expect=200)
assert any(x['asset_id']==asset_id for x in remote_inv['items']),remote_inv

asset_req=dict(auth); asset_req['asset_id']=asset_id
_,remote_asset=api('/v1/federation/service/asset','POST',asset_req,expect=200)
assert base64.b64decode(remote_asset['data_base64'])==b'federated-asset-data',remote_asset

_,social=api('/v1/federation/service/social','POST',auth,expect=200)
assert any(f['other_user_id']==bid and f['status']=='accepted'
           for f in social['friends']),social

_,presence=api('/v1/federation/service/presence','POST',auth,expect=200)
assert presence['online'] is False,presence

wrong=dict(auth); wrong['audience_grid']='other.remote.example'
api('/v1/federation/service/inventory','POST',wrong,expect=403)

_,grants=api('/v1/federation/service-grants',admin=True,expect=200)
assert any(g['id']==ctx['grant_id'] and not g['revoked']
           for g in grants['grants']),grants

api('/v1/federation/service-grants/revoke','POST',
    {'grant_id':ctx['grant_id']},at,expect=200)
api('/v1/federation/service/profile','POST',auth,expect=403)

_,issued2=api('/v1/federation/travel/issue','POST',{
    'audience_grid':'smoke.remote.example',
    'destination_region':'remote-two',
    'lifetime_seconds':120},at,expect=201)
ctx2=decode_travel(issued2['travel_token'])
auth2={
    'grant_id':ctx2['grant_id'],
    'service_token':ctx2['service_token'],
    'audience_grid':'smoke.remote.example',
    'subject_user':aid,
}
api('/v1/federation/service/profile','POST',auth2,expect=200)
api('/v1/federation/revoke','POST',
    {'grid_id':'smoke.remote.example'},admin=True,expect=200)
api('/v1/federation/service/profile','POST',auth2,expect=403)

_,grants=api('/v1/federation/service-grants',admin=True,expect=200)
by_id={g['id']:g for g in grants['grants']}
assert by_id[ctx['grant_id']]['revoked'] is True,by_id
assert by_id[ctx2['grant_id']]['revoked'] is True,by_id

print('OpenGenesisLINK 14.0 OGL-FED v2 remote services smoke: PASS')
PY
