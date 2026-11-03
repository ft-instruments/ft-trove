#!/usr/bin/env bash
# Build TROVE as a plugin and install it for this user:
#   macOS: VST3 -> ~/Library/Audio/Plug-Ins/VST3, AU -> ~/Library/Audio/Plug-Ins/Components
#   Linux: VST3 -> ~/.vst3
# plus a standalone app in plugin/build/Trove_artefacts/Release/Standalone.
#
#   plugin/build.sh           release build
#   plugin/build.sh Debug     debug build
set -euo pipefail
cd "$(dirname "$0")/.."

config="${1:-Release}"

npm install --no-audit --no-fund
npm run build

generator=()
if command -v ninja >/dev/null; then generator=(-G Ninja); fi
# One JUCE checkout shared by every ft-* plugin, at the version CMakeLists.txt
# pins (~/SDKs/JUCE-<tag>, or FT_JUCE_DIR). Without it CMake downloads its own
# into plugin/build, as in CI. Passed empty so a stale path never sticks.
juce_tag=$(sed -n "s/^ *GIT_TAG //p" plugin/CMakeLists.txt)
juce_dir="${FT_JUCE_DIR:-$HOME/SDKs/JUCE-$juce_tag}"
[[ -d "$juce_dir" ]] || juce_dir=""
cmake -S plugin -B plugin/build "${generator[@]}" -DCMAKE_BUILD_TYPE="$config" \
  -DFETCHCONTENT_SOURCE_DIR_JUCE="$juce_dir"
cmake --build plugin/build --config "$config" --parallel

if [[ "$(uname)" == "Darwin" ]]; then
  killall -9 AudioComponentRegistrar 2>/dev/null || true
  echo
  echo "Installed. Check the AU with:  auval -v aumu Trve Ftft"
fi
