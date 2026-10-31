#include "Effects.h"
#include <cmath>
#include "format/SampleLoader.h"

namespace trove {
namespace {

constexpr double kPi = 3.14159265358979323846;

struct NameParam { const char* name; FxParam param; };

constexpr NameParam kNames[] = {
  { "FX_FILTER_FREQUENCY", fxFreq },   { "FX_FILTER_RESONANCE", fxRes },
  { "FX_REVERB_WET_LEVEL", fxWet },    { "FX_WET_LEVEL", fxWet },
  { "FX_REVERB_ROOM_SIZE", fxRoom },   { "FX_REVERB_DAMPING", fxDamp },
  { "FX_MIX", fxMix },                 { "FX_DELAY_TIME", fxTime },
  { "FX_FEEDBACK", fxFeedback },       { "FX_DELAY_FEEDBACK", fxFeedback },
  { "LEVEL", fxLevel },                { "FX_MOD_DEPTH", fxDepth },
  { "FX_MOD_RATE", fxRate },           { "FX_CENTER_FREQUENCY", fxCenter },
  { "FX_STEREO_OFFSET", fxOffset },
};

void designStage(FilterSection::Stage& s, FilterSection::Kind kind, double sr, double freq, double q) {
  const double w = 2.0 * kPi * freq / sr;
  const double cs = std::cos(w);
  const double alpha = std::sin(w) / (2.0 * q);
  double b0, b1, b2;
  switch (kind) {
    case FilterSection::highpass: b0 = (1 + cs) / 2; b1 = -(1 + cs); b2 = (1 + cs) / 2; break;
    case FilterSection::bandpass: b0 = alpha;        b1 = 0;         b2 = -alpha;       break;
    default:                      b0 = (1 - cs) / 2; b1 = 1 - cs;    b2 = (1 - cs) / 2; break;
  }
  const double a0 = 1 + alpha;
  s.b0 = b0 / a0; s.b1 = b1 / a0; s.b2 = b2 / a0;
  s.a1 = -2 * cs / a0; s.a2 = (1 - alpha) / a0;
}

float run(FilterSection::Stage& s, int ch, float x) {
  const double y = s.b0 * x + s.z1[ch];
  s.z1[ch] = s.b1 * x - s.a1 * y + s.z2[ch];
  s.z2[ch] = s.b2 * x - s.a2 * y;
  return (float)y;
}

// ------------------------------------------------------------------ filter

class FilterFx : public Fx {
public:
  FilterFx(const Effect& e, FilterSection::Kind kind, int poles)
    : kind(kind), poles(poles) {
    p[fxFreq] = (float)e.number("frequency", 22000.0);
    p[fxRes] = (float)e.number("resonance", 0.7);
  }
  void prepare(double sr, int) override { section.setup(sr, kind, poles); section.reset(); }
  void process(float* l, float* r, int n) override {
    section.update(p[fxFreq], p[fxRes]);
    for (int i = 0; i < n; ++i) { l[i] = section.process(0, l[i]); r[i] = section.process(1, r[i]); }
  }
private:
  FilterSection::Kind kind;
  int poles;
  FilterSection section;
};

// ------------------------------------------------------------------ reverb

class ReverbFx : public Fx {
public:
  explicit ReverbFx(const Effect& e) {
    p[fxWet] = (float)e.number("wetLevel", 0.5);
    p[fxRoom] = (float)e.number("roomSize", 0.7);
    p[fxDamp] = (float)e.number("damping", 0.3);
  }
  void prepare(double sr, int) override { reverb.setSampleRate(sr); reverb.reset(); last = -1; }
  void process(float* l, float* r, int n) override {
    const float wet = juce::jlimit(0.0f, 1.0f, p[fxWet].load());
    const float room = juce::jlimit(0.0f, 1.0f, p[fxRoom].load());
    const float damp = juce::jlimit(0.0f, 1.0f, p[fxDamp].load());
    if (wet != last || room != lastRoom || damp != lastDamp) {
      last = wet; lastRoom = room; lastDamp = damp;
      juce::Reverb::Parameters rp;
      rp.roomSize = room;
      rp.damping = damp;
      rp.wetLevel = wet * 0.5f;
      rp.dryLevel = 1.0f - 0.3f * wet;
      rp.width = 1.0f;
      reverb.setParameters(rp);
    }
    if (wet < 0.001f) return;
    reverb.processStereo(l, r, n);
  }
private:
  juce::Reverb reverb;
  float last = -1, lastRoom = -1, lastDamp = -1;
};

// -------------------------------------------------- chorus / phaser (JUCE)

class ChorusFx : public Fx {
public:
  explicit ChorusFx(const Effect& e) {
    p[fxMix] = (float)e.number("mix", 0.5);
    p[fxDepth] = (float)e.number("modDepth", 0.25);
    p[fxRate] = (float)e.number("modRate", 1.0);
  }
  void prepare(double sr, int maxBlock) override {
    chorus.prepare({ sr, (juce::uint32)maxBlock, 2 });
    chorus.setCentreDelay(7.0f);
    chorus.setFeedback(0.0f);
  }
  void process(float* l, float* r, int n) override {
    const float mix = juce::jlimit(0.0f, 1.0f, p[fxMix].load());
    if (mix < 0.001f) return;
    chorus.setMix(mix);
    chorus.setDepth(juce::jlimit(0.0f, 1.0f, p[fxDepth].load()));
    chorus.setRate(juce::jlimit(0.01f, 20.0f, p[fxRate].load()));
    float* ch[2] = { l, r };
    juce::dsp::AudioBlock<float> block(ch, 2, (size_t)n);
    chorus.process(juce::dsp::ProcessContextReplacing<float>(block));
  }
private:
  juce::dsp::Chorus<float> chorus;
};

class PhaserFx : public Fx {
public:
  explicit PhaserFx(const Effect& e) {
    p[fxMix] = (float)e.number("mix", 0.5);
    p[fxDepth] = (float)e.number("modDepth", 0.5);
    p[fxRate] = (float)e.number("modRate", 0.5);
    p[fxCenter] = (float)e.number("centerFrequency", 400.0);
    p[fxFeedback] = (float)e.number("feedback", 0.5);
  }
  void prepare(double sr, int maxBlock) override { phaser.prepare({ sr, (juce::uint32)maxBlock, 2 }); }
  void process(float* l, float* r, int n) override {
    const float mix = juce::jlimit(0.0f, 1.0f, p[fxMix].load());
    if (mix < 0.001f) return;
    phaser.setMix(mix);
    phaser.setDepth(juce::jlimit(0.0f, 1.0f, p[fxDepth].load()));
    phaser.setRate(juce::jlimit(0.01f, 20.0f, p[fxRate].load()));
    phaser.setCentreFrequency(juce::jlimit(20.0f, 18000.0f, p[fxCenter].load()));
    phaser.setFeedback(juce::jlimit(-0.95f, 0.95f, p[fxFeedback].load()));
    float* ch[2] = { l, r };
    juce::dsp::AudioBlock<float> block(ch, 2, (size_t)n);
    phaser.process(juce::dsp::ProcessContextReplacing<float>(block));
  }
private:
  juce::dsp::Phaser<float> phaser;
};

// ------------------------------------------------------------------- delay

class DelayFx : public Fx {
public:
  explicit DelayFx(const Effect& e) : musical(e.text("delayTimeFormat") == "musical_time") {
    p[fxTime] = (float)e.number("delayTime", 0.25);
    p[fxFeedback] = (float)e.number("feedback", 0.4);
    p[fxWet] = (float)e.number("wetLevel", 0.3);
    p[fxOffset] = (float)e.number("stereoOffset", 0.0);
  }
  void prepare(double sampleRate, int) override {
    sr = sampleRate;
    size = (int)(sr * 4.0) + 8;
    for (auto& b : buf) b.assign((size_t)size, 0.0f);
    pos = 0;
  }
  void process(float* l, float* r, int n) override {
    const float wet = juce::jlimit(0.0f, 1.0f, p[fxWet].load());
    if (wet < 0.001f) return;
    const double base = musical ? p[fxTime].load() * 0.05 : p[fxTime].load();
    const double tL = juce::jlimit(0.005, 3.5, base);
    const double tR = juce::jlimit(0.005, 3.5, base + p[fxOffset].load());
    const float fb = juce::jlimit(0.0f, 0.95f, p[fxFeedback].load());
    for (int i = 0; i < n; ++i) {
      const float dl = read(0, tL * sr), dr = read(1, tR * sr);
      buf[0][(size_t)pos] = l[i] + fb * dl;
      buf[1][(size_t)pos] = r[i] + fb * dr;
      l[i] += wet * dl;
      r[i] += wet * dr;
      if (++pos >= size) pos = 0;
    }
  }
private:
  float read(int ch, double delay) const {
    double at = pos - delay;
    while (at < 0) at += size;
    const int a = (int)at;
    const float f = (float)(at - a);
    const float x0 = buf[(size_t)ch][(size_t)a];
    const float x1 = buf[(size_t)ch][(size_t)((a + 1) % size)];
    return x0 + (x1 - x0) * f;
  }
  bool musical;
  double sr = 44100.0;
  int size = 0, pos = 0;
  std::array<std::vector<float>, 2> buf;
};

// ------------------------------------------------------------- convolution

class ConvolutionFx : public Fx {
public:
  ConvolutionFx(const Effect& e, const juce::File& root) : root(root) {
    p[fxWet] = (float)e.number("wetLevel", 0.0);
    p[fxMix] = (float)e.number("mix", 0.0);
    ir = e.text("irFile");
  }
  void prepare(double sr, int maxBlock) override {
    conv.prepare({ sr, (juce::uint32)maxBlock, 2 });
    wetL.assign((size_t)maxBlock, 0.0f);
    wetR.assign((size_t)maxBlock, 0.0f);
    if (ir.isNotEmpty()) loadImpulse(SampleLoader::resolve(root, ir));
  }
  void loadImpulse(const juce::File& file) override {
    if (!file.existsAsFile()) return;
    conv.loadImpulseResponse(file, juce::dsp::Convolution::Stereo::yes,
                             juce::dsp::Convolution::Trim::yes, 0,
                             juce::dsp::Convolution::Normalise::yes);
  }
  void process(float* l, float* r, int n) override {
    const float amount = juce::jlimit(0.0f, 1.0f, juce::jmax(p[fxMix].load(), p[fxWet].load()));
    if (amount < 0.001f) return;
    std::copy(l, l + n, wetL.begin());
    std::copy(r, r + n, wetR.begin());
    float* ch[2] = { wetL.data(), wetR.data() };
    juce::dsp::AudioBlock<float> block(ch, 2, (size_t)n);
    conv.process(juce::dsp::ProcessContextReplacing<float>(block));
    const float dry = 1.0f - 0.35f * amount;
    for (int i = 0; i < n; ++i) {
      l[i] = l[i] * dry + wetL[(size_t)i] * amount;
      r[i] = r[i] * dry + wetR[(size_t)i] * amount;
    }
  }
private:
  juce::File root;
  juce::String ir;
  juce::dsp::Convolution conv;
  std::vector<float> wetL, wetR;
};

// -------------------------------------------------------------------- gain

class GainFx : public Fx {
public:
  explicit GainFx(const Effect& e) { p[fxLevel] = (float)e.number("level", 0.0); }
  void prepare(double, int) override {}
  void process(float* l, float* r, int n) override {
    const float g = juce::Decibels::decibelsToGain(p[fxLevel].load());
    for (int i = 0; i < n; ++i) { l[i] *= g; r[i] *= g; }
  }
};

// Effects the engine does not know still hold their slot, so the indices that
// bindings use keep lining up.
class PassFx : public Fx {
public:
  void prepare(double, int) override {}
  void process(float*, float*, int) override {}
};

std::unique_ptr<Fx> make(const Effect& e, const juce::File& root) {
  FilterSection::Kind kind;
  int poles;
  std::unique_ptr<Fx> out;
  if (FilterSection::parse(e.type, kind, poles)) out = std::make_unique<FilterFx>(e, kind, poles);
  else if (e.type == "reverb") out = std::make_unique<ReverbFx>(e);
  else if (e.type == "chorus") out = std::make_unique<ChorusFx>(e);
  else if (e.type == "phaser") out = std::make_unique<PhaserFx>(e);
  else if (e.type == "delay") out = std::make_unique<DelayFx>(e);
  else if (e.type == "convolution") out = std::make_unique<ConvolutionFx>(e, root);
  else if (e.type == "gain") out = std::make_unique<GainFx>(e);
  else out = std::make_unique<PassFx>();
  out->type = e.type;
  return out;
}

} // namespace

bool fxParamFromName(const juce::String& name, FxParam& out) {
  for (auto& n : kNames)
    if (name == n.name) { out = n.param; return true; }
  return false;
}

// ------------------------------------------------------------ FilterSection

bool FilterSection::parse(const juce::String& type, Kind& kind, int& poles) {
  const auto t = type.toLowerCase();
  if (t.startsWith("lowpass")) kind = lowpass;
  else if (t.startsWith("highpass")) kind = highpass;
  else if (t.startsWith("bandpass")) kind = bandpass;
  else return false;
  poles = t.endsWith("_4pl") ? 4 : t.endsWith("_1pl") ? 1 : 2;
  return true;
}

void FilterSection::reset() {
  for (auto& s : stage) { s.z1[0] = s.z1[1] = s.z2[0] = s.z2[1] = 0.0; }
}

void FilterSection::update(double freq, double res) {
  freq = juce::jlimit(20.0, juce::jmin(22000.0, sr * 0.45), freq);
  res = juce::jmax(0.1, res);
  if (freq == lastFreq && res == lastRes) return;
  lastFreq = freq; lastRes = res;

  if (poles == 1) {
    const double a = std::exp(-2.0 * kPi * freq / sr);
    auto& s = stage[0];
    if (kind == highpass) { s.b0 = (1 + a) / 2; s.b1 = -(1 + a) / 2; }
    else { s.b0 = 1 - a; s.b1 = 0; }
    s.b2 = 0; s.a1 = -a; s.a2 = 0;
  } else if (poles == 4) {
    designStage(stage[0], kind, sr, freq, 0.5412);
    designStage(stage[1], kind, sr, freq, juce::jmax(0.5, res * 1.85));
  } else {
    designStage(stage[0], kind, sr, freq, res);
  }
}

float FilterSection::process(int ch, float x) {
  float y = run(stage[0], ch, x);
  if (poles == 4) y = run(stage[1], ch, y);
  return y;
}

// -------------------------------------------------------------------- Chain

Chain::Chain(const std::vector<Effect>& effects, const juce::File& root, double sampleRate) {
  for (auto& e : effects) {
    fx.push_back(make(e, root));
    fx.back()->prepare(sampleRate, MAX_BLOCK);
  }
}

void Chain::process(float* left, float* right, int n) {
  for (auto& f : fx) f->process(left, right, n);
}

void Chain::set(int index, FxParam param, double value) {
  if (index >= 0 && index < (int)fx.size()) fx[(size_t)index]->p[param] = (float)value;
}

void Chain::loadImpulse(int index, const juce::File& file) {
  if (index >= 0 && index < (int)fx.size()) fx[(size_t)index]->loadImpulse(file);
}

} // namespace trove
