#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
rm -rf data
cmake --preset dev >/dev/null
cmake --build --preset dev >/dev/null

read -r CORE_PORT ADMIN_PORT SCENE_PORT HG_PORT < <(python3 - <<'PY'
import socket
ports=[]
for _ in range(4):
    s=socket.socket(); s.bind(('127.0.0.1',0)); ports.append(s.getsockname()[1]); s.close()
print(*ports)
PY
)
export CORE_PORT ADMIN_PORT SCENE_PORT HG_PORT
SECRET="smoke-scene-ticket-secret-0123456789abcdef-200"
ADMIN_KEY="smoke-admin-api-key-0123456789abcdef"
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
admin_api_key = "$ADMIN_KEY"
[storage]
worlds = "data/worlds.db"
regions = "data/regions.db"
users = "data/users.db"
sessions = "data/sessions.db"
assets_metadata = "data/assets.db"
assets_blobs = "data/assets"
appearance = "data/appearance.db"
inventory = "data/inventory.db"
friends = "data/friends.db"
messages = "data/messages.db"
groups = "data/groups.db"
parcels = "data/parcels.db"
moderation = "data/moderation.db"
audit = "data/audit.log"
estates = "data/estates.db"
landmarks = "data/landmarks.db"
notifications = "data/notifications.db"
group_channels = "data/group_channels.db"
federation_identity = "data/federation-identity.db"
federation_trust = "data/federation-trust.db"
federation_sessions = "data/federation-sessions.db"
hypergrid_sessions = "data/hypergrid-sessions.db"
[federation]
grid_id = "local.opengenesislink"
base_url = "http://127.0.0.1:$ADMIN_PORT"
[hypergrid]
enabled = true
listen_address = "127.0.0.1"
port = $HG_PORT
external_name = "http://127.0.0.1:$HG_PORT"
home_uri = "http://127.0.0.1:$HG_PORT"
asset_uri = "http://127.0.0.1:$HG_PORT"
inventory_uri = "http://127.0.0.1:$HG_PORT"
avatar_uri = "http://127.0.0.1:$HG_PORT"
friends_uri = "http://127.0.0.1:$HG_PORT"
im_uri = "http://127.0.0.1:$HG_PORT"
region_host = "127.0.0.1"
region_http_port = $SCENE_PORT
region_internal_port = $SCENE_PORT
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
parcels = "data/parcels.db"
moderation = "data/moderation.db"
save_interval_seconds = 1
[regions]
count = 2
[region0]
id = "genesis-central"
name = "Genesis Central"
grid_x = 1000
grid_y = 1000
[region1]
id = "genesis-east"
name = "Genesis East"
grid_x = 1001
grid_y = 1000
EOF_WORLD

./build/dev/opengenesis-core "$CORE_CFG" > /tmp/ogl-core.log 2>&1 & CORE=$!
stop_pid(){
  pid="${1:-}"
  [ -n "$pid" ] || return 0
  kill "$pid" 2>/dev/null || true
  for _ in $(seq 1 30); do
    kill -0 "$pid" 2>/dev/null || break
    sleep 0.1
  done
  if kill -0 "$pid" 2>/dev/null; then kill -KILL "$pid" 2>/dev/null || true; fi
  wait "$pid" 2>/dev/null || true
}
cleanup(){
  status=$?; trap - EXIT
  if [ "$status" -ne 0 ]; then
    echo "Smoke ports: core=$CORE_PORT admin=$ADMIN_PORT scene=$SCENE_PORT hypergrid=$HG_PORT" >&2
    echo "--- Core ---" >&2; cat /tmp/ogl-core.log 2>/dev/null >&2 || true; cat /tmp/ogl-core2.log 2>/dev/null >&2 || true
    echo "--- World ---" >&2; cat /tmp/ogl-world.log 2>/dev/null >&2 || true; cat /tmp/ogl-world2.log 2>/dev/null >&2 || true
  fi
  stop_pid "${WORLD:-}"
  stop_pid "${CORE:-}"
  return "$status"
}
trap cleanup EXIT
sleep 0.5
./build/dev/opengenesis-world "$WORLD_CFG" > /tmp/ogl-world.log 2>&1 & WORLD=$!
sleep 3

python3 - <<'PY'
import base64,json,os,socket,struct,time,urllib.request,urllib.error,urllib.parse
A=f"http://127.0.0.1:{os.environ['ADMIN_PORT']}"; PORT=int(os.environ['SCENE_PORT'])
def api(path,method='GET',body=None,token=None,admin=False):
    data=None if body is None else json.dumps(body).encode(); headers={}
    if data is not None: headers['Content-Type']='application/json'
    if token: headers['Authorization']='Bearer '+token
    if admin: headers['X-OpenGenesis-Admin-Key']='smoke-admin-api-key-0123456789abcdef'
    req=urllib.request.Request(A+path,data=data,headers=headers,method=method)
    with urllib.request.urlopen(req,timeout=6) as r: return r.status,r.headers.get_content_type(),r.read()
HG=f"http://127.0.0.1:{os.environ['HG_PORT']}"
def hg_form(path,fields):
    data=urllib.parse.urlencode(fields).encode()
    req=urllib.request.Request(HG+path,data=data,headers={'Content-Type':'application/x-www-form-urlencoded'},method='POST')
    with urllib.request.urlopen(req,timeout=6) as r: return r.status,r.read().decode()
def hg_xml(method,fields):
    members=''.join('<member><name>'+k+'</name><value><string>'+v+'</string></value></member>' for k,v in fields.items())
    data=('<?xml version="1.0"?><methodCall><methodName>'+method+'</methodName><params><param><value><struct>'+members+'</struct></value></param></params></methodCall>').encode()
    req=urllib.request.Request(HG+'/',data=data,headers={'Content-Type':'text/xml'},method='POST')
    with urllib.request.urlopen(req,timeout=6) as r: return r.status,r.read().decode()
def rx(s,n):
    b=b''
    while len(b)<n:
        p=s.recv(n-len(b))
        if not p: raise RuntimeError('closed')
        b+=p
    return b
def send(s,t,r,p=''):
    b=p.encode(); s.sendall(struct.pack('>4sHHII',b'OGL1',1,t,r,len(b))+b)
def recv(s):
    m,v,t,r,n=struct.unpack('>4sHHII',rx(s,16)); assert m==b'OGL1' and v==1
    return t,r,rx(s,n).decode() if n else ''
def fields(p): return dict(line.split('=',1) for line in p.splitlines() if '=' in line)
def join(region,ticket,ok=True):
    s=socket.create_connection(('127.0.0.1',PORT),timeout=5)
    send(s,1,1,'client=ogl-smoke-100\nprotocol=1\n'); assert recv(s)[0]==2
    send(s,100,2,f'region={region}\nticket={ticket}\nx=128\ny=128\nz=23\n')
    t,_,p=recv(s)
    if ok: assert t==101,(t,p)
    else: assert t==255,(t,p)
    return s,p

def register(username,display):
    st,_,raw=api('/v1/auth/register','POST',{'username':username,'display_name':display,'password':'correct horse battery staple'})
    assert st==201; return json.loads(raw)

status,ctype,html=api('/'); assert status==200 and ctype=='text/html' and b'presence' in html.lower() and b'social' in html.lower()
_,_,raw=api('/v1'); info=json.loads(raw); assert info['version']=='4.5.0'
for cap in ['presence-v1','friends-v1','messaging-v1','avatar-movement-v1','region-handoff-v1','scene-capabilities-v1','groups-v1','land-parcels-v1','object-permissions-v1','asset-permissions-v1','teleport-v1','moderation-v1','audit-v1','estates-v1','landmarks-v1','notifications-v1','group-channels-v1','prometheus-metrics-v1','ogl-fed-v1','hypergrid-session-v1']:
    assert cap in info['capabilities'],cap
_,_,raw=api('/v1/federation/info'); fed=json.loads(raw)
assert fed['protocol']=='OGL-FED/1' and fed['grid_id']=='local.opengenesislink' and len(fed['public_key'])==64

alice=register('alice.smoke','Alice Smoke'); bob=register('bob.smoke','Bob Smoke')
at,auid=alice['token'],alice['user']['id']; bt,buid=bob['token'],bob['user']['id']

# native OGL-FED control plane
remote_key='11'*32
st,_,_=api('/v1/federation/trust','POST',{'grid_id':'smoke.remote.example','base_url':'https://smoke.remote.example','public_key':remote_key},admin=True); assert st==201
_,_,raw=api('/v1/federation/peers',admin=True); assert len(json.loads(raw)['peers'])==1
st,_,raw=api('/v1/federation/travel/issue','POST',{'audience_grid':'smoke.remote.example','origin_region':'genesis-central','destination_region':'remote-welcome','lifetime_seconds':90},at); assert st==201
issued=json.loads(raw); assert issued['protocol']=='OGL-FED/1' and issued['audience_grid']=='smoke.remote.example' and len(issued['travel_token'])>100
st,_,_=api('/v1/federation/revoke','POST',{'grid_id':'smoke.remote.example'},admin=True); assert st==200
# Hypergrid home-travel/session control plane
_,_,raw=api('/v1/hypergrid/info'); hgi=json.loads(raw); assert hgi['enabled'] is True
st,_,raw=api('/v1/hypergrid/travel/issue','POST',{'destination_gatekeeper':'http://127.0.0.1:'+os.environ['HG_PORT'],'client_ip':'127.0.0.1'},at); assert st==201
hgtravel=json.loads(raw); assert len(hgtravel['agent_id'])==36 and len(hgtravel['session_id'])==36 and ';' in hgtravel['service_session_id']
xml=('<?xml version="1.0"?><methodCall><methodName>verify_agent</methodName><params><param><value><struct>'
     '<member><name>sessionID</name><value><string>'+hgtravel['session_id']+'</string></value></member>'
     '<member><name>token</name><value><string>'+hgtravel['service_session_id']+'</string></value></member>'
     '</struct></value></param></params></methodCall>').encode()
req=urllib.request.Request('http://127.0.0.1:'+os.environ['HG_PORT']+'/',data=xml,headers={'Content-Type':'text/xml'},method='POST')
with urllib.request.urlopen(req,timeout=6) as resp:
    body=resp.read().decode(); assert '<name>result</name>' in body and '>True<' in body
_,_,raw=api('/v1/hypergrid/sessions',admin=True); hgs=json.loads(raw); assert hgs['home_sessions']==1
st,body=hg_xml('get_server_urls',{}); assert st==200 and 'SRV_InventoryServerURI' in body and 'SRV_AvatarServerURI' in body and 'SRV_IMServerURI' in body
st,body=hg_form('/xinventory',{'METHOD':'GETROOTFOLDER','PRINCIPAL':hgtravel['agent_id']}); assert st==200 and '<folder type="List">' in body
st,body=hg_form('/avatar',{'METHOD':'getavatar','UserID':hgtravel['agent_id']}); assert st==200 and '<AvatarType>1</AvatarType>' in body
remote_hg='12345678-1234-4234-8234-123456789abc'
st,body=hg_xml('grid_instant_message',{'from_agent_id':remote_hg,'to_agent_id':hgtravel['agent_id'],'im_session_id':'aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeeeee','timestamp':str(int(time.time())),'from_agent_name':'Remote Resident','message':'hello over hypergrid','dialog':'AA==','from_group':'FALSE','offline':'AA==','parent_estate_id':'0','position_x':'0','position_y':'0','position_z':'0','region_id':'00000000-0000-0000-0000-000000000000','binary_bucket':''}); assert st==200 and '<name>success</name>' in body and '>TRUE<' in body
st,_,raw=api('/v1/hypergrid/travel/return','POST',{'session_id':hgtravel['session_id']},at); assert st==200 and json.loads(raw)['status']=='returning-home'
st,body=hg_xml('agent_is_coming_home',{'sessionID':hgtravel['session_id'],'externalName':HG}); assert st==200 and '>True<' in body
st,body=hg_xml('logout_agent',{'userID':hgtravel['agent_id'],'sessionID':hgtravel['session_id']}); assert st==200 and '>true<' in body

for path,val in [('/tmp/ogl-auth-token',at),('/tmp/ogl-user-id',auid),('/tmp/ogl-bob-token',bt),('/tmp/ogl-bob-id',buid)]: open(path,'w').write(val)

# social graph + offline-capable direct message
st,_,_=api('/v1/social/friends/request','POST',{'user_id':buid},at); assert st==201
st,_,_=api('/v1/social/friends/accept','POST',{'user_id':auid},bt); assert st==200
_,_,raw=api('/v1/social/friends',token=at); friends=json.loads(raw)['friends']; assert len(friends)==1 and friends[0]['status']=='accepted'
st,_,raw=api('/v1/social/messages','POST',{'recipient_id':buid,'text':'hello from 4.5.0-dev'},at); assert st==201
message_id=json.loads(raw)['message_id']; open('/tmp/ogl-message-id','w').write(message_id)
_,_,raw=api('/v1/social/messages',token=bt); inbox=json.loads(raw); assert inbox['unread']==1 and inbox['messages'][0]['text']=='hello from 4.5.0-dev'
st,_,_=api('/v1/social/messages/read','POST',{'message_id':message_id},bt); assert st==200

# groups + land governance
st,_,raw=api('/v1/groups','POST',{'name':'Smoke Builders'},at); assert st==201
group_id=json.loads(raw)['group_id']; open('/tmp/ogl-group-id','w').write(group_id)
st,_,_=api('/v1/groups/members','POST',{'group_id':group_id,'user_id':buid,'role':'member'},at); assert st==201
_,_,raw=api(f'/v1/groups/{group_id}/members',token=at); assert len(json.loads(raw)['members'])==2
st,_,raw=api('/v1/parcels','POST',{'region_id':'genesis-central','name':'Smoke Parcel','x1':0,'y1':0,'x2':255,'y2':255},at); assert st==201
parcel_id=json.loads(raw)['parcel_id']; open('/tmp/ogl-parcel-id','w').write(parcel_id)
st,_,_=api('/v1/parcels/policy','POST',{'parcel_id':parcel_id,'group_id':group_id,'public_entry':False,'public_build':False,'group_build':True,'group_terraform':True},at); assert st==200
# estate + group channel + notifications + landmarks
st,_,raw=api('/v1/estates','POST',{'name':'Smoke Estate'},at); assert st==201
estate_id=json.loads(raw)['estate_id']; open('/tmp/ogl-estate-id','w').write(estate_id)
st,_,_=api('/v1/estates/managers','POST',{'estate_id':estate_id,'user_id':buid,'action':'add'},at); assert st==200
st,_,_=api('/v1/estates/regions','POST',{'estate_id':estate_id,'region_id':'genesis-central'},bt); assert st==200
st,_,_=api('/v1/estates/regions/policy','POST',{'estate_id':estate_id,'region_id':'genesis-central','public_access':True,'allow_fly':True,'allow_scripts':True,'allow_voice':False,'max_agents':10,'maturity':1,'landing_x':64,'landing_y':65,'landing_z':22},bt); assert st==200
_,_,raw=api('/v1/regions/genesis-central/estate'); ep=json.loads(raw)['policies'][0]; assert ep['estate_id']==estate_id and ep['max_agents']==10 and ep['landing']['x']==64
st,_,raw=api('/v1/groups/channel','POST',{'group_id':group_id,'kind':'notice','title':'Smoke Notice','text':'Operations channel online'},at); assert st==201
_,_,raw=api(f'/v1/groups/{group_id}/channel',token=bt); assert json.loads(raw)['posts'][0]['kind']=='notice'
st,_,raw=api('/v1/landmarks','POST',{'name':'Smoke Home','region_id':'genesis-central','x':70,'y':71,'z':23},at); assert st==201
landmark_id=json.loads(raw)['landmark']['id']; open('/tmp/ogl-landmark-id','w').write(landmark_id)
_,_,raw=api('/v1/landmarks',token=at); assert len(json.loads(raw)['landmarks'])==1
_,_,raw=api('/v1/notifications',token=bt); notices=json.loads(raw); assert notices['unread']>=3 and any(n['type']=='group_notice' for n in notices['notifications'])
notification_id=notices['notifications'][0]['id']; st,_,_=api('/v1/notifications/read','POST',{'notification_id':notification_id},bt); assert st==200
st,_,raw=api('/v1/viewer/teleport','POST',{'landmark_id':landmark_id},at); assert st==200 and json.loads(raw)['spawn']['x']==70
st,ctype,metrics=api('/metrics'); assert st==200 and ctype=='text/plain' and b'opengenesis_estates 1' in metrics and b'opengenesis_region_sim_fps' in metrics
# Bob is admitted because his group membership is embedded in his ticket
st,_,raw=api('/v1/viewer/session','POST',{'region':'genesis-central'},bt); assert st==200 and group_id in json.loads(raw)['groups']

# Avatar Appearance API
_,_,raw=api('/v1/avatar/appearance',token=at); ap=json.loads(raw); assert ap['revision']>=1 and ap['wearables']==[] and ap['attachments']==[]

# content remains part of the integrated user flow
payload=b'OpenGenesis 4.5.0 content payload'
st,_,raw=api('/v1/assets','POST',{'name':'one.txt','mime_type':'text/plain','data_base64':base64.b64encode(payload).decode()},at); assert st==201
asset_id=json.loads(raw)['asset']['id']; open('/tmp/ogl-asset-id','w').write(asset_id)
_,_,raw=api('/v1/inventory',token=at); root=json.loads(raw)['root']['id']
st,_,raw=api('/v1/inventory/folders','POST',{'parent_id':root,'name':'Objects'},at); assert st==201
folder=json.loads(raw)['folder']['id']
st,_,_=api('/v1/inventory/items','POST',{'parent_id':folder,'asset_id':asset_id,'name':'One Asset'},at); assert st==201
st,body=hg_form('/xinventory',{'METHOD':'GETINVENTORYSKELETON','PRINCIPAL':hgtravel['agent_id']}); assert st==200 and '<FOLDERS type="List">' in body
st,_,raw=api('/v1/assets/transfer','POST',{'asset_id':asset_id,'recipient_id':buid,'keep_copy':True},at); assert st==200
transferred_id=json.loads(raw)['asset']['id']; open('/tmp/ogl-transferred-id','w').write(transferred_id)
_,_,raw=api('/v1/inventory',token=bt); assert len(json.loads(raw)['items'])==1

# moderation + audit are admin-key protected
st,_,raw=api('/v1/admin/moderation/ban','POST',{'user_id':buid,'scope':'region','scope_id':'genesis-east','reason':'smoke-test'},admin=True); assert st==201
ban_id=json.loads(raw)['ban_id']
try:
    api('/v1/viewer/teleport','POST',{'region':'genesis-east'},bt)
    raise AssertionError('banned teleport unexpectedly allowed')
except urllib.error.HTTPError as e:
    assert e.code==403
st,_,_=api('/v1/admin/moderation/unban','POST',{'ban_id':ban_id},admin=True); assert st==200
_,_,raw=api('/v1/admin/audit',admin=True); assert len(json.loads(raw)['events'])>=5

# adjacency is already known by Core
_,_,raw=api('/v1/regions/genesis-central/neighbors'); neighbors=json.loads(raw)['neighbors']; assert [x['id'] for x in neighbors]==['genesis-east']

# unsigned join is rejected
bad,_=join('genesis-central','',False); bad.close()
# authenticated Alice enters central
st,_,raw=api('/v1/viewer/session','POST',{'region':'genesis-central'},at); assert st==200
viewer=json.loads(raw); ticket=viewer['scene_ticket']; assert 'scene.move' in viewer['capabilities']
s,p=join('genesis-central',ticket); j=fields(p); assert j['user_id']==auid and 'scene.move' in j['capabilities']
# replay rejected
replay,_=join('genesis-central',ticket,False); replay.close()
# object/terrain persistence
send(s,110,3,'name=One Persistent Cube\nx=130\ny=128\nz=30\nphysical=false\n'); t,_,p=recv(s); assert t==111
obj=int(fields(p)['id']); open('/tmp/ogl-object-id','w').write(str(obj))
send(s,142,4,'x=5\ny=6\nheight=29.25\n'); assert recv(s)[0]==143
# native avatar movement and boundary detection
send(s,150,5,'x=300\ny=128\nz=23\nvx=4\nvy=0\nvz=0\n'); t,_,p=recv(s); assert t==151 and fields(p)['boundary']=='east',(t,p)
time.sleep(3)
_,_,raw=api('/v1/presence',token=at); prs=json.loads(raw)['presences']; assert len(prs)==1 and prs[0]['user_id']==auid and prs[0]['region_id']=='genesis-central'

# client-driven adjacent-region handoff
st,_,raw=api('/v1/viewer/handoff','POST',{'from_region':'genesis-central','to_region':'genesis-east'},at); assert st==200
handoff=json.loads(raw); assert handoff['handoff_from']=='genesis-central' and handoff['region']['id']=='genesis-east'
send(s,42,6,''); s.close(); time.sleep(1)
e,p=join('genesis-east',handoff['scene_ticket']); ej=fields(p); assert ej['handoff_from']=='genesis-central'
time.sleep(3)
_,_,raw=api('/v1/presence',token=at); prs=json.loads(raw)['presences']; assert len(prs)==1 and prs[0]['region_id']=='genesis-east'
send(e,42,3,''); e.close(); time.sleep(2)

_,_,raw=api('/v1/status'); status=json.loads(raw); assert status['version']=='4.5.0' and len(status['regions'])==2 and status['friendships']==1 and status['messages']==2 and status['groups']==1 and status['parcels']==1 and status['estates']==1 and status['landmarks']==1 and status['notifications']>=4 and status['group_posts']==1
open('/tmp/ogl-generation','w').write(str(status['world_nodes'][0]['generation']))
open('/tmp/ogl-ticks','w').write(str(status['regions'][0]['ticks']))
PY

GEN1=$(cat /tmp/ogl-generation); TICKS1=$(cat /tmp/ogl-ticks)
stop_pid "$CORE"; sleep 2
./build/dev/opengenesis-core "$CORE_CFG" > /tmp/ogl-core2.log 2>&1 & CORE=$!
sleep 5
python3 - "$GEN1" "$TICKS1" <<'PY'
import base64,json,os,sys,urllib.request
A=f"http://127.0.0.1:{os.environ['ADMIN_PORT']}"; at=open('/tmp/ogl-auth-token').read(); bt=open('/tmp/ogl-bob-token').read(); aid=open('/tmp/ogl-asset-id').read()
def api(path,method='GET',body=None,token=None):
 d=None if body is None else json.dumps(body).encode(); h={}
 if d is not None:h['Content-Type']='application/json'
 if token:h['Authorization']='Bearer '+token
 with urllib.request.urlopen(urllib.request.Request(A+path,data=d,headers=h,method=method),timeout=6) as r:return json.load(r)
s=api('/v1/status'); assert s['world_nodes'][0]['state']=='online' and s['world_nodes'][0]['generation']>int(sys.argv[1])
assert max(r['ticks'] for r in s['regions'])>int(sys.argv[2]); assert s['friendships']==1 and s['messages']==2 and s['estates']==1 and s['landmarks']==1 and s['group_posts']==1
assert api('/v1/auth/me',token=at)['user']['username']=='alice.smoke'
assert len(api('/v1/social/friends',token=at)['friends'])==1
assert len(api('/v1/landmarks',token=at)['landmarks'])==1
assert len(api('/v1/estates',token=at)['estates'])==1
assert len(api('/v1/notifications',token=bt)['notifications'])>=3
assert api('/v1/social/messages',token=bt)['unread']==0
assert len(api('/v1/inventory',token=at)['items'])==1
assert len(api('/v1/inventory',token=bt)['items'])==1
assert base64.b64decode(api('/v1/assets/'+aid,token=at)['data_base64'])==b'OpenGenesis 4.5.0 content payload'
PY

# Full World restart validates scene/terrain persistence across both-region runtime recreation.
stop_pid "$WORLD"; sleep 2
./build/dev/opengenesis-world "$WORLD_CFG" > /tmp/ogl-world2.log 2>&1 & WORLD=$!
sleep 4
python3 - <<'PY'
import json,os,socket,struct,urllib.request
A=f"http://127.0.0.1:{os.environ['ADMIN_PORT']}"; P=int(os.environ['SCENE_PORT']); t=open('/tmp/ogl-auth-token').read(); uid=open('/tmp/ogl-user-id').read(); oid=int(open('/tmp/ogl-object-id').read())
def api(path,method='GET',body=None):
 d=None if body is None else json.dumps(body).encode(); h={'Authorization':'Bearer '+t}
 if d is not None:h['Content-Type']='application/json'
 with urllib.request.urlopen(urllib.request.Request(A+path,data=d,headers=h,method=method),timeout=6) as r:return json.load(r)
def rx(s,n):
 b=b''
 while len(b)<n:
  p=s.recv(n-len(b)); assert p; b+=p
 return b
def send(s,ty,r,p=''): b=p.encode(); s.sendall(struct.pack('>4sHHII',b'OGL1',1,ty,r,len(b))+b)
def recv(s):
 _,_,ty,r,n=struct.unpack('>4sHHII',rx(s,16)); return ty,r,rx(s,n).decode() if n else ''
v=api('/v1/viewer/session','POST',{'region':'genesis-central'}); s=socket.create_connection(('127.0.0.1',P),timeout=5)
send(s,1,1,'client=restart-check\nprotocol=1\n'); assert recv(s)[0]==2
send(s,100,2,f"region=genesis-central\nticket={v['scene_ticket']}\n"); assert recv(s)[0]==101
send(s,102,3,''); ty,_,snap=recv(s); assert ty==103 and f'entity={oid}|object|One Persistent Cube|' in snap and ('|'+uid+'|') in snap
send(s,140,4,'x=5\ny=6\n'); ty,_,sample=recv(s); assert ty==141 and 'height=29.250' in sample
send(s,42,5,''); s.close()
PY

python3 - <<'PY'
import json,os,urllib.request,urllib.error
A=f"http://127.0.0.1:{os.environ['ADMIN_PORT']}"
for f in ['/tmp/ogl-auth-token','/tmp/ogl-bob-token']:
 t=open(f).read(); req=urllib.request.Request(A+'/v1/auth/logout',data=b'{}',headers={'Authorization':'Bearer '+t,'Content-Type':'application/json'},method='POST')
 with urllib.request.urlopen(req,timeout=5) as r: assert json.load(r)['status']=='logged-out'
PY

stop_pid "$WORLD"; sleep 2
python3 - <<'PY'
import json,os,urllib.request
d=json.load(urllib.request.urlopen(f"http://127.0.0.1:{os.environ['ADMIN_PORT']}/v1/status")); assert all(r['state']=='offline' for r in d['regions']); assert d['identities']==2 and d['active_sessions']==0 and d['friendships']==1 and d['messages']==2 and d['groups']==1 and d['parcels']==1 and d['estates']==1 and d['landmarks']==1 and d['group_posts']==1
PY

echo "OpenGenesisLINK 4.5.0-dev integrated world/social/federation/hypergrid services smoke test: PASS"
