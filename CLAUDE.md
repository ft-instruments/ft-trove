# ft-trove

TROVE: a sampler for Decent Sampler libraries (VST3, AU, standalone) whose shelf of cover art is the instrument: it finds every library in your folders and loads on click.

- Type: FT-Instrument
- Kind: web
- Status: active
- Version: package.json + `project(... VERSION x.y.z)` in plugin/CMakeLists.txt
- Build: `plugin/build.sh` (UI: `npm run build`)   Test: `npm run typecheck`, `trove-render` smoke test (see README)   Run: the standalone in plugin/build/Trove_artefacts/Release/Standalone; `npm run dev` for the UI with a mock shelf
- Ships to: VST3/AU/standalone, installed for this user by `plugin/build.sh`; release zips for macOS, Windows and Linux from GitHub Actions (.github/workflows/build.yml)

## Notes
- No browser build: the audio, the format reading and the scanner are C++ (`plugin/Source/`); `src/` is only the UI. Unlike the other ft-* there is no TS DSP to keep in parity.
- The built UI must stay pure ASCII (vite.config.ts escapes it) and the editor serves it as UTF-8: the Linux WebView misreads the encoding otherwise.
- `npm run demo` writes two test libraries to `examples/` and `~/Documents/Samplers`; their samples are not committed.
