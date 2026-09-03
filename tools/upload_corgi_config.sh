#!/bin/bash
# Upload a FluidNC config to the Corgi over the WebUI, with a backup first.
#
# The Corgi boots /config.yaml from LocalFS, so whatever is uploaded lands
# under that name regardless of the local filename. Nothing takes effect until
# the controller reboots ($Bye or power cycle).
#
#   ./tools/upload_corgi_config.sh <local.yaml> [host]
#
# The multipart shape is ESP3D's: a `path` field, a `<remote>S` field carrying
# the byte count so the far end can reject a truncated write, and the file
# itself under `myfiles`. Getting any of the three wrong fails silently with a
# 200, which is why the verify step at the end is not optional.
set -euo pipefail

SRC="${1:?usage: upload_corgi_config.sh <local.yaml> [host]}"
HOST="${2:-192.168.0.5}"
UA="Mozilla/5.0"

[ -f "$SRC" ] || { echo "no such file: $SRC" >&2; exit 1; }
python3 -c "import yaml,sys; yaml.safe_load(open(sys.argv[1]))" "$SRC" \
  || { echo "refusing to upload: $SRC is not valid YAML" >&2; exit 1; }

STAMP=$(date +%Y-%m-%d_%H%M%S)
BACKUP="$(dirname "$SRC")/corgi_config.backup-$STAMP.yaml"
echo "backing up live config -> $BACKUP"
curl -fsS -A "$UA" "http://$HOST/config.yaml" -o "$BACKUP"

SZ=$(stat -c%s "$SRC")
echo "uploading $SRC ($SZ bytes) -> $HOST:/config.yaml"
curl -fsS -A "$UA" \
  -F "path=/" \
  -F "/config.yamlS=$SZ" \
  -F "myfiles=@$SRC;filename=config.yaml" \
  "http://$HOST/files" >/dev/null

# Read it back rather than trusting the response. A short write here means a
# controller that will not boot.
VERIFY=$(mktemp)
trap 'rm -f "$VERIFY"' EXIT
curl -fsS -A "$UA" "http://$HOST/config.yaml" -o "$VERIFY"
if cmp -s "$SRC" "$VERIFY"; then
    echo "verified: $HOST:/config.yaml matches $SRC"
    echo "reboot the controller (\$Bye or power cycle) to apply"
else
    echo "MISMATCH: what landed differs from what was sent." >&2
    echo "restore with: $0 $BACKUP $HOST" >&2
    diff <(head -40 "$SRC") <(head -40 "$VERIFY") >&2 || true
    exit 1
fi
