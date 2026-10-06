# FxSound for Linux & macOS

**Unofficial community ports of the open-source FxSound audio enhancer.**

This fork keeps the real upstream **FxSound DfxDsp** processing engine and
original factory presets, while replacing the Windows-only audio transport with
native platform backends:

- **Linux:** PipeWire
- **macOS:** CoreAudio + BlackHole 2ch

The goal is to keep platform-specific work at the audio/UI boundary so the
actual FxSound processing remains as close to upstream Windows behavior as
practical.

> **Status**
>
> - Linux: **v0.1.0-alpha**, tested on Kali Linux
> - macOS: **v0.2.2-alpha**, tested on Apple Silicon / macOS 26.6

![FxSound Linux](docs/screenshots/fxsound-linux-dark.jpg)

## Features

- Real upstream FxSound **DfxDsp**
- Original FxSound factory `.fac` presets
- 32-bit floating-point processing
- 48 kHz stereo primary path
- Windows-style ~40 ms / 1920-frame processing cadence
- Windows default **10-band EQ**
- Optional 5 / 10 / 15 / 20 / 31-band modes
- Clarity / Fidelity
- Ambience
- Surround
- Dynamic Boost
- Bass Boost
- Volume Leveling
- Master Gain
- Balance
- Filter Q
- Preset autosave / modified state
- Persistent output-device selection
- Dark / light UI
- Compact / full UI
- Always-on-Top preference
- Live visualizer
- User-level auto-start services

## Install

The same command works on supported Linux systems and macOS:

```bash
git clone https://github.com/harshitethic/fxsound-app.git
cd fxsound-app
./install.sh
```

The root installer detects the operating system automatically.

### macOS

The macOS installer builds FxSound from source, installs the app to:

```text
~/Applications/FxSound.app
```

and stores runtime files/settings under:

```text
~/Library/Application Support/FxSound
```

The stable system-audio path uses **BlackHole 2ch**. If BlackHole is being
installed for the first time, macOS requires one restart. After restarting,
run `./install.sh` again.

The FxSound UI starts **minimized at login**, while the audio daemon starts
automatically and keeps the last selected FxSound settings.

Full macOS notes: [macos/README.md](macos/README.md)

### Debian / Ubuntu / Kali

The Linux installer installs the required PipeWire/JUCE build dependencies,
builds the native daemon/UI, runs DSP tests and enables the user service.

Open it after installation with:

```bash
fxsound-linux
```

Full Linux notes: [linux/README.md](linux/README.md)

## Uninstall

Keep saved settings:

```bash
./uninstall.sh
```

Remove the app and saved settings:

```bash
./uninstall.sh --purge
```

On macOS, BlackHole is intentionally **not** removed because other audio
software may depend on it.

## Architecture

### Linux

```text
Linux applications
       |
       v
PipeWire virtual sink: FxSound
       |
       v
Upstream FxSound DfxDsp
       |
       v
Selected PipeWire hardware sink
```

### macOS

```text
macOS applications
       |
       v
BlackHole 2ch
       |
       v
Upstream FxSound DfxDsp
       |
       v
Selected physical CoreAudio output
```

The macOS source also includes an experimental native CoreAudio process-tap
backend. It is disabled by default on the tested macOS 26.6 system because
Apple's HAL can block while starting a private tap aggregate.

## Windows-parity work

The ports intentionally keep the upstream FxSound processing code rather than
reimplementing an approximate EQ.

Current parity work includes:

- upstream DfxDsp signal path
- upstream factory preset payloads
- 48 kHz stereo processing
- Windows-style ~40 ms buffer priming
- default 10-band EQ
- original effect controls
- persistent preset/EQ/audio-control state

The final output can still differ from Windows because PipeWire/CoreAudio,
hardware drivers and OS-level speaker processing are different.

## Build tests

Linux and macOS both have CI workflows that compile the native port and run:

```text
dsp_smoke
preset_audio_test
```

The macOS workflow also fetches JUCE 6.1.6 and applies the small SDK
compatibility patch used by the installer.

## Known limitations

- These are **unofficial community ports**, not official FxSound releases.
- Stereo is currently the best-tested path.
- Bluetooth, HDMI, 5.1/7.1 and unusual sample rates need broader testing.
- The macOS native process-tap backend is experimental.
- Linux automatic installation currently targets Debian-family distributions.
- macOS automatic installation assumes Homebrew when dependencies are missing.

## Contributing

PRs and tested-device reports are welcome, especially for:

- Bluetooth / HDMI validation
- multichannel audio
- adaptive 44.1 / 48 kHz handling
- packaging
- UI parity
- automated audio regression tests
- macOS native system-tap reliability

Please keep platform-specific changes outside the DSP algorithms wherever
possible.

## Upstream and license

This project is derived from
[FxSound](https://github.com/fxsound2/fxsound-app) and preserves its repository
history and copyright notices.

FxSound and this derivative are licensed under the **GNU Affero General Public
License v3.0 or later (AGPL-3.0-or-later)**. See [LICENSE](LICENSE).

This fork is not presented as an official FxSound Linux or macOS release.

---

### Built by [@harshitethic](https://github.com/harshitethic) with love.

And a lot of listening to the same songs over and over until Linux and macOS
stopped sounding different.
