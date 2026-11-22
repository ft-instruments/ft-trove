# Changelog

All notable changes to ft-trove. Format: [Keep a Changelog](https://keepachangelog.com/en/1.1.0/); versions follow [SemVer](https://semver.org/).

## Unreleased

## [0.1.0] - 2026-11-22

First public release.

### Added
- A sampler for Decent Sampler libraries (`.dspreset` folders and `.dslibrary` archives) as a VST3, AU (macOS) and standalone app, with its own C++ engine and a web UI in a JUCE WebView.
- The shelf: scans your library folders, caches the index and cover art, loads a library on click; search and folder/archive filters.
- Libraries' own panels (knobs and controls from the preset's `<ui>`), an overall Output volume, and host parameters for level, tone, space and the amp envelope.
- `trove-render`: an offline smoke test that parses a preset, plays a chord and reports the result.
- The family look in small touches: a Tektur wordmark, registration brackets on the header, Geist Mono labels (bundled, so the UI never fetches fonts).
- Builds for macOS (Apple silicon + Intel), Windows and Linux, with no network connections of any kind.

[Unreleased]: https://github.com/ft-instruments/ft-trove/compare/v0.1.0...HEAD
[0.1.0]: https://github.com/ft-instruments/ft-trove/releases/tag/v0.1.0
