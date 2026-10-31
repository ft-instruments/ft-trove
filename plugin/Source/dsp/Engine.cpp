#include "Engine.h"
#include <algorithm>
#include <cmath>
#include "Env.h"

namespace trove {
namespace {

inline float hermite(const float* x, int i, float frac, int frames) {
  const int a = juce::jlimit(0, frames - 1, i - 1);
  const int b = juce::jlimit(0, frames - 1, i);
  const int c = juce::jlimit(0, frames - 1, i + 1);
  const int d = juce::jlimit(0, frames - 1, i + 2);
  const float c0 = (x[c] - x[a]) * 0.5f;
  const float v = x[b] - x[c];
  const float w = c0 + v;
  const float a3 = w + v + (x[d] - x[b]) * 0.5f;
  const float b3 = w + a3;
  return (((a3 * frac) - b3) * frac + c0) * frac + x[b];
}

// A cutoff table maps a 0..1 position onto Hz by straight lines between points.
float tableLookup(const std::vector<std::pair<float, float>>& t, float x) {
  if (t.empty()) return 22000.0f;
  if (x <= t.front().first) return t.front().second;
  for (size_t i = 1; i < t.size(); ++i)
    if (x <= t[i].first) {
      const float span = juce::jmax(1.0e-6f, t[i].first - t[i - 1].first);
      return t[i - 1].second + (x - t[i - 1].first) / span * (t[i].second - t[i - 1].second);
    }
  return t.back().second;
}

float tableInverse(const std::vector<std::pair<float, float>>& t, float y) {
  if (t.empty()) return 1.0f;
  if (y <= t.front().second) return t.front().first;
  for (size_t i = 1; i < t.size(); ++i)
    if (y <= t[i].second) {
      const float span = juce::jmax(1.0e-6f, t[i].second - t[i - 1].second);
      return t[i - 1].first + (y - t[i - 1].second) / span * (t[i].first - t[i - 1].first);
    }
  return t.back().first;
}

inline float softClip(float x) {
  constexpr float knee = 0.97f;
  if (x > knee) return knee + (1.0f - knee) * std::tanh((x - knee) / (1.0f - knee));
  if (x < -knee) return -knee - (1.0f - knee) * std::tanh((-x - knee) / (1.0f - knee));
  return x;
}

} // namespace

struct Engine::Voice {
  std::shared_ptr<const Program> program;
  const Zone* zone = nullptr;
  juce::uint64 order = 0;
  int note = 60;
  int group = 0;

  double pos = 0.0, ratio = 1.0, baseRatio = 1.0;
  double stopPos = 1.0, loopStart = 0.0, loopEnd = 0.0;
  bool loop = false;

  bool active = false, released = false, oneShot = false, sustained = false;
  double baseGain = 1.0;          // zone gain and velocity, before panel controls
  float curL = 0.0f, curR = 0.0f; // the gains the last chunk ended on
  bool fresh = true;
  int fade = 0;
  Envelope env;
  std::array<Envelope, Engine::MAX_MODS> modEnv;   // per-voice modulator envelopes
  float velocity01 = 0.0f;

  // The filters and gain a group carries, applied to this voice alone.
  struct GroupStage { int kind = 0; FilterSection section; };  // 0 none, 1 filter, 2 gain
  std::array<GroupStage, Engine::GROUP_FX> stages;

  // Adds the raw sample (envelope and de-click fade included, no gain) to
  // left and right, which the engine has cleared.
  void render(float* left, float* right, int n);
};

void Engine::Voice::render(float* left, float* right, int n) {
  if (!zone) { active = false; return; }
  const auto* s = zone->sample.get();
  if (s == nullptr || s->frames < 4) { active = false; return; }

  const float* sl = s->left();
  const float* sr = s->right();
  const int frames = (int)s->frames;
  const double boundary = loop ? loopEnd : stopPos;

  for (int i = 0; i < n; ++i) {
    if (pos >= boundary) {
      if (loop) {
        pos -= (loopEnd - loopStart);
        if (pos < loopStart) pos = loopStart;
      } else {
        active = false;
        return;
      }
    }

    float e = 1.0f;
    if (!oneShot) {
      e = env.next();
      if (!env.active()) { active = false; return; }
    }

    const int idx = (int)pos;
    const float frac = (float)(pos - idx);
    const float l = hermite(sl, idx, frac, frames);
    const float r = (sr == sl) ? l : hermite(sr, idx, frac, frames);

    float f = 1.0f;
    if (fade < 64) { f = (float)fade / 64.0f; ++fade; }

    left[i] += l * e * f;
    right[i] += r * e * f;

    pos += ratio;
  }
}

Engine::Engine() {
  for (auto& v : voices) v = std::make_unique<Voice>();
  rrCursor.fill(0);
  rrLast.fill(0);
  for (auto& g : groupOn) g = true;
  for (auto& g : groupGain) g = 1.0f;
  for (auto& g : groupPanOffset) g = 0.0f;
  for (auto& g : tagGain) g = 1.0f;
  for (auto& g : velocityOn) g = true;
  for (auto& g : groupFxPos) g = 1.0f;
  for (auto& g : groupFx) for (auto& f : g) { f[0] = 22000.0f; f[1] = 0.7f; f[2] = 0.0f; }
  for (int i = 0; i < NUM_PARAMS; ++i) params[(size_t)i] = PARAMS[i].def;
}

Engine::~Engine() = default;

void Engine::prepare(double sampleRate) {
  sr = sampleRate > 0 ? sampleRate : 44100.0;
  for (auto& v : voices) { v->active = false; v->env.reset(); }
  toneL.reset();
  toneR.reset();
  lastTone = -1.0;
  reverb.setSampleRate(sr);
  lastReverb = -1.0;
  panicRequest = false;
  activeCount = 0;
  peakOut = 0.0f;

  std::shared_ptr<const Program> current = program();
  if (current != nullptr) {
    auto rebuilt = std::make_shared<Chain>(current->effects, juce::File(current->rootDir), sr);
    const juce::SpinLock::ScopedLockType sl(programLock);
    chain = std::move(rebuilt);
  }
}

std::shared_ptr<const Program> Engine::program() const {
  const juce::SpinLock::ScopedLockType sl(programLock);
  return prog;
}

void Engine::setProgram(std::shared_ptr<const Program> program) {
  std::shared_ptr<Chain> built;
  if (program != nullptr) {
    resetRuntime(*program);
    built = std::make_shared<Chain>(program->effects, juce::File(program->rootDir), sr);
  }
  {
    const juce::SpinLock::ScopedLockType sl(programLock);
    prog = std::move(program);
    chain = std::move(built);
  }
  rrCursor.fill(0);
  rrLast.fill(0);
}

// Back to what the preset itself says, before the panel applies its values.
void Engine::resetRuntime(const Program& program) {
  for (int g = 0; g < 256; ++g) {
    groupOn[(size_t)g] = g >= (int)program.groupOn.size() || program.groupOn[(size_t)g] != 0;
    groupGain[(size_t)g] = 1.0f;
    groupPanOffset[(size_t)g] = 0.0f;
    for (int i = 0; i < GROUP_FX; ++i) { groupFx[(size_t)g][(size_t)i][0] = 22000.0f; groupFx[(size_t)g][(size_t)i][1] = 0.7f; groupFx[(size_t)g][(size_t)i][2] = 0.0f; }
  }
  for (auto& t : tagGain) t = 1.0f;
  globalTuning = 0.0f;
  for (int i = 0; i < MAX_MODS; ++i) {
    const bool have = i < (int)program.modulators.size();
    const auto& m = have ? program.modulators[(size_t)i] : Modulator();
    mods[(size_t)i].amount = (float)m.amount;
    mods[(size_t)i].attack = (float)m.env.attack;
    mods[(size_t)i].decay = (float)m.env.decay;
    mods[(size_t)i].sustain = (float)m.env.sustain;
    mods[(size_t)i].release = (float)m.env.release;
  }
  for (int i = 0; i < (int)velocityOn.size(); ++i)
    velocityOn[(size_t)i] = i < (int)program.velocity.size() && program.velocity[(size_t)i].enabled;
  for (auto& p : groupFxPos) p = 1.0f;
  for (int g = 0; g < (int)program.groupEffects.size() && g < 256; ++g)
    for (int i = 0; i < (int)program.groupEffects[(size_t)g].size() && i < GROUP_FX; ++i) {
      const auto& e = program.groupEffects[(size_t)g][(size_t)i];
      groupFx[(size_t)g][(size_t)i][0] = (float)e.number("frequency", 22000.0);
      groupFx[(size_t)g][(size_t)i][1] = (float)e.number("resonance", 0.7);
      groupFx[(size_t)g][(size_t)i][2] = (float)e.number("level", 0.0);
      // a modulated cutoff starts at the table position its own value sits at
      if (g < (int)program.slotTable.size() && !program.slotTable[(size_t)g][(size_t)i].empty())
        groupFxPos[(size_t)(g * GROUP_FX + i)] =
          tableInverse(program.slotTable[(size_t)g][(size_t)i], (float)e.number("frequency", 22000.0));
    }
}

void Engine::setGroupEnabled(int group, bool on) {
  if (group >= 0 && group < 256) groupOn[(size_t)group] = on;
}

void Engine::setGroupVolume(int group, double linear) {
  if (group < 0 || group >= 256) return;
  const auto p = program();
  const double base = (p != nullptr && group < (int)p->groupVolume.size()) ? p->groupVolume[(size_t)group] : 1.0;
  groupGain[(size_t)group] = (float)(base > 1.0e-6 ? juce::jmax(0.0, linear) / base : juce::jmax(0.0, linear));
}

void Engine::setGroupPan(int group, double pan) {
  if (group >= 0 && group < 256) groupPanOffset[(size_t)group] = (float)juce::jlimit(-1.0, 1.0, pan);
}

void Engine::setTagVolume(const juce::String& tag, double linear) {
  const auto p = program();
  if (p == nullptr) return;
  const int id = p->tagNames.indexOf(tag);
  if (id >= 0 && id < MAX_TAGS) tagGain[(size_t)id] = (float)juce::jmax(0.0, linear);
}

void Engine::setGroupFx(int group, int index, FxParam param, double value) {
  if (group < 0 || group >= 256 || index < 0 || index >= GROUP_FX) return;
  const int slot = param == fxFreq ? 0 : param == fxRes ? 1 : param == fxLevel ? 2 : -1;
  if (slot >= 0) groupFx[(size_t)group][(size_t)index][(size_t)slot] = (float)value;
}

void Engine::setFx(int index, FxParam param, double value) {
  std::shared_ptr<Chain> c;
  {
    const juce::SpinLock::ScopedLockType sl(programLock);
    c = chain;
  }
  if (c != nullptr) c->set(index, param, value);
}

void Engine::loadImpulse(int index, const juce::File& file) {
  std::shared_ptr<Chain> c;
  {
    const juce::SpinLock::ScopedLockType sl(programLock);
    c = chain;
  }
  if (c != nullptr) c->loadImpulse(index, file);
}

void Engine::setGlobalTuning(double semitones) {
  globalTuning = (float)juce::jlimit(-24.0, 24.0, semitones);
}

void Engine::setModParam(int index, const juce::String& parameter, double value) {
  if (index < 0 || index >= MAX_MODS) return;
  auto& m = mods[(size_t)index];
  if (parameter == "MOD_AMOUNT") m.amount = (float)value;
  else if (parameter == "ENV_ATTACK") m.attack = (float)juce::jmax(0.0, value);
  else if (parameter == "ENV_DECAY") m.decay = (float)juce::jmax(0.0, value);
  else if (parameter == "ENV_SUSTAIN") m.sustain = (float)juce::jlimit(0.0, 1.0, value);
  else if (parameter == "ENV_RELEASE") m.release = (float)juce::jmax(0.0, value);
}

void Engine::setVelocityEnabled(int index, bool on) {
  if (index >= 0 && index < (int)velocityOn.size()) velocityOn[(size_t)index] = on;
}

void Engine::setGroupFxPosition(int group, int index, double position) {
  if (group >= 0 && group < 256 && index >= 0 && index < GROUP_FX)
    groupFxPos[(size_t)(group * GROUP_FX + index)] = (float)juce::jlimit(0.0, 1.0001, position);
}

void Engine::setParams(const std::array<double, NUM_PARAMS>& values) {
  params = values;
}

void Engine::startVoice(const Zone& zone, int note, float velocity,
                        const std::shared_ptr<const Program>& program) {
  const auto* s = zone.sample.get();
  if (s == nullptr) return;

  Voice* slot = nullptr;
  for (auto& v : voices)
    if (!v->active) { slot = v.get(); break; }
  if (slot == nullptr) {
    Voice* best = nullptr;
    for (auto& v : voices)
      if (v->released && (best == nullptr || v->order < best->order)) best = v.get();
    if (best == nullptr)
      for (auto& v : voices)
        if (best == nullptr || v->order < best->order) best = v.get();
    slot = best;
  }
  if (slot == nullptr) return;

  slot->program = program;
  slot->zone = &zone;
  slot->note = note;
  slot->order = ++order;
  slot->active = true;
  slot->released = false;
  slot->sustained = false;
  slot->fade = 0;
  slot->oneShot = !zone.ampEnvEnabled;

  const double semis = (note - zone.rootNote) * zone.pitchKeyTrack
                     + zone.tuning * (zone.tuningIgnoresKeyTrack ? 1.0 : zone.pitchKeyTrack);
  slot->baseRatio = std::pow(2.0, semis / 12.0) * (s->sourceRate / sr);
  slot->ratio = slot->baseRatio;
  slot->group = juce::jlimit(0, 255, zone.group);
  slot->fresh = true;

  const double frames = (double)s->frames;
  slot->pos = juce::jlimit(0.0, frames - 2.0, zone.startFrame);
  slot->stopPos = zone.endFrame > 0.0 ? juce::jmin(zone.endFrame, frames - 2.0) : frames - 2.0;
  slot->stopPos = juce::jmax(slot->pos + 1.0, slot->stopPos);
  slot->loopStart = juce::jlimit(slot->pos, slot->stopPos - 1.0, zone.loopStart);
  slot->loopEnd = juce::jlimit(slot->loopStart + 2.0, slot->stopPos, zone.loopEnd);
  slot->loop = zone.loopEnabled && slot->loopEnd > slot->loopStart + 2.0;

  const double vel = juce::jlimit(0.0, 1.0, (double)velocity / 127.0);
  const double vg = (1.0 - zone.ampVelTrack) + zone.ampVelTrack * vel;
  slot->baseGain = zone.gain * vg;

  for (auto& st : slot->stages) st.kind = 0;
  if (slot->group < (int)program->groupEffects.size()) {
    const auto& list = program->groupEffects[(size_t)slot->group];
    for (int i = 0; i < (int)list.size() && i < GROUP_FX; ++i) {
      FilterSection::Kind kind;
      int poles;
      auto& st = slot->stages[(size_t)i];
      if (FilterSection::parse(list[(size_t)i].type, kind, poles)) {
        st.kind = 1;
        st.section.setup(sr, kind, poles);
        st.section.reset();
      } else if (list[(size_t)i].type == "gain") {
        st.kind = 2;
      }
    }
  }

  const auto& pe = program->env;
  const double attack = juce::jmax(0.0, zone.env.attack + (params[pAttack] - pe.attack));
  const double decay = juce::jmax(0.0, zone.env.decay + (params[pDecay] - pe.decay));
  const double release = juce::jmax(0.0005, zone.env.release + (params[pRelease] - pe.release));
  const double sustain = pe.sustain > 0.0005
    ? juce::jlimit(0.0, 1.0, zone.env.sustain * params[pSustain] / pe.sustain)
    : juce::jlimit(0.0, 1.0, params[pSustain]);

  slot->env.setSampleRate(sr);
  slot->env.set(attack, zone.env.hold, decay, sustain, release);
  slot->env.reset();
  slot->env.gate(true);

  slot->velocity01 = (float)vel;
  for (int i = 0; i < MAX_MODS; ++i) {
    auto& e = slot->modEnv[(size_t)i];
    e.setSampleRate(sr);
    e.set(mods[(size_t)i].attack, 0.0, mods[(size_t)i].decay, mods[(size_t)i].sustain, mods[(size_t)i].release);
    e.reset();
    e.gate(true);
  }
}

void Engine::noteOn(int note, float velocity) {
  std::shared_ptr<const Program> program;
  {
    const juce::SpinLock::ScopedLockType sl(programLock);
    program = prog;
  }
  if (program == nullptr) return;

  const int vel = juce::jlimit(1, 127, (int)std::lround((double)velocity));
  const Zone* candidates[256];
  int count = 0;
  for (auto& z : program->zones) {
    if (count >= 256) break;
    if (!z.matches(note, vel)) continue;
    if (!groupOn[(size_t)juce::jlimit(0, 255, z.group)]) continue;
    if (z.trigger != "attack" && z.trigger != "first" && z.trigger != "legato") continue;
    candidates[count++] = &z;
  }
  if (count == 0) return;

  // Round robin runs across a whole set of groups that share a seqLength
  // ("Pick RR1".."RR4" are four groups, one position each), and a key often
  // has a sample at only some of those positions. So per length: collect the
  // positions this key actually has, and cycle through just those, with a
  // cursor of its own per key. Anything else would sit on a position this key
  // lacks and play nothing, forever.
  auto lengthOf = [&](const Zone& z) {
    const int g = juce::jlimit(0, 255, z.group);
    return juce::jlimit(1, 64, z.seqLength > 0
      ? z.seqLength
      : (g < (int)program->seqLen.size() ? program->seqLen[(size_t)g] : 1));
  };

  juce::uint64 available[65] = {};
  int mode[65];
  int target[65];
  bool started[65] = {};
  bool take[256] = {};
  for (int i = 0; i < 65; ++i) { mode[i] = seqAlways; target[i] = -1; }

  for (int i = 0; i < count; ++i) {
    const Zone& z = *candidates[i];
    const int len = lengthOf(z);
    if (z.seqMode == seqAlways || len <= 1) continue;
    mode[len] = z.seqMode;
    available[len] |= (juce::uint64)1 << (juce::jlimit(1, 64, z.seqPosition) - 1);
  }

  for (int len = 2; len <= 64; ++len) {
    if (available[len] == 0) continue;
    int positions[64];
    int n = 0;
    for (int bit = 0; bit < 64; ++bit)
      if (available[len] & ((juce::uint64)1 << bit)) positions[n++] = bit + 1;

    const size_t slot = (size_t)len * 128 + (size_t)juce::jlimit(0, 127, note);
    if (mode[len] == seqRoundRobin) {
      target[len] = positions[rrCursor[slot] % (juce::uint32)n];
    } else {
      int pick = rng.nextInt(n);
      if (n > 1 && positions[pick] == rrLast[slot]) pick = (pick + 1) % n;
      target[len] = positions[pick];
    }
  }

  for (int i = 0; i < count; ++i) {
    const Zone& z = *candidates[i];
    const int len = lengthOf(z);
    take[i] = z.seqMode == seqAlways || len <= 1 || z.seqPosition == target[len];
  }

  for (int i = 0; i < count; ++i) {
    if (!take[i]) continue;
    const Zone& z = *candidates[i];
    startVoice(z, note, velocity, program);
    started[lengthOf(z)] = true;
  }

  for (int len = 2; len <= 64; ++len) {
    if (!started[len] || target[len] < 0) continue;
    const size_t slot = (size_t)len * 128 + (size_t)juce::jlimit(0, 127, note);
    rrLast[slot] = target[len];
    ++rrCursor[slot];
  }
}

void Engine::noteOff(int note) {
  for (auto& v : voices) {
    if (!v->active || v->note != note || v->released) continue;
    if (sustainDown) v->sustained = true;
    else { v->env.gate(false); for (auto& e : v->modEnv) e.gate(false); }
    v->released = true;
  }
}

void Engine::setSustain(bool down) {
  sustainDown = down;
  if (!down) {
    for (auto& v : voices) {
      if (!v->active || !v->sustained) continue;
      v->sustained = false;
      v->env.gate(false);
      for (auto& e : v->modEnv) e.gate(false);
    }
  }
}

void Engine::panic() { panicRequest = true; }

void Engine::process(juce::AudioBuffer<float>& buffer) {
  const int n = buffer.getNumSamples();
  const int channels = buffer.getNumChannels();
  if (n <= 0 || channels <= 0) return;

  buffer.clear();
  float* left = buffer.getWritePointer(0);
  float* right = channels > 1 ? buffer.getWritePointer(1) : nullptr;

  if (panicRequest.exchange(false))
    for (auto& v : voices) { v->active = false; v->env.reset(); }

  std::shared_ptr<const Program> current;
  std::shared_ptr<Chain> activeChain;
  {
    const juce::SpinLock::ScopedLockType sl(programLock);
    current = prog;
    activeChain = chain;
  }

  float peak = 0.0f;
  for (int offset = 0; offset < n; offset += Chain::MAX_BLOCK) {
    const int m = juce::jmin(Chain::MAX_BLOCK, n - offset);
    peak = juce::jmax(peak, renderChunk(left + offset, right != nullptr ? right + offset : nullptr,
                                        m, current, activeChain.get()));
  }

  int live = 0;
  for (auto& v : voices) if (v->active) ++live;

  for (int c = 2; c < channels; ++c) buffer.clear(c, 0, n);

  activeCount = live;
  peakOut = peak;
}

float Engine::renderChunk(float* left, float* right, int n,
                          const std::shared_ptr<const Program>& program, Chain* chain) {
  const double tuneRatio = std::pow(2.0, (double)globalTuning.load() / 12.0);

  // each LFO once per chunk; envelopes are per voice, further down
  static const std::vector<std::pair<float, float>> noTable;
  for (int i = 0; i < MAX_MODS; ++i) {
    modValue[(size_t)i] = 0.0f;
    if (program == nullptr || i >= (int)program->modulators.size() || program->modulators[(size_t)i].envelope) continue;
    modValue[(size_t)i] = (float)std::sin(lfoPhase[(size_t)i]);
    lfoPhase[(size_t)i] += 2.0 * juce::MathConstants<double>::pi * program->modulators[(size_t)i].lfoHz * n / sr;
    if (lfoPhase[(size_t)i] > 1.0e6) lfoPhase[(size_t)i] = 0.0;
  }

  for (auto& v : voices) {
    if (!v->active) continue;
    if (v->program.get() != program.get()) { v->active = false; v->env.reset(); continue; }

    std::fill(voiceL.begin(), voiceL.begin() + n, 0.0f);
    std::fill(voiceR.begin(), voiceR.begin() + n, 0.0f);
    const int g = v->group;

    // drift: an LFO on a group's tuning
    double driftSemis = 0.0;
    for (int mi = 0; mi < MAX_MODS && mi < (int)program->modulators.size(); ++mi) {
      const auto& m = program->modulators[(size_t)mi];
      if (m.envelope) continue;
      for (auto& t : m.targets)
        if (t.parameter == "GROUP_TUNING" && t.group == g)
          driftSemis += modValue[(size_t)mi] * mods[(size_t)mi].amount.load() *
                        (t.hasRange ? juce::jmax(std::abs(t.outMin), std::abs(t.outMax)) : 1.0);
    }
    v->ratio = v->baseRatio * tuneRatio * (driftSemis != 0.0 ? std::pow(2.0, driftSemis / 12.0) : 1.0);
    v->render(voiceL.data(), voiceR.data(), n);

    float envOut[MAX_MODS] = {};
    for (int mi = 0; mi < MAX_MODS && mi < (int)program->modulators.size(); ++mi)
      if (program->modulators[(size_t)mi].envelope)
        for (int k = 0; k < n; ++k) envOut[mi] = v->modEnv[(size_t)mi].next();

    for (int i = 0; i < GROUP_FX; ++i) {
      auto& st = v->stages[(size_t)i];
      if (st.kind == 1) {
        const auto& table = g < (int)program->slotTable.size() ? program->slotTable[(size_t)g][(size_t)i] : noTable;
        double cutoff = groupFx[(size_t)g][(size_t)i][0];
        if (!table.empty()) {
          // something modulates this cutoff: move along its table
          float position = groupFxPos[(size_t)(g * GROUP_FX + i)];
          for (int mi = 0; mi < MAX_MODS && mi < (int)program->modulators.size(); ++mi) {
            const auto& m = program->modulators[(size_t)mi];
            if (!m.envelope) continue;
            for (auto& t : m.targets)
              if (t.parameter == "FX_FILTER_FREQUENCY" && t.group == g && t.effect == i)
                position += envOut[mi] * mods[(size_t)mi].amount.load();
          }
          for (int vi = 0; vi < (int)program->velocity.size() && vi < (int)velocityOn.size(); ++vi) {
            const auto& b = program->velocity[(size_t)vi];
            if (velocityOn[(size_t)vi] && b.target.parameter == "FX_FILTER_FREQUENCY" && b.target.group == g && b.target.effect == i)
              position += v->velocity01 * (float)b.amount;
          }
          cutoff = tableLookup(table, juce::jlimit(0.0f, 1.0001f, position));
        }
        st.section.update(cutoff, groupFx[(size_t)g][(size_t)i][1]);
        for (int k = 0; k < n; ++k) {
          voiceL[(size_t)k] = st.section.process(0, voiceL[(size_t)k]);
          voiceR[(size_t)k] = st.section.process(1, voiceR[(size_t)k]);
        }
      } else if (st.kind == 2) {
        const float gain = juce::Decibels::decibelsToGain(groupFx[(size_t)g][(size_t)i][2].load());
        for (int k = 0; k < n; ++k) { voiceL[(size_t)k] *= gain; voiceR[(size_t)k] *= gain; }
      }
    }

    float level = groupGain[(size_t)g];
    for (int t : v->zone->tags)
      if (t >= 0 && t < MAX_TAGS) level *= tagGain[(size_t)t];
    const double pan = juce::jlimit(-1.0, 1.0, v->zone->pan + (double)groupPanOffset[(size_t)g].load());
    const double angle = (pan + 1.0) * juce::MathConstants<double>::pi * 0.25;
    const float targetL = (float)(v->baseGain * level * std::cos(angle));
    const float targetR = (float)(v->baseGain * level * std::sin(angle));
    if (v->fresh) { v->curL = targetL; v->curR = targetR; v->fresh = false; }

    // glide to the new gains over the chunk, so turning a knob never clicks
    const float stepL = (targetL - v->curL) / (float)n;
    const float stepR = (targetR - v->curR) / (float)n;
    float gl = v->curL, gr = v->curR;
    for (int k = 0; k < n; ++k) {
      gl += stepL;
      gr += stepR;
      left[k] += voiceL[(size_t)k] * gl;
      if (right != nullptr) right[k] += voiceR[(size_t)k] * gr;
    }
    v->curL = targetL;
    v->curR = targetR;
  }

  if (chain != nullptr) {
    if (right != nullptr) {
      chain->process(left, right, n);
    } else {
      std::copy(left, left + n, monoRight.begin());
      chain->process(left, monoRight.data(), n);
      for (int k = 0; k < n; ++k) left[k] = 0.5f * (left[k] + monoRight[(size_t)k]);
    }
  }

  const double tone = juce::jlimit(0.0, 1.0, params[pTone]);
  if (std::abs(tone - lastTone) > 1.0e-4) {
    lastTone = tone;
    const double cutoff = juce::jmin(20000.0, 200.0 * std::pow(100.0, tone));
    auto coeff = juce::dsp::IIR::Coefficients<float>::makeLowPass(sr, cutoff);
    toneL.coefficients = coeff;
    toneR.coefficients = coeff;
    toneL.reset();
    toneR.reset();
  }
  for (int i = 0; i < n; ++i) {
    left[i] = toneL.processSample(left[i]);
    if (right != nullptr) right[i] = toneR.processSample(right[i]);
  }

  const double mix = juce::jlimit(0.0, 1.0, params[pReverb]);
  if (std::abs(mix - lastReverb) > 1.0e-4 || lastReverb < 0.0) {
    lastReverb = mix;
    juce::Reverb::Parameters rp;
    rp.roomSize = 0.55f + 0.35f * (float)mix;
    rp.damping = 0.42f;
    rp.wetLevel = (float)mix;
    rp.dryLevel = 1.0f - 0.30f * (float)mix;
    rp.width = 1.0f;
    reverb.setParameters(rp);
  }
  if (mix > 0.001) {
    if (right != nullptr) reverb.processStereo(left, right, n);
    else reverb.processMono(left, n);
  }

  const float gain = (float)juce::Decibels::decibelsToGain(params[pGain]);
  float peak = 0.0f;
  for (int i = 0; i < n; ++i) {
    left[i] = softClip(left[i] * gain);
    peak = juce::jmax(peak, std::abs(left[i]));
    if (right != nullptr) {
      right[i] = softClip(right[i] * gain);
      peak = juce::jmax(peak, std::abs(right[i]));
    }
  }
  return peak;
}

} // namespace trove
