# FxSound Linux

An experimental native Linux port of the open-source FxSound application.

This port keeps FxSound's upstream **DfxDsp** processing engine and factory
`.fac` presets, replaces the Windows audio-passthrough layer with PipeWire,
and reuses the upstream JUCE controls/theme for the desktop UI.

> This is a derivative of FxSound and remains licensed under
> **GNU AGPL-3.0-or-later**. FxSound copyright and attribution are preserved.

## Current Linux architecture

```
Desktop apps
    |
    v
PipeWire sink: "FxSound"
    |
    v
upstream DfxDsp
  - Clarity / Fidelity
  - Ambience
  - Surround
  - Dynamic Boost
  - Bass Boost
  - Graphic EQ
  - upstream .fac presets
    |
    v
selected PipeWire hardware sink
```

The selected output device is saved in
`~/.config/fxsound-linux/output-node`.

## What works

- Native 48 kHz stereo PipeWire virtual sink named **FxSound**
- Real upstream DfxDsp processing
- Upstream factory presets
- 5 / 10 / 15 / 20 / 31-band EQ support
- Clarity, Ambience, Surround, Dynamic Boost, Bass Boost
- Master Gain, Volume Leveling, Balance, Filter Q
- Native PipeWire output-device discovery and live switching
- Output-device persistence across reboot
- Upstream FxSound JUCE theme and controls
- Spectrum visualizer
- User-level systemd service
- Desktop launcher
- Light/dark UI modes
- No JamesDSP dependency

The optional Linux peak guard is **disabled by default** so the processed
samples remain faithful to the upstream DfxDsp output. It can be enabled for
troubleshooting with `FXSOUND_PEAK_GUARD=1`.

## Build

Required core packages on Debian/Kali-like distributions:

```bash
sudo apt install build-essential cmake pkg-config \
  libpipewire-0.3-dev libspa-0.2-dev
```

The desktop UI currently expects JUCE 6.1.6:

```bash
cmake -S linux -B linux/build-ui \
  -DCMAKE_BUILD_TYPE=Release \
  -DFXSOUND_BUILD_PIPEWIRE=OFF \
  -DFXSOUND_JUCE_DIR=/path/to/JUCE-6.1.6

cmake --build linux/build-ui -j"$(nproc)"
```

Build the realtime daemon:

```bash
cmake -S linux -B linux/build-prod-core \
  -DCMAKE_BUILD_TYPE=Release \
  -DFXSOUND_BUILD_PIPEWIRE=ON \
  -DFXSOUND_JUCE_DIR=/nonexistent

cmake --build linux/build-prod-core -j"$(nproc)"
```

## Tests

Core DSP smoke test:

```bash
linux/build-prod-core/dsp_smoke
```

Decode/inspect an upstream factory preset:

```bash
linux/build-prod-core/preset_dump linux/factory-presets/1.fac
```

Realtime factory-preset test, with the daemon already running:

```bash
python3 linux/tests/live_factory_test.py
```

For memory-safety work, build the core with AddressSanitizer and run
`dsp_smoke`. The Linux port was developed with ASan specifically because
several legacy Windows compatibility assumptions (notably wide `swprintf`)
are unsafe if copied literally to glibc.

## Install for the current user

After building both the daemon and UI:

```bash
chmod +x linux/install-user.sh
./linux/install-user.sh
```

This installs:

- `~/.local/libexec/fxsound-linux/fxsoundd`
- `~/.local/libexec/fxsound-linux/fxsound-ui`
- `~/.local/bin/fxsound-linux`
- `~/.config/systemd/user/fxsound-linux.service`
- `~/.local/share/applications/fxsound-linux.desktop`
- factory presets under `~/.local/share/fxsound-linux/presets/`

The daemon starts at login and exposes a virtual **FxSound** output device.
The UI can be opened from the application launcher or by running:

```bash
fxsound-linux
```

## Linux control socket

The UI talks to the realtime daemon over:

```
$XDG_RUNTIME_DIR/fxsound-linux/control.sock
```

Current commands include:

- `STATUS`
- `PRESETS`
- `PRESET <name>`
- `POWER 0|1`
- `EFFECT <name> <value>`
- `EQ <band> <gain>`
- `EQFREQ <band> <frequency>`
- `BANDS <count>`
- `AUDIO <control> <value>`
- `METERS`
- `SPECTRUM`
- `OUTPUTS`
- `OUTPUT <pipewire-node-name>`

## Important implementation note

The legacy FxSound DSP was written around Windows/MSVC behavior. A Linux
compatibility layer lives in `linux/compat/`. In particular, Microsoft wide
`swprintf` uses `%s` for wide strings while glibc expects `%ls`.
The compatibility layer translates those format strings so preset/registry
paths remain correct without corrupting memory.

The goal is to keep Linux-specific changes at the platform boundary and leave
the actual audio algorithms as close to upstream as possible.
