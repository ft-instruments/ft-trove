// The effects a preset declares in <effects>: filters, reverb, chorus, phaser,
// delay, convolution and gain. Each effect keeps its settings in atomics, so
// the panel can turn a knob from the message thread while the audio thread
// reads it a block at a time.
#pragma once

#include <array>
#include <atomic>
#include <memory>
#include <vector>
#include <juce_dsp/juce_dsp.h>
#include "format/Library.h"

namespace trove {

enum FxParam {
  fxFreq, fxRes, fxWet, fxRoom, fxDamp, fxMix, fxTime, fxFeedback,
  fxLevel, fxDepth, fxRate, fxCenter, fxOffset, FX_PARAM_COUNT
};

// "FX_FILTER_FREQUENCY" and friends, as a binding names them.
bool fxParamFromName(const juce::String& dsName, FxParam& out);

// A resonant filter section. Used by the instrument chain and, one per voice,
// for the filters a group carries.
struct FilterSection {
  enum Kind { lowpass, highpass, bandpass };

  Kind kind = lowpass;
  int poles = 2;                 // 1, 2 or 4
  double sr = 44100.0;
  double lastFreq = -1.0, lastRes = -1.0;

  struct Stage { double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1[2] = {}, z2[2] = {}; };
  std::array<Stage, 2> stage;

  static bool parse(const juce::String& type, Kind& kind, int& poles);
  void setup(double sampleRate, Kind k, int p) { sr = sampleRate; kind = k; poles = p; lastFreq = lastRes = -1.0; }
  void update(double freq, double res);
  void reset();
  float process(int ch, float x);
};

class Fx {
public:
  virtual ~Fx() = default;
  virtual void prepare(double sampleRate, int maxBlock) = 0;
  // Stereo in place. `right` is never null here; the engine hands in a scratch
  // channel for mono hosts.
  virtual void process(float* left, float* right, int n) = 0;
  virtual void loadImpulse(const juce::File&) {}

  juce::String type;
  std::array<std::atomic<float>, FX_PARAM_COUNT> p;
};

class Chain {
public:
  static constexpr int MAX_BLOCK = 1024;

  Chain(const std::vector<Effect>& effects, const juce::File& root, double sampleRate);
  void process(float* left, float* right, int n);
  void set(int index, FxParam param, double value);
  void loadImpulse(int index, const juce::File& file);
  int size() const { return (int)fx.size(); }

private:
  std::vector<std::unique_ptr<Fx>> fx;
};

} // namespace trove
