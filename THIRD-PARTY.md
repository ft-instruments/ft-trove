# Third-party software

TROVE is licensed under the [AGPL v3](LICENSE). What it includes or builds on:

| Component | Used for | License |
|---|---|---|
| [JUCE](https://juce.com) 8 | the plugin shell (VST3, AU, standalone, WebView), audio file reading and zip extraction, fetched by CMake at build time, not copied into this repo | AGPLv3 (or commercial; this project uses the AGPL option) |
| VST3 SDK (3.7.x) | bundled inside JUCE | Steinberg VST3 License or GPLv3 (this project uses the GPLv3 option, which is compatible with the AGPLv3) |

Build-time only (nothing of it ships): Vite, TypeScript and `@types/node`, plus
their dependencies, all MIT, ISC, BSD or Apache-2.0.

The UI uses the system's fonts; none are bundled. No sample libraries ship
with Trove: `tools/make-demo.mjs` synthesizes two small test libraries.
"Decent Sampler" is the name of a product by Decent Samples; Trove is not
affiliated with it and only reads its documented file format. "Steinberg" and
"VST" are trademarks of Steinberg Media Technologies GmbH; "Audio Units" is a
trademark of Apple Inc.
