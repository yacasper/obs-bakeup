#!/bin/sh
# Renders card.html to a PNG with headless Chrome.
#
#   ./export.sh                                   -> ../social-preview.png (the repository card)
#   ./export.sh out.png "title=Version%201.1&subtitle=New%20things&image=none"
#
# The second argument is the query string described at the top of card.html.
# Needs Google Chrome or Chromium; set CHROME to use another binary.
set -eu

HERE=$(cd "$(dirname "$0")" && pwd)
OUT=${1:-"$HERE/../social-preview.png"}
QUERY=${2:-}

if [ -z "${CHROME:-}" ]; then
  for candidate in \
    "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome" \
    "/Applications/Chromium.app/Contents/MacOS/Chromium" \
    "$(command -v google-chrome 2>/dev/null || true)" \
    "$(command -v chromium 2>/dev/null || true)"; do
    if [ -n "$candidate" ] && [ -x "$candidate" ]; then CHROME=$candidate; break; fi
  done
fi
[ -n "${CHROME:-}" ] || { echo "No Chrome or Chromium found; set CHROME=/path/to/browser" >&2; exit 1; }

URL="file://$HERE/card.html"
[ -n "$QUERY" ] && URL="$URL?$QUERY"

"$CHROME" --headless=new --disable-gpu --hide-scrollbars --force-device-scale-factor=1 \
  --window-size=1280,640 --virtual-time-budget=2000 --screenshot="$OUT" "$URL" >/dev/null 2>&1

echo "wrote $OUT"
