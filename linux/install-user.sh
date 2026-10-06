#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
LINUX_DIR="$ROOT/linux"
DAEMON="${FXSOUND_DAEMON:-$LINUX_DIR/build-prod-core/fxsound_pipewire}"
UI="${FXSOUND_UI:-$LINUX_DIR/build-ui/fxsound_linux_ui_artefacts/Release/FxSound Linux}"

if [[ ! -x "$DAEMON" ]]; then
  echo "Missing daemon build: $DAEMON" >&2
  exit 1
fi
if [[ ! -x "$UI" ]]; then
  echo "Missing UI build: $UI" >&2
  exit 1
fi

BIN_DIR="$HOME/.local/bin"
LIBEXEC_DIR="$HOME/.local/libexec/fxsound-linux"
DATA_DIR="$HOME/.local/share/fxsound-linux/presets"
APP_DIR="$HOME/.local/share/applications"
ICON_DIR="$HOME/.local/share/icons/hicolor/256x256/apps"
SYSTEMD_DIR="$HOME/.config/systemd/user"

mkdir -p "$BIN_DIR" "$LIBEXEC_DIR" "$DATA_DIR" "$APP_DIR" "$ICON_DIR" "$SYSTEMD_DIR"

install -m 0755 "$DAEMON" "$LIBEXEC_DIR/fxsoundd"
install -m 0755 "$UI" "$LIBEXEC_DIR/fxsound-ui"
install -m 0755 "$LINUX_DIR/fxsound-rt-priority.sh" "$LIBEXEC_DIR/fxsound-rt-priority"

declare -A PRESETS=(
  [1.fac]="General.fac"
  [2.fac]="Music.fac"
  [3.fac]="Voice.fac"
  [4.fac]="Volume_Boost.fac"
  [5.fac]="Gaming.fac"
  [6.fac]="Classic_Processing.fac"
  [7.fac]="Light_Processing.fac"
  [8.fac]="Bass_Boost.fac"
  [9.fac]="Streaming_Video.fac"
  [10.fac]="Movies.fac"
  [11.fac]="TV.fac"
  [12.fac]="Transcription.fac"
  [Default.fac]="Default.fac"
)

for source in "${!PRESETS[@]}"; do
  install -m 0644 "$LINUX_DIR/factory-presets/$source" "$DATA_DIR/${PRESETS[$source]}"
done

install -m 0644 "$ROOT/fxsound/Images/fxsound.png" "$ICON_DIR/fxsound-linux.png"

cat >"$BIN_DIR/fxsound-linux" <<'EOF'
#!/bin/sh
systemctl --user start fxsound-linux.service >/dev/null 2>&1 || true
exec "$HOME/.local/libexec/fxsound-linux/fxsound-ui" "$@"
EOF
chmod 0755 "$BIN_DIR/fxsound-linux"

cat >"$SYSTEMD_DIR/fxsound-linux.service" <<'EOF'
[Unit]
Description=FxSound Linux real-time DSP
After=pipewire.service wireplumber.service
Wants=pipewire.service

[Service]
Type=simple
ExecStart=%h/.local/libexec/fxsound-linux/fxsoundd
ExecStartPost=%h/.local/libexec/fxsound-linux/fxsound-rt-priority
Environment=FXSOUND_SINK_NAME=fxsound_sink
Environment=FXSOUND_SINK_DESCRIPTION=FxSound
Environment=FXSOUND_OUTPUT_NAME=fxsound_processed_output
Restart=on-failure
RestartSec=1

[Install]
WantedBy=default.target
EOF

cat >"$APP_DIR/fxsound-linux.desktop" <<'EOF'
[Desktop Entry]
Type=Application
Name=FxSound Linux
GenericName=Audio Enhancer
Comment=FxSound real-time audio enhancement for Linux
Exec=%h/.local/bin/fxsound-linux
Icon=fxsound-linux
Terminal=false
Categories=AudioVideo;Audio;
StartupNotify=true
EOF
# Desktop files do not expand %h in Exec on all desktop environments.
sed -i "s|Exec=%h/|Exec=$HOME/|" "$APP_DIR/fxsound-linux.desktop"

export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/run/user/$(id -u)}"
export DBUS_SESSION_BUS_ADDRESS="${DBUS_SESSION_BUS_ADDRESS:-unix:path=$XDG_RUNTIME_DIR/bus}"

systemctl --user daemon-reload
systemctl --user enable --now fxsound-linux.service

for _ in $(seq 1 50); do
  if [[ -S "$XDG_RUNTIME_DIR/fxsound-linux/control.sock" ]]; then
    break
  fi
  sleep 0.1
done

if command -v wpctl >/dev/null 2>&1; then
  fx_id="$(
    wpctl status 2>/dev/null |
      awk '/Sinks:/{inside=1;next}/Sources:/{inside=0} inside && /FxSound/{gsub(/[^0-9]/,"",$2); print $2; exit}'
  )"
  if [[ -n "$fx_id" ]]; then
    wpctl set-default "$fx_id"
  fi
fi

command -v update-desktop-database >/dev/null 2>&1 &&
  update-desktop-database "$APP_DIR" >/dev/null 2>&1 || true

echo "FxSound Linux installed."
echo "Daemon: $LIBEXEC_DIR/fxsoundd"
echo "UI:     $LIBEXEC_DIR/fxsound-ui"
echo "Launch: $BIN_DIR/fxsound-linux"