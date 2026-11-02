# TROVE as a plugin: VST3 (macOS + Linux), AU (macOS) and a standalone app.
# The sampler and the preset parser are C++; the UI is the web UI (../dist)
# embedded into the binary and shown in a WebView.
#
#   ./build.sh            builds the web UI, then the plugins
#
# or by hand:
#   npm run build   (in the repo root; the UI must exist before configuring)
#   cmake -S plugin -B plugin/build -G Ninja -DCMAKE_BUILD_TYPE=Release
#   cmake --build plugin/build

cmake_minimum_required(VERSION 3.22)
project(Trove VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")

include(FetchContent)
FetchContent_Declare(JUCE
  GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
  GIT_TAG 8.0.15
  GIT_SHALLOW TRUE
  PATCH_COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_SOURCE_DIR}/patch-juce.cmake)
FetchContent_MakeAvailable(JUCE)

# Copy the built plugins into the user's plugin folders (off in CI).
option(TROVE_INSTALL "Install the plugins after building" ON)

# Everything the plugin and the offline renderer share.
add_library(trove_core STATIC
  Source/format/Dspreset.cpp
  Source/format/SampleLoader.cpp
  Source/library/Artwork.cpp
  Source/library/Scanner.cpp
  Source/dsp/Engine.cpp
  Source/dsp/Effects.cpp)
target_include_directories(trove_core PUBLIC Source)
set_target_properties(trove_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_link_libraries(trove_core
  PUBLIC
    juce::juce_audio_formats
    juce::juce_dsp
    juce::juce_graphics
  PRIVATE
    juce::juce_recommended_config_flags)

set(TROVE_FORMATS VST3 Standalone)
if(APPLE)
  list(APPEND TROVE_FORMATS AU)
endif()

juce_add_plugin(Trove
  COMPANY_NAME "FT"
  BUNDLE_ID "com.ft.trove"
  PLUGIN_MANUFACTURER_CODE Ftft
  PLUGIN_CODE Trve
  PRODUCT_NAME "Trove"
  DESCRIPTION "sample library player"
  FORMATS ${TROVE_FORMATS}
  IS_SYNTH TRUE
  NEEDS_MIDI_INPUT TRUE
  NEEDS_MIDI_OUTPUT FALSE
  IS_MIDI_EFFECT FALSE
  EDITOR_WANTS_KEYBOARD_FOCUS TRUE
  NEEDS_WEB_BROWSER TRUE
  VST3_CATEGORIES Instrument Synth
  AU_MAIN_TYPE kAudioUnitType_MusicDevice
  NEEDS_WEBVIEW2 TRUE
  COPY_PLUGIN_AFTER_BUILD ${TROVE_INSTALL})

target_sources(Trove PRIVATE
  Source/PluginProcessor.cpp
  Source/PluginEditor.cpp)

target_include_directories(Trove PRIVATE Source)

target_compile_definitions(Trove PUBLIC
  JUCE_WEB_BROWSER=1
  JUCE_USE_WIN_WEBVIEW2_WITH_STATIC_LINKING=1
  JUCE_USE_CURL=0
  JUCE_VST3_CAN_REPLACE_VST2=0)

# The built web UI, embedded. Rebuild it (npm run build) and re-run the build
# after changing anything under src/.
set(TROVE_UI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../dist")
if(NOT EXISTS "${TROVE_UI_DIR}/index.html")
  message(FATAL_ERROR "No web UI in ${TROVE_UI_DIR}: run `npm run build` in the repo root first (or use plugin/build.sh).")
endif()
file(GLOB_RECURSE TROVE_UI_FILES CONFIGURE_DEPENDS "${TROVE_UI_DIR}/*")
juce_add_binary_data(TroveUI SOURCES ${TROVE_UI_FILES})

target_link_libraries(Trove
  PRIVATE
    TroveUI
    trove_core
    juce::juce_audio_devices
    juce::juce_audio_utils
    juce::juce_gui_extra
  PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags)

# Offline check: parse a preset, play a chord, write a wav, print the peak.
juce_add_console_app(TroveRender PRODUCT_NAME "trove-render")
target_sources(TroveRender PRIVATE Tools/render.cpp)
target_link_libraries(TroveRender
  PRIVATE
    trove_core
    juce::juce_gui_basics
    juce::juce_recommended_config_flags)
