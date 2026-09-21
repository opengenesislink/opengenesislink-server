#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

rm -rf data/platform-15-smoke-*

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
EXPECTED_VERSION="$(sed -e 's/-dev$//' VERSION | tr -d '\r\n')"
export EXPECTED_VERSION

CFG=/tmp/ogl-core-platform-15.toml
ADMIN_KEY="platform-15-smoke-admin-key-0123456789abcdef"
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
scene_ticket_secret = "platform-15-scene-ticket-secret-0123456789abcdef"
admin_api_key = "$ADMIN_KEY"
login_attempts_per_minute = 100
registration_attempts_per_minute = 100

[database]
backend = "file"

[storage]
worlds = "data/platform-15-smoke-worlds.db"
regions = "data/platform-15-smoke-regions.db"
users = "data/platform-15-smoke-users.db"
sessions = "data/platform-15-smoke-sessions.db"
assets_metadata = "data/platform-15-smoke-assets.db"
assets_blobs = "data/platform-15-smoke-assets"
appearance = "data/platform-15-smoke-appearance.db"
inventory = "data/platform-15-smoke-inventory.db"
friends = "data/platform-15-smoke-friends.db"
messages = "data/platform-15-smoke-messages.db"
social_policies = "data/platform-15-smoke-social-policies.db"
economy = "data/platform-15-smoke-economy.db"
marketplace = "data/platform-15-smoke-marketplace.db"
groups = "data/platform-15-smoke-groups.db"
parcels = "data/platform-15-smoke-parcels.db"
moderation = "data/platform-15-smoke-moderation.db"
audit = "data/platform-15-smoke-audit.log"
admin_roles = "data/platform-15-smoke-admin-roles.db"
estates = "data/platform-15-smoke-estates.db"
landmarks = "data/platform-15-smoke-landmarks.db"
notifications = "data/platform-15-smoke-notifications.db"
group_channels = "data/platform-15-smoke-group-channels.db"
crossings = "data/platform-15-smoke-crossings.db"
object_crossings = "data/platform-15-smoke-object-crossings.db"
scripts = "data/platform-15-smoke-scripts.db"
script_world_actions = "data/platform-15-smoke-script-actions.db"
federation_identity = "data/platform-15-smoke-fed-identity.db"
federation_trust = "data/platform-15-smoke-fed-trust.db"
federation_sessions = "data/platform-15-smoke-fed-sessions.db"
federation_service_grants = "data/platform-15-smoke-fed-grants.db"
hypergrid_sessions = "data/platform-15-smoke-hg-sessions.db"

[economy]
currency_code = "OGL"

[federation]
grid_id = "platform-smoke.local"
base_url = "https://platform-smoke.local"

[hypergrid]
enabled = false
EOF

"$BUILD_DIR/opengenesis-core" "$CFG" >/tmp/ogl-platform-15-core.log 2>&1 &
CORE=$!

cleanup() {
  status=$?
  kill "$CORE" 2>/dev/null || true
  wait "$CORE" 2>/dev/null || true
  if [ "$status" -ne 0 ]; then
    cat /tmp/ogl-platform-15-core.log >&2 || true
  fi
  exit "$status"
}
trap cleanup EXIT
sleep 2

python3 - <<'PY'
import base64
import json
import os
import urllib.error
import urllib.request

BASE=f"http://127.0.0.1:{os.environ['ADMIN_PORT']}"
ADMIN="platform-15-smoke-admin-key-0123456789abcdef"

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
    payload=json.loads(raw or b'{}')
    if expect is not None:
        assert status==expect,(path,status,payload)
    return status,payload

def register(username,display):
    _,payload=api(
        '/v1/auth/register','POST',
        {'username':username,'display_name':display,
         'password':'correct horse battery staple'},
        expect=201)
    return payload

_,info=api('/v1',expect=200)
assert info['version']==os.environ['EXPECTED_VERSION'],info
for cap in (
    'social-policy-v1','group-invites-v1','parcel-access-v1',
    'economy-ledger-v1','economy-escrow-v1',
    'marketplace-v1','marketplace-fulfillment-v1'):
    assert cap in info['capabilities'],cap

alice=register('platform.alice','Platform Alice')
bob=register('platform.bob','Platform Bob')
charlie=register('platform.charlie','Platform Charlie')
at,aid=alice['token'],alice['user']['id']
bt,bid=bob['token'],bob['user']['id']
ct,cid=charlie['token'],charlie['user']['id']

# Social policy: a block prevents relationship and wallet interaction.
api('/v1/social/block','POST',
    {'user_id':bid,'blocked':True},ct,expect=200)
api('/v1/social/friends/request','POST',
    {'user_id':cid},bt,expect=403)
api('/v1/social/block','POST',
    {'user_id':bid,'blocked':False},ct,expect=200)

# Friends + mute: mute suppresses notification while message remains deliverable.
api('/v1/social/friends/request','POST',
    {'user_id':cid},bt,expect=201)
api('/v1/social/friends/accept','POST',
    {'user_id':bid},ct,expect=200)
api('/v1/social/mute','POST',
    {'user_id':bid,'muted':True},ct,expect=200)
_,before=api('/v1/notifications',token=ct,expect=200)
before_unread=before['unread']
api('/v1/social/messages','POST',
    {'recipient_id':cid,'text':'muted notification test'},bt,expect=201)
_,after=api('/v1/notifications',token=ct,expect=200)
assert after['unread']==before_unread,(before,after)
_,msgs=api('/v1/social/messages',token=ct,expect=200)
assert any(m['text']=='muted notification test' for m in msgs['messages']),msgs

# Group invitation lifecycle.
_,created=api('/v1/groups','POST',{'name':'Platform Builders'},at,expect=201)
gid=created['group_id']
_,invite_payload=api('/v1/groups/invites','POST',
    {'group_id':gid,'user_id':bid,'role':'member',
     'lifetime_seconds':600},at,expect=201)
invite=invite_payload['invite'][0]
_,bob_invites=api('/v1/groups/invites',token=bt,expect=200)
assert any(x['id']==invite['id'] and x['state']=='pending'
           for x in bob_invites['invites']),bob_invites
api('/v1/groups/invites/accept','POST',
    {'invite_id':invite['id']},bt,expect=200)
_,members=api(f'/v1/groups/{gid}/members',token=bt,expect=200)
assert any(m['user_id']==bid for m in members['members']),members

# Provider-neutral wallet ledger.
api('/v1/economy/admin/mint','POST',
    {'user_id':bid,'amount_minor':10000,
     'reference':'smoke:mint:bob','memo':'Smoke funding'},
    admin=True,expect=201)
_,wallet=api('/v1/economy/wallet',token=bt,expect=200)
assert wallet['currency']=='OGL' and wallet['balance_minor']==10000,wallet

api('/v1/economy/transfer','POST',
    {'recipient_id':cid,'amount_minor':500,
     'reference':'smoke:transfer:bob-charlie',
     'memo':'Smoke transfer'},bt,expect=201)
_,bob_wallet=api('/v1/economy/wallet',token=bt,expect=200)
_,charlie_wallet=api('/v1/economy/wallet',token=ct,expect=200)
assert bob_wallet['balance_minor']==9500,bob_wallet
assert charlie_wallet['balance_minor']==500,charlie_wallet

# Blocking also stops direct wallet transfers.
api('/v1/social/block','POST',
    {'user_id':bid,'blocked':True},ct,expect=200)
api('/v1/economy/transfer','POST',
    {'recipient_id':cid,'amount_minor':1,
     'reference':'smoke:blocked-transfer'},bt,expect=403)
api('/v1/social/block','POST',
    {'user_id':bid,'blocked':False},ct,expect=200)

# Marketplace listing and escrow-backed fulfillment.
asset_data=b'OpenGenesisLINK 15 marketplace asset'
_,asset_resp=api('/v1/assets','POST',{
    'name':'Platform Shirt',
    'mime_type':'application/octet-stream',
    'data_base64':base64.b64encode(asset_data).decode()},at,expect=201)
asset_id=asset_resp['asset']['id']

_,listing=api('/v1/marketplace/listings','POST',{
    'asset_id':asset_id,
    'title':'Platform Shirt',
    'description':'15.0 smoke listing',
    'price_minor':2500},at,expect=201)
listing_id=listing['id']

_,purchase=api('/v1/marketplace/purchase','POST',
    {'listing_id':listing_id},bt,expect=201)
assert purchase['status']=='purchased',purchase
delivered_asset=purchase['asset']['id']

_,bob_wallet=api('/v1/economy/wallet',token=bt,expect=200)
_,alice_wallet=api('/v1/economy/wallet',token=at,expect=200)
assert bob_wallet['balance_minor']==7000,bob_wallet
assert alice_wallet['balance_minor']==2500,alice_wallet

_,bob_inv=api('/v1/inventory',token=bt,expect=200)
assert any(i['asset_id']==delivered_asset
           for i in bob_inv['items']),bob_inv
_,asset_payload=api('/v1/assets/'+delivered_asset,token=bt,expect=200)
assert base64.b64decode(asset_payload['data_base64'])==asset_data,asset_payload

_,mine=api('/v1/marketplace/mine',token=at,expect=200)
sold=next(x for x in mine['listings'] if x['id']==listing_id)
assert sold['state']=='sold' and sold['buyer_user_id']==bid,sold
api('/v1/marketplace/purchase','POST',
    {'listing_id':listing_id},bt,expect=409)

# Insufficient funds must release listing reservation and keep the listing active.
_,asset2=api('/v1/assets','POST',{
    'name':'Expensive Platform Item',
    'mime_type':'application/octet-stream',
    'data_base64':base64.b64encode(b'expensive').decode()},at,expect=201)
_,listing2=api('/v1/marketplace/listings','POST',{
    'asset_id':asset2['asset']['id'],
    'title':'Expensive Item',
    'description':'rollback test',
    'price_minor':8000},at,expect=201)
api('/v1/marketplace/purchase','POST',
    {'listing_id':listing2['id']},bt,expect=409)
_,active=api('/v1/marketplace/listings',expect=200)
assert any(x['id']==listing2['id'] and x['state']=='active'
           for x in active['listings']),active

_,escrows=api('/v1/economy/escrows',token=bt,expect=200)
assert any(e['state']=='committed' and e['amount_minor']==2500
           for e in escrows['escrows']),escrows

_,buyer_ledger=api('/v1/economy/ledger',token=bt,expect=200)
assert any(e['kind']=='escrow_reserve'
           for e in buyer_ledger['entries']),buyer_ledger
_,seller_ledger=api('/v1/economy/ledger',token=at,expect=200)
assert any(e['kind']=='escrow_commit'
           for e in seller_ledger['entries']),seller_ledger

_,policies=api('/v1/social/policies',token=ct,expect=200)
assert any(p['target_user_id']==bid and p['muted'] is True
           for p in policies['policies']),policies

_,metrics_raw=api('/v1/content/stats',expect=200)
assert metrics_raw['economy_accounts']>=3,metrics_raw
assert metrics_raw['marketplace_active']>=1,metrics_raw

print('OpenGenesisLINK 15.0 platform services smoke: PASS')
PY
