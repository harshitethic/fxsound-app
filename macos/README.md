# FxSound for macOS

This directory contains the **unofficial community macOS port of FxSound**.

It keeps the real upstream FxSound **DfxDsp** engine and original factory
presets, replaces the Windows audio-driver/WASAPI layer with CoreAudio, and
uses a native JUCE macOS desktop app.

## Status

**v0.2.2-alpha**

Tested on:

- Apple Silicon (M1)
- macOS 26.6
- MacBook Pro built-in speakers
- 48 kHz stereo

The stable default system-audio backend is **BlackHole 2ch**. An experimental
native CoreAudio process-tap implementation is included in source, but is
disabled by default because the macOS 26.6 HAL can block during tap startup.

## Install

From the repository root:

```bash
git clone https://github.com/harshitethic/fxsound-app.git
cd fxsound-app
./install.sh
```

The root installer detects macOS automatically and calls
`macos/install.sh`.

The installer:

1. Checks Xcode Command Line Tools.
2. Installs missing CMake/BlackHole dependencies with Homebrew.
3. Downloads JUCE 6.1.6.
4. Applies the JUCE macOS 15/26 SDK compatibility patch.
5. Builds the upstream DfxDsp engine, CoreAudio daemon and FxSound app.
6. Runs DSP and factory-preset tests.
7. Installs `~/Applications/FxSound.app`.
8. Installs a user LaunchAgent for the audio daemon.
9. Installs a second LaunchAgent that opens the FxSound UI minimized at login.
10. Keeps preset/EQ/UI settings under `~/Library/Application Support/FxSound`.

If BlackHole is installed for the first time, macOS requires one restart.
After restarting, run `./install.sh` again.

## Requirements

- macOS
- Xcode Command Line Tools
- Homebrew when CMake or BlackHole is missing
- BlackHole 2ch
- 48 kHz stereo is the primary tested path

## Architecture

```text
macOS applications
       |
       v
BlackHole 2ch (system default)
       |
       v
Upstream FxSound DfxDsp
  - Clarity / Fidelity
  - Ambience
  - Surround
  - Dynamic Boost
  - Bass Boost
  - Graphic EQ
  - original .fac presets
       |
       v
Selected physical CoreAudio output
```

The daemon and UI communicate over:

```text
~/Library/Caches/FxSound/control.sock
```

## Windows-parity behavior

The current Mac build uses:

- 32-bit floating-point processing
- 48 kHz stereo
- ~40 ms / 1920-frame FxSound priming cadence
- Windows default 10-band EQ
- optional 5 / 10 / 15 / 20 / 31-band modes
- Clarity, Ambience, Surround, Dynamic Boost and Bass Boost
- Volume Leveling, Master Gain, Balance and Filter Q
- factory-preset modified/autosave state
- persistent output-device selection
- UI dark/light mode, compact/full mode and Always-on-Top
- minimized UI at login
- crash-restart LaunchAgent for the audio daemon

Factory `.fac` payloads come from the upstream FxSound repository.

## Experimental native system tap

The source contains a native CoreAudio process-tap backend. It is disabled by
default on the tested macOS 26.6 system because `AudioDeviceStart` can block
inside Apple's HAL for the private tap aggregate.

Developers can opt in manually:

```bash
FXSOUND_EXPERIMENTAL_NATIVE_TAP=1 \
  "$HOME/Library/Application Support/FxSound/bin/fxsoundd"
```

Do not enable this for normal use yet.

## Useful commands

Open the UI:

```bash
open "$HOME/Applications/FxSound.app"
```

Inspect the audio LaunchAgent:

```bash
launchctl print "gui/$(id -u)/com.harshitethic.fxsound.audio"
```

View daemon logs:

```bash
tail -f "$HOME/Library/Application Support/FxSound/logs/daemon.err"
```

## Uninstall

Keep settings:

```bash
./uninstall.sh
```

Remove app and saved settings:

```bash
./uninstall.sh --purge
```

The uninstaller intentionally does **not** remove BlackHole because other audio
software may use it.

## Limitations

- This is not an official FxSound macOS release.
- Stereo/48 kHz is the best-tested path.
- Bluetooth, HDMI, multichannel and unusual sample rates need broader testing.
- The final speaker stage can still differ from Windows because macOS/Apple
  hardware applies its own output processing.
- The native process-tap backend remains experimental.

See the root README for licensing and upstream attribution.
