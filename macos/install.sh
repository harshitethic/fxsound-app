#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
MAC_DIR="$ROOT/macos"
JUCE_DIR="$MAC_DIR/.deps/JUCE"
BUILD_DIR="$MAC_DIR/build-release"

APP="$HOME/Applications/FxSound.app"
APP_SUPPORT="$HOME/Library/Application Support/FxSound"
BIN_DIR="$APP_SUPPORT/bin"
PRESET_DIR="$APP_SUPPORT/presets"
LOG_DIR="$APP_SUPPORT/logs"
AUDIO_LABEL="com.harshitethic.fxsound.audio"
UI_LABEL="com.harshitethic.fxsound.ui"
AUDIO_PLIST="$HOME/Library/LaunchAgents/$AUDIO_LABEL.plist"
UI_PLIST="$HOME/Library/LaunchAgents/$UI_LABEL.plist"
DAEMON="$BIN_DIR/fxsoundd-v0.2.2"

if [[ "$(uname -s)" != "Darwin" ]]; then
  echo "This installer is for macOS. Run ./install.sh from the repository root on Linux." >&2
  exit 1
fi

echo "==> FxSound macOS installer"
echo "    Unofficial community port — upstream DfxDsp + CoreAudio/BlackHole"

if ! xcode-select -p >/dev/null 2>&1; then
  echo "Xcode Command Line Tools are required." >&2
  echo "Run: xcode-select --install" >&2
  echo "Then rerun ./install.sh" >&2
  exit 1
fi

need_brew=0
command -v cmake >/dev/null 2>&1 || need_brew=1
command -v git >/dev/null 2>&1 || need_brew=1
[[ -d /Library/Audio/Plug-Ins/HAL/BlackHole2ch.driver ]] || need_brew=1

if [[ "$need_brew" -eq 1 ]] && ! command -v brew >/dev/null 2>&1; then
  cat >&2 <<'EOF'
Homebrew is required to install missing macOS build/audio dependencies.
Install Homebrew from https://brew.sh, then rerun:

  ./install.sh
EOF
  exit 1
fi

if ! command -v cmake >/dev/null 2>&1; then
  echo "==> Installing CMake"
  brew install cmake
fi

if ! command -v git >/dev/null 2>&1; then
  echo "==> Installing Git"
  brew install git
fi

needs_reboot=0
if [[ ! -d /Library/Audio/Plug-Ins/HAL/BlackHole2ch.driver ]]; then
  echo "==> Installing BlackHole 2ch"
  echo "    macOS may ask for your administrator password."
  HOMEBREW_NO_AUTO_UPDATE=1 brew install --cask blackhole-2ch
  needs_reboot=1
fi

if [[ ! -f "$JUCE_DIR/CMakeLists.txt" ]]; then
  echo "==> Fetching JUCE 6.1.6"
  mkdir -p "$(dirname "$JUCE_DIR")"
  rm -rf "$JUCE_DIR"
  git clone --depth 1 --branch 6.1.6 https://github.com/juce-framework/JUCE.git "$JUCE_DIR"
fi

echo "==> Applying JUCE compatibility patch"
python3 "$MAC_DIR/patch_juce_macos26.py" "$JUCE_DIR"

echo "==> Configuring FxSound"
cmake -S "$MAC_DIR" -B "$BUILD_DIR" \
  -DCMAKE_BUILD_TYPE=Release \
  -DFXSOUND_JUCE_DIR="$JUCE_DIR"

echo "==> Building FxSound DSP, CoreAudio daemon and desktop app"
cmake --build "$BUILD_DIR" --parallel

echo "==> Running DSP checks"
"$BUILD_DIR/dsp_smoke"
"$BUILD_DIR/preset_audio_test" "$ROOT/linux/factory-presets/1.fac"

echo "==> Installing for $USER"
mkdir -p \
  "$HOME/Applications" \
  "$BIN_DIR" \
  "$PRESET_DIR" \
  "$LOG_DIR" \
  "$APP_SUPPORT/state" \
  "$APP_SUPPORT/autosave" \
  "$HOME/Library/LaunchAgents"

ui_src="$BUILD_DIR/fxsound_macos_ui_artefacts/Release/FxSound.app"
if [[ ! -d "$ui_src" ]]; then
  echo "Built FxSound.app was not found at: $ui_src" >&2
  exit 1
fi

rm -rf "$APP"
cp -R "$ui_src" "$APP"
codesign --force --deep --sign - "$APP" >/dev/null

install -m 0755 "$BUILD_DIR/fxsound_coreaudio" "$DAEMON"
codesign --force --sign - "$DAEMON" >/dev/null
ln -sfn "$(basename "$DAEMON")" "$BIN_DIR/fxsoundd"

# The daemon uses friendly filenames while preserving byte-identical upstream
# factory preset payloads.
install -m 0644 "$ROOT/linux/factory-presets/1.fac"       "$PRESET_DIR/General.fac"
install -m 0644 "$ROOT/linux/factory-presets/2.fac"       "$PRESET_DIR/Music.fac"
install -m 0644 "$ROOT/linux/factory-presets/3.fac"       "$PRESET_DIR/Voice.fac"
install -m 0644 "$ROOT/linux/factory-presets/4.fac"       "$PRESET_DIR/Volume_Boost.fac"
install -m 0644 "$ROOT/linux/factory-presets/5.fac"       "$PRESET_DIR/Gaming.fac"
install -m 0644 "$ROOT/linux/factory-presets/6.fac"       "$PRESET_DIR/Classic_Processing.fac"
install -m 0644 "$ROOT/linux/factory-presets/7.fac"       "$PRESET_DIR/Light_Processing.fac"
install -m 0644 "$ROOT/linux/factory-presets/8.fac"       "$PRESET_DIR/Bass_Boost.fac"
install -m 0644 "$ROOT/linux/factory-presets/9.fac"       "$PRESET_DIR/Streaming_Video.fac"
install -m 0644 "$ROOT/linux/factory-presets/10.fac"      "$PRESET_DIR/Movies.fac"
install -m 0644 "$ROOT/linux/factory-presets/11.fac"      "$PRESET_DIR/TV.fac"
install -m 0644 "$ROOT/linux/factory-presets/12.fac"      "$PRESET_DIR/Transcription.fac"
install -m 0644 "$ROOT/linux/factory-presets/Default.fac" "$PRESET_DIR/Default.fac"

cat > "$AUDIO_PLIST" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>Label</key>
  <string>$AUDIO_LABEL</string>
  <key>ProgramArguments</key>
  <array>
    <string>$DAEMON</string>
  </array>
  <key>RunAtLoad</key>
  <true/>
  <key>KeepAlive</key>
  <dict>
    <key>SuccessfulExit</key>
    <false/>
  </dict>
  <key>ThrottleInterval</key>
  <integer>3</integer>
  <key>ProcessType</key>
  <string>Interactive</string>
  <key>StandardOutPath</key>
  <string>$LOG_DIR/daemon.log</string>
  <key>StandardErrorPath</key>
  <string>$LOG_DIR/daemon.err</string>
</dict>
</plist>
PLIST

cat > "$UI_PLIST" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>Label</key>
  <string>$UI_LABEL</string>
  <key>ProgramArguments</key>
  <array>
    <string>/usr/bin/open</string>
    <string>-g</string>
    <string>$APP</string>
    <string>--args</string>
    <string>--minimized</string>
  </array>
  <key>RunAtLoad</key>
  <true/>
  <key>LimitLoadToSessionType</key>
  <string>Aqua</string>
</dict>
</plist>
PLIST

plutil -lint "$AUDIO_PLIST" >/dev/null
plutil -lint "$UI_PLIST" >/dev/null

if ! system_profiler SPAudioDataType 2>/dev/null | grep -q "BlackHole 2ch:"; then
  needs_reboot=1
fi

if [[ "$needs_reboot" -eq 1 ]]; then
  cat <<'EOF'

FxSound has been built and installed.

BlackHole was newly installed (or is not visible to CoreAudio yet), so macOS
must be restarted once before FxSound can process system audio.

After restarting, run:

  ./install.sh

The second run will verify the build and enable the audio/UI login services.
EOF
  exit 0
fi

echo "==> Enabling FxSound login services"
uid="$(id -u)"
launchctl bootout "gui/$uid/$UI_LABEL" >/dev/null 2>&1 || true
launchctl bootout "gui/$uid/$AUDIO_LABEL" >/dev/null 2>&1 || true

# Give an older daemon time to restore the physical output and release CoreAudio.
for _ in $(seq 1 60); do
  pgrep -f "$APP_SUPPORT/bin/fxsoundd" >/dev/null 2>&1 || break
  sleep 0.1
done

rm -f "$HOME/Library/Caches/FxSound/control.sock"
launchctl bootstrap "gui/$uid" "$AUDIO_PLIST"

socket="$HOME/Library/Caches/FxSound/control.sock"
for _ in $(seq 1 100); do
  [[ -S "$socket" ]] && break
  sleep 0.1
done

if [[ ! -S "$socket" ]]; then
  echo "FxSound audio service started but its control socket did not become ready." >&2
  echo "See: $LOG_DIR/daemon.err" >&2
  exit 1
fi

launchctl bootstrap "gui/$uid" "$UI_PLIST" >/dev/null 2>&1 || true
launchctl kickstart "gui/$uid/$UI_LABEL" >/dev/null 2>&1 || true

echo
echo "FxSound for macOS is installed."
echo "App: $APP"
echo "Audio service: $AUDIO_LABEL"
echo "The UI starts minimized at login and your FxSound settings are persisted."
echo
echo "Open FxSound now with:"
echo "  open \"$APP\""
