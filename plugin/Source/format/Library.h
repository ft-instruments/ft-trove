// The parsed shape of a .dspreset: a program of zones, each zone already
// resolved against its group's inherited settings and pointing at a decoded
// sample. Nothing here knows about audio rendering.
#pragma once

#include <juce_core/juce_core.h>
#include <array>
#include <memory>
#include <vector>
#include "Sample.h"

namespace trove {

struct Env {
  double attack = 0.0, hold = 0.0, decay = 0.3, sustain = 1.0, release = 0.4;
};

enum SeqMode { seqAlways = 0, seqRoundRobin = 1, seqRandom = 2, seqTrueRandom = 3 };

struct Zone {
  std::shared_ptr<const Sample> sample;
  juce::String path;

  int loNote = 0, hiNote = 127, rootNote = 60;
  int loVel = 0, hiVel = 127;

  double startFrame = 0.0, endFrame = -1.0;
  double loopStart = 0.0, loopEnd = -1.0;
  bool loopEnabled = false;
  int loopCrossfade = 0;
  bool loopDuringRelease = true;

  double tuning = 0.0;             // semitones, global + group + sample
  bool tuningIgnoresKeyTrack = false;
  double pitchKeyTrack = 1.0;
  double gain = 1.0;               // product of the three volumes
  double pan = 0.0;                // -1 .. 1, inherited pan plus offsets
  double ampVelTrack = 1.0;
  double glideTime = 0.0;
  double delaySeconds = 0.0;

  bool ampEnvEnabled = true;
  Env env;

  int group = 0;
  std::vector<int> tags;           // indices into Program::tagNames
  juce::String trigger = "attack";
  int seqMode = seqAlways;
  int seqPosition = 1;
  int seqLength = 0;

  bool matches(int note, int velocity) const {
    return note >= loNote && note <= hiNote && velocity >= loVel && velocity <= hiVel;
  }
};

// One <effect> from <effects>, either on the instrument or inside a group.
// Attributes stay raw; the engine reads the ones its effect type understands.
struct Effect {
  juce::String type;
  std::vector<std::pair<juce::String, juce::String>> attrs;

  juce::String text(const char* name) const {
    for (auto& a : attrs) if (a.first == name) return a.second;
    return {};
  }
  bool has(const char* name) const {
    for (auto& a : attrs) if (a.first == name) return true;
    return false;
  }
  double number(const char* name, double fallback) const {
    for (auto& a : attrs) if (a.first == name) return a.second.getDoubleValue();
    return fallback;
  }
};

// What a modulator or a velocity binding drives: a group's filter cutoff, or a
// group's tuning. The table maps the (0..1) modulated position onto Hz.
struct ModTarget {
  int group = -1, effect = -1;
  juce::String parameter;                          // FX_FILTER_FREQUENCY, GROUP_TUNING, ...
  std::vector<std::pair<float, float>> table;      // x (0..1) -> value
  double outMin = 0.0, outMax = 1.0;
  bool hasRange = false;
};

struct Modulator {
  bool envelope = true;                            // otherwise an LFO
  double amount = 1.0;
  Env env;
  double lfoHz = 1.0;
  std::vector<ModTarget> targets;
};

struct VelocityBinding {
  ModTarget target;
  double amount = 1.0;
  bool enabled = true;
};

// The preset's own panel, straight from <ui>: Decent Sampler draws these
// controls itself, so they are plain data rather than an image.
struct UiBinding {
  juce::String type;        // amp | effect | general | control | ...
  juce::String parameter;   // ENV_ATTACK, FX_REVERB_WET_LEVEL, ...
  juce::String level;       // instrument | group | tag | ui | midi
  int position = 0;
  juce::String translation;      // linear | table | fixed_value | ""
  bool hasOutputRange = false;
  double outMin = 0.0, outMax = 1.0;
  juce::String table;            // "x,y;x,y" when translation == table
  juce::String fixedValue;       // translationValue, for fixed_value
  juce::String identifier;       // tag name, for level="tag"
  juce::String tags;             // UI tags this targets, for level="ui"
  int groupIndex = -1, effectIndex = -1;
  int modulatorIndex = -1, bindingIndex = -1;
  double factor = 1.0;           // some presets scale with factor="0.01"
  bool hasFactor = false;
  bool triggerOnLoad = true;
};

struct UiState {                 // one face of an image button
  juce::String name, main, click, hover;
  std::vector<UiBinding> bindings; // what this state does when it becomes current
};

// type: knob | bar | skin | button | menu | label | image | strip
struct UiControl {
  juce::String type;
  juce::String tab;
  bool labeled = false;                    // a <labeled-knob>: Decent Sampler insets its skin
  juce::String tags;                       // what UI-level bindings use to find it
  int uiIndex = 0;                         // position in the panel, same purpose
  int x = 0, y = 0, w = 60, h = 60;
  juce::String text, style, source, valueType, orientation, align;
  juce::String fg, bg, textColor;          // CSS colours, alpha included
  double minValue = 0.0, maxValue = 1.0, value = 0.0, defaultValue = 0.0;
  bool hasDefault = false;
  int textSize = 14;
  bool showLabel = true, visible = true, vertical = false, dragVertical = true, stretch = true;
  int frames = 0;                          // skins and strips
  double dragPixels = 0.0, frameRate = 0.0, opacity = 1.0;
  juce::String placeholder;
  juce::StringArray options;               // menu entries
  std::vector<std::vector<UiBinding>> optionBindings;  // what each entry switches
  std::vector<UiState> states;             // image buttons
  std::vector<UiBinding> bindings;         // a control may drive several parameters
};

struct UiSpec {
  bool valid = false;
  int width = 0, height = 0;
  juce::String bgColor, bgImage;
  bool bgTopLeft = false;
  juce::StringArray tabs;
  std::vector<UiControl> controls;
  bool hasKeyboard = false;
  // <keyboard><color loNote hiNote color>: spans DS paints on its full keyboard.
  struct KeyRange { int lo = 0, hi = 127; juce::String colour; };
  std::vector<KeyRange> keyRanges;
};

struct Program {
  juce::String name, presetPath, rootDir;
  std::vector<Zone> zones;
  std::vector<std::shared_ptr<const Sample>> samples;
  std::vector<int> seqLen;          // per group, computed
  Env env;                          // the <groups> envelope, what the knobs start at
  double volume = 1.0;
  int groups = 0;
  std::vector<char> groupOn;        // enabled="false" groups start switched off
  std::vector<double> groupVolume;  // each group's own volume, baked into zone gain; knobs scale it
  juce::StringArray tagNames;
  std::vector<Effect> effects;      // the instrument chain, in file order
  std::vector<std::vector<Effect>> groupEffects;
  std::vector<Modulator> modulators;
  std::vector<VelocityBinding> velocity;
  // For each group effect, the table that maps a modulated 0..1 position onto
  // a cutoff in Hz, when something modulates it.
  std::vector<std::array<std::vector<std::pair<float, float>>, 4>> slotTable;
  juce::StringArray warnings;
  UiSpec ui;

  int zoneCount() const { return (int)zones.size(); }
};

} // namespace trove
