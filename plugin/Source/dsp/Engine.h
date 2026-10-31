// The sampler: zones in, stereo audio out. Voices hold their own reference
// to the program, so a library swap never pulls the ground out from under a
// ringing note. Nothing allocates after the program is loaded.
#pragma once

#include <array>
#include <atomic>
#include <memory>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include "Effects.h"
#include "Params.h"
#include "format/Library.h"

namespace trove {

class Engine {
public:
  static constexpr int MAX_VOICES = 64;

  Engine();
  ~Engine();

  void prepare(double sampleRate);
  void setProgram(std::shared_ptr<const Program> program);
  std::shared_ptr<const Program> program() const;

  void noteOn(int note, float velocity);
  void noteOff(int note);
  void setSustain(bool down);
  void panic();
  // A menu in the library UI switches whole groups on and off.
  void setGroupEnabled(int group, bool on);
  // What the panel's controls do to the sound. Volumes and gains are linear.
  void setGroupVolume(int group, double linear);
  void setGroupPan(int group, double pan);                 // -1 .. 1
  void setTagVolume(const juce::String& tag, double linear);
  void setGroupFx(int group, int index, FxParam param, double value);
  void setFx(int index, FxParam param, double value);
  void loadImpulse(int index, const juce::File& file);
  void setGlobalTuning(double semitones);
  // Modulators: a filter envelope and a drift LFO, and what velocity does.
  void setModParam(int index, const juce::String& parameter, double value);
  void setVelocityEnabled(int index, bool on);
  void setGroupFxPosition(int group, int index, double position);   // 0..1 along the cutoff table
  void setParams(const std::array<double, NUM_PARAMS>& values);
  void process(juce::AudioBuffer<float>& buffer);

  int activeVoices() const { return activeCount.load(); }
  float peak() const { return peakOut.load(); }

private:
  struct Voice;

  void startVoice(const Zone& zone, int note, float velocity,
                  const std::shared_ptr<const Program>& program);

  std::array<std::unique_ptr<Voice>, MAX_VOICES> voices;
  mutable juce::SpinLock programLock;
  std::shared_ptr<const Program> prog;
  std::array<double, NUM_PARAMS> params {};
  static constexpr int MAX_TAGS = 64;
  static constexpr int GROUP_FX = 4;

  std::array<std::atomic<bool>, 256> groupOn;
  std::array<std::atomic<float>, 256> groupGain;       // 1 = as the preset has it
  std::array<std::atomic<float>, 256> groupPanOffset;
  std::array<std::atomic<float>, MAX_TAGS> tagGain;
  // frequency, resonance and level of each effect a group carries
  std::array<std::array<std::array<std::atomic<float>, 3>, GROUP_FX>, 256> groupFx;
  std::atomic<float> globalTuning { 0.0f };
  static constexpr int MAX_MODS = 2;
  struct ModRuntime { std::atomic<float> amount { 1.0f }, attack { 0.0f }, decay { 0.3f }, sustain { 1.0f }, release { 0.1f }; };
  std::array<ModRuntime, MAX_MODS> mods;
  std::array<std::atomic<bool>, 8> velocityOn;
  std::array<std::atomic<float>, 256 * GROUP_FX> groupFxPos;       // base position of a modulated cutoff
  std::array<float, MAX_MODS> modValue {};                          // this chunk's modulator outputs
  std::array<double, MAX_MODS> lfoPhase {};
  std::shared_ptr<Chain> chain;
  std::array<float, Chain::MAX_BLOCK> voiceL {}, voiceR {}, monoRight {};
  void resetRuntime(const Program& program);
  float renderChunk(float* left, float* right, int n,
                   const std::shared_ptr<const Program>& program, Chain* chain);
  std::array<juce::uint32, 65 * 128> rrCursor {};   // per sequence length and key
  std::array<int, 65 * 128> rrLast {};
  juce::Random rng { 0x7a11ce };
  double sr = 44100.0;
  juce::dsp::IIR::Filter<float> toneL, toneR;
  double lastTone = -1.0;
  juce::Reverb reverb;
  double lastReverb = -1.0;
  juce::uint64 order = 0;
  bool sustainDown = false;
  std::atomic<bool> panicRequest { false };
  std::atomic<int> activeCount { 0 };
  std::atomic<float> peakOut { 0.0f };

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Engine)
};

} // namespace trove
