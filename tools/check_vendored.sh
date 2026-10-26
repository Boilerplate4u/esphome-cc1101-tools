#!/usr/bin/env bash
# Verifies that the Oregon v2.1 files in components/rf_sniffer are unchanged
# copies of oregon_v21_receiver at the commit named in their header.
#
# Usage: tools/check_vendored.sh [path to an oregon_v21_receiver checkout]
# Without a path the files are downloaded from GitHub.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DIR="$ROOT/components/rf_sniffer"
REPO="boilerplate4u/oregon_v21_receiver"
FILES="manchester_decoder.h manchester_decoder.cpp oregon_protocol.h oregon_protocol.cpp"
LOCAL="${1:-}"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

status=0
for f in $FILES; do
  commit="$(sed -n '1s/.*(commit \([0-9a-f]\{7,40\}\),.*/\1/p' "$DIR/$f")"
  if [ -z "$commit" ]; then
    echo "FAIL $f: no commit in header"
    status=1
    continue
  fi
  upstream="$TMP/$f"
  if [ -n "$LOCAL" ]; then
    git -C "$LOCAL" show "$commit:components/oregon_v21_receiver/$f" > "$upstream"
  else
    curl -fsSL "https://raw.githubusercontent.com/$REPO/$commit/components/oregon_v21_receiver/$f" -o "$upstream"
  fi
  # Drop the 3 line vendoring header and undo the namespace change.
  tail -n +4 "$DIR/$f" | sed 's/namespace rf_sniffer/namespace oregon_v21_receiver/g' > "$TMP/$f.local"
  if diff -u "$upstream" "$TMP/$f.local" > "$TMP/$f.diff"; then
    echo "OK   $f ($commit)"
  else
    echo "FAIL $f differs from $REPO@$commit:"
    cat "$TMP/$f.diff"
    status=1
  fi
done
exit $status
