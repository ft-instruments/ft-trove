// Decodes audio files and keeps one copy of each. Also unpacks .dslibrary
// containers (plain zip archives of a preset plus its samples).
#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <map>
#include <memory>
#include "Sample.h"

namespace trove {

class SampleLoader {
public:
  SampleLoader();

  /** Cached by absolute path and modification time. Null when it will not decode. */
  std::shared_ptr<const Sample> load(const juce::File& file);

  /** Extracts a zip once; the same directory is reused on later scans. */
  static juce::File unpackZip(const juce::File& zip, const juce::File& targetDir, juce::String& error);

  /** Resolves a path inside a library, matching case the way Windows-made libraries expect. */
  static juce::File resolve(const juce::File& base, const juce::String& relative);

private:
  juce::AudioFormatManager formats;
  juce::CriticalSection lock;
  std::map<juce::String, std::shared_ptr<const Sample>> cache;
};

} // namespace trove
