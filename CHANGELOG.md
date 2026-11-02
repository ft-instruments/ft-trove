# Changelog

All notable changes to ft-trove. Format: [Keep a Changelog](https://keepachangelog.com/en/1.1.0/); versions follow [SemVer](https://semver.org/).

## [0.1.0] - 2026-11-02

First tracked release. The version file already said 0.1.0, so this tags the project as it stands today.

### Added
- A sampler for Decent Sampler libraries (`.dspreset` folders and `.dslibrary` archives) as a VST3, AU (macOS) and standalone app, with its own C++ engine and a web UI in a JUCE WebView.
- The shelf: scans your library folders, caches the index and cover art, loads a library on click; search and folder/archive filters.
- Libraries' own panels (knobs and controls from the preset's `<ui>`), an overall Output volume, and host parameters for level, tone, space and the amp envelope.
- `trove-render`: an offline smoke test that parses a preset, plays a chord and reports the result.
- AGPL-3.0 license, a Download section in the README and a GitHub Actions workflow that builds the plugin for macOS, Windows and Linux.

### Changed
- The plugin serves its pages as UTF-8 explicitly and the built UI is pure ASCII, so the Linux WebView cannot misread the text encoding.
- `plugin/CMakeLists.txt`: `TROVE_INSTALL` option (off in CI) and `NEEDS_WEBVIEW2` with static WebView2 linking for Windows.
