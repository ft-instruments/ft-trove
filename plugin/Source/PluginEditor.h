// The editor is the web UI (../dist, embedded) inside a WebView. The shelf,
// the knobs and the keyboard talk to the processor over one event channel,
// "trove", in both directions; src/bridge.ts is the other end.
#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include <optional>
#include <unordered_map>
#include "PluginProcessor.h"

class TroveEditor final : public juce::AudioProcessorEditor, private juce::Timer {
public:
  explicit TroveEditor(trove::TroveProcessor&);
  ~TroveEditor() override = default;

  void paint(juce::Graphics&) override;
  void resized() override;

private:
  void timerCallback() override;
  void onMessage(const juce::var& msg);
  void send(juce::DynamicObject::Ptr obj);
  void sendHello();
  void sendIndex();
  void sendLoaded(const trove::LoadedInfo& info);
  void sendParams();
  std::optional<juce::WebBrowserComponent::Resource> resource(const juce::String& url);
  void addRootChooser();

  trove::TroveProcessor& proc;
  juce::WebBrowserComponent web;
  std::unique_ptr<juce::FileChooser> chooser;
  juce::File loadedDir;

  std::unordered_map<std::string, int> ids;
  std::array<float, trove::NUM_PARAMS> shown {};
  int seenIndex = -1;
  bool seenBusy = false;
  void applyBinding(const juce::var& msg);
  int seenState = -1;
  std::array<juce::uint32, 4> seenHeld {};
  int tick = 0;
  bool ready = false;
  bool sized = false;

  static constexpr int UI_W = 1440, UI_H = 960;
  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TroveEditor)
};
