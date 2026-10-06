#!/usr/bin/env bash
set -euo pipefail

PURGE=0
[[ "${1:-}" == "--purge" ]] && PURGE=1

if [[ "$(uname -s)" != "Darwin" ]]; then
  echo "This uninstaller is for macOS." >&2
  exit 1
fi

uid="$(id -u)"
APP="$HOME/Applications/FxSound.app"
APP_SUPPORT="$HOME/Library/Application Support/FxSound"
AUDIO_LABEL="com.harshitethic.fxsound.audio"
UI_LABEL="com.harshitethic.fxsound.ui"

echo "==> Stopping FxSound"
launchctl bootout "gui/$uid/$UI_LABEL" >/dev/null 2>&1 || true
launchctl bootout "gui/$uid/$AUDIO_LABEL" >/dev/null 2>&1 || true

# Allow the daemon to restore the original physical output on clean shutdown.
for _ in $(seq 1 60); do
  pgrep -f "$APP_SUPPORT/bin/fxsoundd" >/dev/null 2>&1 || break
  sleep 0.1
done

# A daemon stuck inside a CoreAudio HAL call cannot handle SIGTERM. Do not leave
# an orphaned process behind during uninstall.
for pid in $(pgrep -f "$APP_SUPPORT/bin/fxsoundd" 2>/dev/null || true); do
  kill -KILL "$pid" >/dev/null 2>&1 || true
done

rm -f \
  "$HOME/Library/LaunchAgents/$AUDIO_LABEL.plist" \
  "$HOME/Library/LaunchAgents/$UI_LABEL.plist"

rm -rf "$APP"

if [[ "$PURGE" -eq 1 ]]; then
  rm -rf "$APP_SUPPORT"
else
  rm -rf "$APP_SUPPORT/bin" "$APP_SUPPORT/presets" "$APP_SUPPORT/logs"
fi

echo "FxSound for macOS removed."
if [[ "$PURGE" -eq 0 ]]; then
  echo "Settings were kept in: $APP_SUPPORT"
  echo "Use ./uninstall.sh --purge to remove saved settings too."
fi

if system_profiler SPAudioDataType 2>/dev/null | grep -A12 "BlackHole 2ch:" | grep -q "Default Output Device: Yes"; then
  echo
  echo "Note: macOS still reports BlackHole as the default output."
  echo "Choose your speakers in System Settings > Sound > Output."
fi

echo
echo "BlackHole was not removed because other audio apps may use it."
echo "To remove it separately: brew uninstall --cask blackhole-2ch"
