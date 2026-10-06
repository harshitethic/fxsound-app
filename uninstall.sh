#!/usr/bin/env bash
set -euo pipefail

if [[ "$(uname -s)" == "Darwin" ]]; then
  ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
  exec "$ROOT/macos/uninstall.sh" "$@"
fi

PURGE=0
[[ "${1:-}" == "--purge" ]] && PURGE=1

export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/run/user/$(id -u)}"
export DBUS_SESSION_BUS_ADDRESS="${DBUS_SESSION_BUS_ADDRESS:-unix:path=$XDG_RUNTIME_DIR/bus}"

systemctl --user disable --now fxsound-linux.service >/dev/null 2>&1 || true

rm -f \
  "$HOME/.config/systemd/user/fxsound-linux.service" \
  "$HOME/.local/bin/fxsound-linux" \
  "$HOME/.local/share/applications/fxsound-linux.desktop" \
  "$HOME/.local/share/icons/hicolor/256x256/apps/fxsound-linux.png"
rm -rf "$HOME/.local/libexec/fxsound-linux"
rm -rf "$HOME/.local/share/fxsound-linux"

if [[ "$PURGE" -eq 1 ]]; then
  rm -rf "$HOME/.config/fxsound-linux"
fi

systemctl --user daemon-reload >/dev/null 2>&1 || true

if command -v wpctl >/dev/null 2>&1; then
  sink_id="$(
    wpctl status 2>/dev/null |
      awk '
        /Sinks:/{inside=1; next}
        /Sources:/{inside=0}
        inside && $0 !~ /FxSound/ {
          if (match($0, /[0-9]+\./)) {
            print substr($0, RSTART, RLENGTH-1)
            exit
          }
        }'
  )"
  [[ -n "$sink_id" ]] && wpctl set-default "$sink_id" >/dev/null 2>&1 || true
fi

echo "FxSound Linux removed."
if [[ "$PURGE" -eq 0 ]]; then
  echo "Settings kept in ~/.config/fxsound-linux (use ./uninstall.sh --purge to remove them too)."
fi
