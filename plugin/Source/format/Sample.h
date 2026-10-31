// One decoded audio file, shared by every zone that points at it.
#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

namespace trove {

struct Sample {
  juce::AudioBuffer<float> audio;
  double sourceRate = 44100.0;
  juce::int64 frames = 0;

  bool hasLoop = false;
  juce::int64 loopStart = 0, loopEnd = 0;

  const float* left() const { return audio.getReadPointer(0); }
  const float* right() const {
    return audio.getNumChannels() > 1 ? audio.getReadPointer(1) : audio.getReadPointer(0);
  }
};

} // namespace trove
