// The plugin around the sampler: host parameters, MIDI, the library loader
// and the bridge to the WebView editor. Loading happens on its own thread;
// the audio thread only ever swaps a shared_ptr.
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <atomic>
#include <memory>
#include "Params.h"
#include "dsp/Engine.h"
#include "format/SampleLoader.h"
#include "library/Scanner.h"

namespace trove {

struct LoadedInfo {
  juce::String id, name, preset, error;
  int zones = 0, groups = 0;
  bool ownEffects = false;   // the preset brings its own <effects>
  double attack = 0.0, decay = 0.3, sustain = 1.0, release = 0.4, gainDb = 0.0;
  std::shared_ptr<const Program> program;
  UiSpec ui;
};

class TroveProcessor final : public juce::AudioProcessor {
public:
  TroveProcessor();
  ~TroveProcessor() override;

  void prepareToPlay(double sampleRate, int samplesPerBlock) override;
  void releaseResources() override {}
  bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
  void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
  using AudioProcessor::processBlock;

  juce::AudioProcessorEditor* createEditor() override;
  bool hasEditor() const override { return true; }

  const juce::String getName() const override { return JucePlugin_Name; }
  bool acceptsMidi() const override { return true; }
  bool producesMidi() const override { return false; }
  bool isMidiEffect() const override { return false; }
  double getTailLengthSeconds() const override { return 20.0; }

  int getNumPrograms() override { return 1; }
  int getCurrentProgram() override { return 0; }
  void setCurrentProgram(int) override {}
  const juce::String getProgramName(int) override { return {}; }
  void changeProgramName(int, const juce::String&) override {}

  void getStateInformation(juce::MemoryBlock& destData) override;
  void setStateInformation(const void* data, int sizeInBytes) override;

  Scanner& scanner() { return scan; }

  void requestLibrary(const juce::String& id, const juce::File& preset, const juce::String& name);
  bool takeLoaded(LoadedInfo& out);
  void clearLibrary();

  float paramValue(int i) const;
  void setParamFromUi(int i, double value);
  void setParamRaw(int i, double value);
  std::atomic<int> stateVersion { 0 };
  // Notes currently held, from MIDI and the page alike: 128 bits for the keyboard.
  std::array<std::atomic<juce::uint32>, 4> heldNotes {};

  void uiNoteOn(int note, float velocity);
  void uiNoteOff(int note);
  void uiSustain(bool down);
  void uiPanic();
  void uiGroup(int group, bool on);

  Engine& engineRef() { return engine; }
  float peakValue() const { return engine.peak(); }

private:
  struct Loader;
  friend struct Loader;

  void install(std::shared_ptr<const Program> program, const juce::String& id,
               const juce::String& name, const juce::String& error);

  Scanner scan;
  SampleLoader samples;
  Engine engine;
  juce::MidiMessageCollector collector;
  std::array<juce::AudioParameterFloat*, NUM_PARAMS> raw {};
  std::unique_ptr<Loader> loader;

  juce::CriticalSection infoLock;
  LoadedInfo info;
  int seenInfo = 0;
  std::atomic<int> infoVersion { 0 };

  std::shared_ptr<int> alive = std::make_shared<int>(0);

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TroveProcessor)
};

} // namespace trove
