#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "format/Dspreset.h"
#include <cmath>

namespace trove {

struct TroveProcessor::Loader final : private juce::Thread {
  explicit Loader(TroveProcessor& o) : juce::Thread("trove loader"), owner(o) {}
  ~Loader() override {
    signalThreadShouldExit();
    wake.signal();
    stopThread(5000);
  }

  void request(const juce::String& id, const juce::File& preset, const juce::String& name) {
    {
      const juce::ScopedLock sl(lock);
      pendingId = id;
      pendingName = name;
      pendingFile = preset;
      hasPending = true;
    }
    if (!isThreadRunning()) startThread(juce::Thread::Priority::normal);
    wake.signal();
  }

  void run() override {
    while (!threadShouldExit()) {
      wake.wait(500);
      if (threadShouldExit()) break;

      juce::String id, name;
      juce::File file;
      bool any = false;
      {
        const juce::ScopedLock sl(lock);
        any = hasPending;
        hasPending = false;
        id = pendingId;
        name = pendingName;
        file = pendingFile;
      }
      if (!any) continue;

      juce::String error;
      auto program = Dspreset::parse(file, owner.samples, error);
      owner.install(program, id, name, error);
    }
  }

  TroveProcessor& owner;
  juce::CriticalSection lock;
  juce::String pendingId, pendingName;
  juce::File pendingFile;
  bool hasPending = false;
  juce::WaitableEvent wake;
};

TroveProcessor::TroveProcessor()
  : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)) {
  for (int i = 0; i < NUM_PARAMS; ++i) {
    const auto& def = PARAMS[i];
    auto* p = new juce::AudioParameterFloat(
      juce::ParameterID { def.id, 1 }, def.name,
      juce::NormalisableRange<float>(def.min, def.max, 0.0f, def.skew), def.def,
      juce::AudioParameterFloatAttributes().withLabel(def.unit));
    addParameter(p);
    raw[(size_t)i] = p;
  }
  loader = std::make_unique<Loader>(*this);
  scan.startScan();
}

TroveProcessor::~TroveProcessor() {
  *alive = 1;
  loader.reset();
}

void TroveProcessor::prepareToPlay(double sampleRate, int samplesPerBlock) {
  engine.prepare(sampleRate);
  collector.reset(sampleRate);
}

bool TroveProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
  const auto out = layouts.getMainOutputChannelSet();
  return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

void TroveProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) {
  const juce::ScopedNoDenormals noDenormals;
  const int n = buffer.getNumSamples();

  for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
    buffer.clear(ch, 0, n);

  for (const auto meta : midiMessages)
    collector.addMessageToQueue(meta.getMessage());

  juce::MidiBuffer midi;
  collector.removeNextBlockOfMessages(midi, n);

  for (const auto meta : midi) {
    const auto m = meta.getMessage();
    if (m.isNoteOn()) {
      engine.noteOn(m.getNoteNumber(), m.getFloatVelocity() * 127.0f);
      heldNotes[(size_t)(m.getNoteNumber() >> 5)] |= 1u << (m.getNoteNumber() & 31);
    } else if (m.isNoteOff()) {
      engine.noteOff(m.getNoteNumber());
      heldNotes[(size_t)(m.getNoteNumber() >> 5)] &= ~(1u << (m.getNoteNumber() & 31));
    }
    else if (m.isController() && m.getControllerNumber() == 64)
      engine.setSustain(m.getControllerValue() >= 64);
    else if (m.isAllNotesOff() || m.isAllSoundOff()) {
      engine.panic();
      for (auto& word : heldNotes) word = 0;
    }
  }

  std::array<double, NUM_PARAMS> values {};
  for (int i = 0; i < NUM_PARAMS; ++i) values[(size_t)i] = paramValue(i);
  engine.setParams(values);

  engine.process(buffer);
}

juce::AudioProcessorEditor* TroveProcessor::createEditor() {
  return new TroveEditor(*this);
}

float TroveProcessor::paramValue(int i) const {
  return raw[(size_t)juce::jlimit(0, NUM_PARAMS - 1, i)]->get();
}

void TroveProcessor::setParamRaw(int i, double value) {
  if (i < 0 || i >= NUM_PARAMS) return;
  auto* p = raw[(size_t)i];
  p->setValueNotifyingHost(p->convertTo0to1((float)value));
}

void TroveProcessor::setParamFromUi(int i, double value) {
  setParamRaw(i, value);
}

void TroveProcessor::uiNoteOn(int note, float velocity) {
  collector.addMessageToQueue(
    juce::MidiMessage::noteOn(1, juce::jlimit(0, 127, note), juce::jlimit(0.0f, 1.0f, velocity)));
}

void TroveProcessor::uiNoteOff(int note) {
  collector.addMessageToQueue(juce::MidiMessage::noteOff(1, juce::jlimit(0, 127, note)));
}

void TroveProcessor::uiSustain(bool down) {
  collector.addMessageToQueue(
    juce::MidiMessage::controllerEvent(1, 64, down ? 127 : 0));
}

void TroveProcessor::uiGroup(int group, bool on) { engine.setGroupEnabled(group, on); }

void TroveProcessor::uiPanic() {
  engine.panic();
  collector.addMessageToQueue(juce::MidiMessage::allNotesOff(1));
}

void TroveProcessor::requestLibrary(const juce::String& id, const juce::File& preset,
                                    const juce::String& name) {
  loader->request(id, preset, name);
}

bool TroveProcessor::takeLoaded(LoadedInfo& out) {
  const juce::ScopedLock sl(infoLock);
  if (infoVersion.load() == seenInfo) return false;
  seenInfo = infoVersion.load();
  out = info;
  return true;
}

void TroveProcessor::clearLibrary() {
  engine.panic();
  const juce::ScopedLock sl(infoLock);
  info = {};
  infoVersion++;
}

void TroveProcessor::install(std::shared_ptr<const Program> program, const juce::String& id,
                             const juce::String& name, const juce::String& error) {
  LoadedInfo fresh;
  fresh.id = id;
  fresh.name = name;
  fresh.error = error;
  fresh.program = program;

  if (program != nullptr) {
    fresh.preset = program->presetPath;
    fresh.zones = (int)program->zones.size();
    fresh.groups = program->groups;
    fresh.ownEffects = !program->effects.empty();
    fresh.ui = program->ui;
    fresh.attack = program->env.attack;
    fresh.decay = program->env.decay;
    fresh.sustain = program->env.sustain;
    fresh.release = program->env.release;
    fresh.gainDb = juce::jlimit((double)PARAMS[pGain].min, (double)PARAMS[pGain].max,
                               juce::Decibels::gainToDecibels(program->volume));
    if (fresh.name.isEmpty()) fresh.name = program->name;
    engine.setProgram(program);
  }

  {
    const juce::ScopedLock sl(infoLock);
    info = fresh;
    infoVersion++;
  }

  auto token = alive;
  juce::MessageManager::callAsync([this, token, fresh]() {
    if (*token != 0) return;
    setParamRaw(pAttack, fresh.attack);
    setParamRaw(pDecay, fresh.decay);
    setParamRaw(pSustain, fresh.sustain);
    setParamRaw(pRelease, fresh.release);
    setParamRaw(pGain, fresh.gainDb);
    // A preset with its own effects is the whole sound: Trove's own tone and
    // space start out of the way, as Decent Sampler would have it.
    if (fresh.ownEffects) {
      setParamRaw(pTone, PARAMS[pTone].max);
      setParamRaw(pReverb, 0.0);
    }
    stateVersion++;
  });
}

void TroveProcessor::getStateInformation(juce::MemoryBlock& destData) {
  juce::XmlElement xml("TROVE");
  {
    const juce::ScopedLock sl(infoLock);
    xml.setAttribute("preset", info.preset);
    xml.setAttribute("name", info.name);
  }
  for (int i = 0; i < NUM_PARAMS; ++i)
    xml.setAttribute(PARAMS[i].id, paramValue(i));
  copyXmlToBinary(xml, destData);
}

void TroveProcessor::setStateInformation(const void* data, int sizeInBytes) {
  auto xml = getXmlFromBinary(data, sizeInBytes);
  if (xml == nullptr || !xml->hasTagName("TROVE")) return;

  for (int i = 0; i < NUM_PARAMS; ++i)
    if (xml->hasAttribute(PARAMS[i].id))
      setParamRaw(i, xml->getDoubleAttribute(PARAMS[i].id, PARAMS[i].def));

  const juce::File preset(xml->getStringAttribute("preset"));
  if (preset.existsAsFile()) {
    auto name = xml->getStringAttribute("name");
    if (name.isEmpty()) name = preset.getFileNameWithoutExtension();
    requestLibrary("restored", preset, name);
  }
  stateVersion++;
}

} // namespace trove

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
  return new trove::TroveProcessor();
}
