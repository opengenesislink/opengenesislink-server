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
path.write_text(text)
PY

bash ./scripts/smoke-test.sh
