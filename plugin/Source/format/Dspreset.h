// Reads Decent Sampler presets. inspect() is the cheap pass the library
// scanner uses (XML only); parse() also resolves and decodes every sample.
#pragma once

#include <juce_core/juce_core.h>
#include <memory>
#include "Library.h"

namespace trove {

class SampleLoader;

struct Inspection {
  juce::String background;
  int groups = 0, samples = 0;
  int lowNote = 0, highNote = 0;
  Env env;
  double volume = 1.0;
  juce::StringArray warnings;
};

class Dspreset {
public:
  // Just the <ui> panel, without touching any samples.
  static UiSpec readPanel(const juce::File& preset);
  static Inspection inspect(const juce::File& preset, juce::String& error);
  static std::shared_ptr<Program> parse(const juce::File& preset, SampleLoader& loader, juce::String& error);
};

} // namespace trove
