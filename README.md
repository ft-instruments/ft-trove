# ft-trove

**TROVE** — a sampler for Decent Sampler libraries. VST3 + standalone (AU on
macOS), no browser build. The shelf is the instrument: Trove finds every
library under your folders, shows the cover art, and loads on click instead
of making you hunt through a file dialog.

```
npm install
npm run demo         # builds two tiny test libraries into ~/Documents/Samplers
plugin/build.sh      # builds the UI, the VST3, AU (macOS) and the standalone
```

The standalone lands in `plugin/build/Trove_artefacts/Release/Standalone`;
the VST3 is copied to `~/.vst3` (Linux) or `~/Library/Audio/Plug-Ins/VST3`
(macOS) by the build.

## Download

Builds of the VST3, the AU (macOS only) and the standalone app for macOS,
Windows and Linux are attached to each
[release](https://github.com/ft-instruments/ft-trove/releases).

| | Standalone | VST3 | AU |
|---|---|---|---|
| macOS (Apple silicon + Intel) | yes | yes | yes |
| Windows | yes | yes | no |
| Linux | yes | yes | no |

Unzip and copy the plugin to your plugin folder (macOS
`~/Library/Audio/Plug-Ins/{VST3,Components}`, Windows
`C:\Program Files\Common Files\VST3`, Linux `~/.vst3`). The builds are not
signed or notarized: on macOS run `xattr -dr com.apple.quarantine` on the
files you copied (or right-click the app and choose Open the first time); on Windows
SmartScreen asks once. The Windows editor needs the WebView2 runtime
(included in Windows 11); on Linux it needs WebKitGTK.

## Privacy

TROVE makes no network connections: no telemetry, no update check, no
analytics, no accounts. The UI uses system fonts, the plugin serves it from
memory (`PluginEditor.cpp`, a JUCE resource provider), and JUCE is built
without its networking code (`JUCE_USE_CURL=0`). Libraries are read from your
own folders; the index and thumbnails are cached on your disk.

## Why it is not a licensing problem

Decent Sampler's *engine* is closed source, but the *file format* is
documented in public (`decentsampler-developers-guide.readthedocs.io`) and
has an official XML schema. `.dspreset` is plain XML; `.dslibrary` is a zip of
the preset and its samples. Reading a documented format for interoperability
is fine. What you cannot do is call the thing Decent Sampler, or ship their
engine — Trove has its own and a different name. Libraries bought from the
Decent Samples store may be watermarked/protected, so third-party playback of
those is the one case to check per library.

## How it is built

The UI is TypeScript in a JUCE WebView, the audio and
the file handling are C++. No DSP is duplicated in the browser.

```
src/                        UI (vite → dist/ → embedded in the plugin)
  main.ts                   shelf, search, folders, tray wiring
  bridge.ts                 JUCE event channel ("trove")
  knob.ts, keyboard.ts      controls
  mock.ts                   demo shelf for `npm run dev`
plugin/
  Source/format/            .dspreset parser, zip + audio loading
  Source/library/           background scanner, index cache, thumbnails
  Source/dsp/               sampler engine (voices, loops, ADSR, round robin)
  Source/PluginEditor.cpp   WebView, artwork resource provider
  Tools/render.cpp          offline smoke test: parse a preset, render a wav
```

### What the format reading covers

- `<groups>`, `<group>`, `<sample>` with inheritance, including the odd bits:
  `volume` multiplies at all three levels, `pan` inherits while
  `globalPan`/`groupPan` add, tuning and key tracking combine.
- Velocity layers, note ranges, round robin (`seqMode`/`seqPosition`/`seqLength`).
- Loops from attributes, and from WAV metadata when present.
- dB (`"-6dB"`) or linear volumes, `attack`/`ampEnvAttack` aliases.
- `.dslibrary` archives, extracted once into the app cache.
- Case-insensitive sample paths, because libraries are made on Windows.

Known gaps, on purpose for now: oscillators, slice stretch, buses/aux outputs,
release/key-switch triggers, modulators and MPE, `.dslibrary` copy protection.
Parsing never fails silently: unreadable libraries are listed with the reason.

## The shelf

- Scans `~/Documents/Samplers`, `<app data>/Trove/Libraries`, plus any folder
  you add, six levels deep. No file dialog after that.
- Caches the index and thumbnails, so reopening is instant; rescans only look
  at files whose mtime changed.
- Artwork comes from the preset's `backgroundImage` or from `cover`/`folder`/
  `front`/`artwork` images in the library; otherwise the card gets a tinted
  specimen placeholder.
- Search filters names and paths, chips filter folders vs archives.

## Controls

The seven host parameters are `Level`, `Tone`, `Space`, and the amp envelope
`Attack`/`Decay`/`Sustain`/`Release`. Loading a library sets the envelope
knobs to that library's `<groups>` values; moving them offsets the values
per zone, so layered instruments keep their differences.

Play with MIDI, the on-screen keys (press low on a key for more velocity), or
`A`–`K`; `Z`/`X` shift octave, `Space` is the sustain pedal.

## Tests

```
node tools/make-demo.mjs
plugin/build/TroveRender_artefacts/Release/TroveRender \
  "examples/Glass Keys/Glass Keys.dspreset" /tmp/trove.wav
plugin/build/TroveRender_artefacts/Release/TroveRender \
  "examples/Brass Cabinet.dslibrary" /tmp/brass.wav
plugin/build/TroveRender_artefacts/Release/TroveRender --scan
```

The first two parse a folder library and a zip archive, play a chord, and
print zone/group counts, the resolved envelope, missing-sample warnings and
the rendered peak; they exit non-zero on silence. `--scan` runs the real
scanner over your folders, makes the thumbnails and reports anything it
could not read.

## License

Copyright (C) 2026 Filipe F. Thomaz.

TROVE is free software under the [GNU AGPL v3](LICENSE). The plugin is built
on [JUCE](https://juce.com), which is itself AGPLv3 (or commercial), so the
whole thing is distributed under the AGPL (the bundled fonts keep their own
[SIL OFL](src/fonts/); see
[THIRD-PARTY.md](THIRD-PARTY.md)): you can use it for anything, including
music you sell, and change it and share it, as long as a modified version you
distribute comes with its source under the same license.
