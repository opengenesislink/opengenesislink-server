#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

DEV_VERSION="$(tr -d '\r\n' < VERSION)"
NUMERIC_VERSION="${DEV_VERSION%-dev}"

python3 - "$DEV_VERSION" "$NUMERIC_VERSION" <<'PY'
from pathlib import Path
import sys

dev_version=sys.argv[1]
numeric_version=sys.argv[2]
path=Path("scripts/smoke-test.sh")
text=path.read_text()
text=text.replace("6.0.0-dev", dev_version)
text=text.replace("6.0.0", numeric_version)
text=text.replace("'region-handoff-v1'", "'region-handoff-v2'")
text=text.replace("'crossing-v2'", "'crossing-v3','crossing-reservation-v1'")

old_handoff="""st,_,raw=api('/v1/viewer/handoff','POST',{'from_region':'genesis-central','to_region':'genesis-east','vx':4.0,'vy':0.0,'vz':0.0},at); assert st==200"""
new_handoff="""st,_,raw=api('/v1/viewer/handoff','POST',{'from_region':'genesis-central','to_region':'genesis-east','vx':4.0,'vy':0.0,'vz':0.0,'rx':0.0,'ry':0.0,'rz':90.0,'avx':0.0,'avy':0.0,'avz':1.25},at); assert st==200"""
text=text.replace(old_handoff,new_handoff)

old_complete="""st,_,raw=api('/v1/viewer/handoff/complete','POST',{'crossing_id':handoff['crossing_id'],'region':'genesis-east'},at); assert st==200
crossing=json.loads(raw); assert crossing['state']=='completed' and crossing['velocity']['x']==4.0 and crossing['attachment_state'] is not None and crossing['script_state'] is not None
try:
    api('/v1/viewer/handoff/complete','POST',{'crossing_id':handoff['crossing_id'],'region':'genesis-east'},at)
    raise AssertionError('crossing replay accepted')
except urllib.error.HTTPError as ex:
    assert ex.code==409"""
new_complete="""st,_,raw=api('/v1/viewer/handoff/reserve','POST',{'crossing_id':handoff['crossing_id'],'region':'genesis-east'},at); assert st==200
reserved=json.loads(raw); assert reserved['state']=='reserved' and reserved['reservation_token']
reservation_token=reserved['reservation_token']
st,_,raw=api('/v1/viewer/handoff/complete','POST',{'crossing_id':handoff['crossing_id'],'region':'genesis-east','reservation_token':reservation_token},at); assert st==200
crossing=json.loads(raw); assert crossing['state']=='completed' and crossing['velocity']['x']==4.0 and crossing['rotation']['z']==90.0 and crossing['angular_velocity']['z']==1.25 and crossing['attachment_state'] is not None and crossing['script_state'] is not None and crossing['physics_state'] is not None
try:
    api('/v1/viewer/handoff/complete','POST',{'crossing_id':handoff['crossing_id'],'region':'genesis-east','reservation_token':reservation_token},at)
    raise AssertionError('crossing replay accepted')
except urllib.error.HTTPError as ex:
    assert ex.code==409"""
if old_complete not in text:
    raise SystemExit("handoff completion smoke pattern not found")
text=text.replace(old_complete,new_complete)
path.write_text(text)
PY

bash ./scripts/smoke-test.sh
