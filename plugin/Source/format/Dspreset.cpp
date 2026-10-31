#include "Dspreset.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <set>
#include "SampleLoader.h"

namespace trove {
namespace {

constexpr int kMaxGroups = 256;

double parseGain(const juce::String& text, double fallback) {
  auto s = text.trim();
  if (s.isEmpty()) return fallback;
  if (s.endsWithIgnoreCase("db"))
    return juce::Decibels::decibelsToGain((float)s.dropLastCharacters(2).getDoubleValue());
  return juce::jlimit(0.0, 8.0, s.getDoubleValue());
}

bool getGain(const juce::XmlElement& e, const char* primary, const char* alias, double& out) {
  const juce::String s = (alias != nullptr && e.hasAttribute(alias)) ? e.getStringAttribute(alias)
                        : e.hasAttribute(primary) ? e.getStringAttribute(primary)
                        : juce::String();
  if (s.isEmpty()) return false;
  out = parseGain(s, out);
  return true;
}

bool getNumber(const juce::XmlElement& e, std::initializer_list<const char*> names, double& out) {
  for (auto* name : names) {
    if (!e.hasAttribute(name)) continue;
    out = e.getDoubleAttribute(name, out);
    return true;
  }
  return false;
}

bool getBool(const juce::XmlElement& e, std::initializer_list<const char*> names, bool& out) {
  for (auto* name : names) {
    if (!e.hasAttribute(name)) continue;
    out = e.getBoolAttribute(name, out);
    return true;
  }
  return false;
}

int readSeqMode(const juce::String& s) {
  if (s == "round_robin") return seqRoundRobin;
  if (s == "random") return seqRandom;
  if (s == "true_random") return seqTrueRandom;
  return seqAlways;
}

bool readEnv(Env& env, const juce::XmlElement& e) {
  bool any = false;
  auto n = [&](const char* a, const char* b, double& dst) {
    double v = dst;
    if (getNumber(e, { b, a }, v)) { dst = v; any = true; }
  };
  n("attack", "ampEnvAttack", env.attack);
  n("hold", "ampEnvHold", env.hold);
  n("decay", "ampEnvDecay", env.decay);
  n("sustain", "ampEnvSustain", env.sustain);
  n("release", "ampEnvRelease", env.release);
  return any;
}

struct Ctx {
  Env env;
  double volume = 1.0;
  double tuning = 0.0;
  bool ignoresKeyTrack = false;
  double keyTrack = 1.0;
  double pan = 0.0;
  double panOffset = 0.0;
  double ampVel = 1.0;
  double glide = 0.0;
  double crossfade = 0.0;
  bool loopDuringRelease = true;
  bool ampEnvEnabled = true;
  double delay = 0.0;
  bool delayInSamples = false;
  juce::String trigger = "attack";
  int seqMode = seqAlways;
  int seqLength = 0;
  int seqPosition = 1;
};

void readShared(Ctx& c, const juce::XmlElement& e) {
  readEnv(c.env, e);
  getBool(e, { "tuningIgnoresKeyTrack" }, c.ignoresKeyTrack);
  getNumber(e, { "pitchKeyTrack" }, c.keyTrack);
  getNumber(e, { "ampVelTrack" }, c.ampVel);
  getNumber(e, { "glideTime" }, c.glide);
  getNumber(e, { "loopCrossfade" }, c.crossfade);
  getBool(e, { "loopDuringRelease" }, c.loopDuringRelease);
  getBool(e, { "ampEnvEnabled" }, c.ampEnvEnabled);
  getNumber(e, { "delay" }, c.delay);
  if (e.hasAttribute("delayUnit"))
    c.delayInSamples = e.getStringAttribute("delayUnit") == "samples";
  if (e.hasAttribute("trigger")) c.trigger = e.getStringAttribute("trigger");
  if (e.hasAttribute("seqMode")) c.seqMode = readSeqMode(e.getStringAttribute("seqMode"));
  if (e.hasAttribute("seqLength")) c.seqLength = e.getIntAttribute("seqLength", c.seqLength);
  // Zither and most round-robin libraries set the position on the group.
  if (e.hasAttribute("seqPosition")) c.seqPosition = e.getIntAttribute("seqPosition", c.seqPosition);
}

void readGroups(Ctx& c, const juce::XmlElement& g) {
  getGain(g, "volume", "globalVolume", c.volume);
  getNumber(g, { "globalPan" }, c.panOffset);
  getNumber(g, { "globalTuning" }, c.tuning);
  readShared(c, g);
}

void readGroup(Ctx& c, const juce::XmlElement& g) {
  double v = 1.0;
  if (getGain(g, "volume", "groupVolume", v)) c.volume *= v;
  double pan = 0.0;
  if (getNumber(g, { "groupPan" }, pan)) c.panOffset += pan;
  double tuning = 0.0;
  if (getNumber(g, { "groupTuning" }, tuning)) c.tuning += tuning;
  readShared(c, g);
}

void readSample(Ctx& c, const juce::XmlElement& s) {
  double v = 1.0;
  if (getGain(s, "volume", nullptr, v)) c.volume *= v;
  getNumber(s, { "pan" }, c.pan);
  double tuning = 0.0;
  if (getNumber(s, { "tuning" }, tuning)) c.tuning += tuning;
  readShared(c, s);
}

juce::String findBackground(const juce::XmlElement& e) {
  static const char* names[] = {
    "backgroundImage", "backgroundImagePath", "bgImage", "coverImage", "cover", "artwork", "poster", "image"
  };
  for (auto* name : names)
    if (e.hasAttribute(name)) {
      const auto v = e.getStringAttribute(name);
      if (v.isNotEmpty()) return v;
    }
  for (auto* child = e.getFirstChildElement(); child; child = child->getNextElement())
    if (auto found = findBackground(*child); found.isNotEmpty())
      return found;
  return {};
}


// Decent Sampler writes colours as AARRGGBB; a bare two-digit value is a grey.
juce::String cssColour(const juce::String& value) {
  auto s = value.trim().removeCharacters("#");
  if (s.isEmpty() || !s.containsOnly("0123456789abcdefABCDEF")) return {};
  // Decent Sampler reads the text as a number, so a short value has no alpha
  // byte at all: textColor="EE" is invisible, not light grey.
  if (s.length() < 6) return "rgba(0,0,0,0)";
  if (s.length() == 6) s = "FF" + s;
  if (s.length() != 8) return {};
  const auto alpha = s.substring(0, 2).getHexValue32();
  const auto rgb = s.substring(2).toLowerCase();
  if (alpha >= 255) return "#" + rgb;
  return juce::String::formatted("rgba(%d,%d,%d,%.3f)", rgb.substring(0, 2).getHexValue32(),
                                 rgb.substring(2, 4).getHexValue32(), rgb.substring(4, 6).getHexValue32(),
                                 alpha / 255.0);
}

void readBindings(const juce::XmlElement& e, std::vector<UiBinding>& into) {
  for (auto* b = e.getFirstChildElement(); b; b = b->getNextElement()) {
    if (!b->hasTagName("binding")) continue;
    UiBinding out;
    out.type = b->getStringAttribute("type");
    out.parameter = b->getStringAttribute("parameter");
    out.level = b->getStringAttribute("level");
    out.position = b->getIntAttribute("position", 0);
    out.translation = b->getStringAttribute("translation").toLowerCase();
    out.table = b->getStringAttribute("translationTable");
    out.fixedValue = b->getStringAttribute("translationValue");
    out.hasOutputRange = b->hasAttribute("translationOutputMin") || b->hasAttribute("translationOutputMax");
    out.outMin = b->getDoubleAttribute("translationOutputMin", 0.0);
    out.outMax = b->getDoubleAttribute("translationOutputMax", 1.0);
    out.identifier = b->getStringAttribute("identifier");
    out.tags = b->getStringAttribute("tags");
    out.groupIndex = b->hasAttribute("groupIndex") ? b->getIntAttribute("groupIndex", -1) : -1;
    out.effectIndex = b->hasAttribute("effectIndex") ? b->getIntAttribute("effectIndex", -1) : -1;
    out.modulatorIndex = b->hasAttribute("modulatorIndex") ? b->getIntAttribute("modulatorIndex", -1) : -1;
    out.bindingIndex = b->hasAttribute("bindingIndex") ? b->getIntAttribute("bindingIndex", -1) : -1;
    out.hasFactor = b->hasAttribute("factor");
    out.factor = b->getDoubleAttribute("factor", 1.0);
    out.triggerOnLoad = !b->hasAttribute("triggerOnLoad") ||
                        b->getStringAttribute("triggerOnLoad").trim().equalsIgnoreCase("true");
    into.push_back(std::move(out));
  }
}

bool flag(const juce::XmlElement& e, const char* name, bool fallback) {
  if (!e.hasAttribute(name)) return fallback;
  return e.getStringAttribute(name).trim().equalsIgnoreCase("true");
}

// Everything that can sit on the panel. <control>, <labeled-knob> and
// <labeled-slider> are the same widget with different defaults; the look comes
// from `style` and from a filmstrip skin when one is given.
void readControl(const juce::XmlElement& e, const juce::String& tab, UiSpec& ui) {
  const auto tag = e.getTagName();
  UiControl c;
  c.tab = tab;
  c.x = e.getIntAttribute("x", 0);
  c.y = e.getIntAttribute("y", 0);
  c.w = e.getIntAttribute("width", 0);
  c.h = e.getIntAttribute("height", 0);
  c.textSize = e.getIntAttribute("textSize", 0);
  c.tags = e.getStringAttribute("tags");
  c.labeled = tag == "labeled-knob" || tag == "labeled-slider";
  c.uiIndex = (int)ui.controls.size();
  c.visible = flag(e, "visible", true);
  c.opacity = e.getDoubleAttribute("opacity", 1.0);
  c.align = e.getStringAttribute("hAlign");

  auto colour = [&](const char* a, const char* b = nullptr) {
    auto v = cssColour(e.getStringAttribute(a));
    return v.isEmpty() && b ? cssColour(e.getStringAttribute(b)) : v;
  };
  c.fg = colour("trackForegroundColor", "trackForegroundcolor");
  c.bg = colour("trackBackgroundColor");
  c.textColor = colour("textColor");

  c.minValue = e.getDoubleAttribute("minValue", 0.0);
  c.maxValue = e.getDoubleAttribute("maxValue", 1.0);
  c.value = e.getDoubleAttribute("value", c.minValue);
  c.hasDefault = e.hasAttribute("defaultValue");
  c.defaultValue = e.getDoubleAttribute("defaultValue", c.value);
  c.valueType = e.getStringAttribute("type");
  c.style = e.getStringAttribute("style");
  c.showLabel = flag(e, "showLabel", true);
  c.text = e.getStringAttribute("label");
  c.dragPixels = e.getDoubleAttribute("mouseDragSensitivity", 0.0);

  if (tag == "labeled-knob" || tag == "knob" || tag == "control" ||
      tag == "labeled-slider" || tag == "slider") {
    const auto skin = e.getStringAttribute("customSkinImage");
    if (skin.isNotEmpty()) {
      c.type = "skin";
      c.source = skin;
      c.frames = juce::jmax(1, e.getIntAttribute("customSkinNumFrames", 1));
      c.vertical = !e.getStringAttribute("customSkinImageOrientation").equalsIgnoreCase("horizontal");
      c.dragVertical = !c.style.containsIgnoreCase("horizontal_drag");
      if (c.w <= 0) c.w = 64;
      if (c.h <= 0) c.h = 64;
    } else if (c.style.startsWithIgnoreCase("linear") || tag == "labeled-slider" || tag == "slider") {
      c.type = "bar";
      c.vertical = c.style.containsIgnoreCase("vertical") ||
                   (c.style.isEmpty() && c.h > c.w && c.w > 0);
      if (c.w <= 0) c.w = c.vertical ? 24 : 160;
      if (c.h <= 0) c.h = c.vertical ? 120 : 24;
    } else {
      c.type = "knob";
      c.dragVertical = !c.style.containsIgnoreCase("horizontal");
      if (c.w <= 0) c.w = 100;
      if (c.h <= 0) c.h = c.w;
    }
  } else if (tag == "button") {
    c.type = "button";
    if (c.w <= 0) c.w = 80;
    if (c.h <= 0) c.h = 30;
    c.text = e.getStringAttribute("text", c.text);
    for (auto* st = e.getFirstChildElement(); st; st = st->getNextElement()) {
      if (!st->hasTagName("state")) continue;
      UiState state { st->getStringAttribute("name"), st->getStringAttribute("mainImage"),
                      st->getStringAttribute("clickImage"), st->getStringAttribute("hoverImage"), {} };
      readBindings(*st, state.bindings);
      c.states.push_back(std::move(state));
    }
  } else if (tag == "menu") {
    c.type = "menu";
    c.placeholder = e.getStringAttribute("placeholderText");
    if (c.w <= 0) c.w = 120;
    if (c.h <= 0) c.h = 30;
    for (auto* o = e.getFirstChildElement(); o; o = o->getNextElement())
      if (o->hasTagName("option")) {
        c.options.add(o->getStringAttribute("name"));
        c.optionBindings.emplace_back();
        readBindings(*o, c.optionBindings.back());
      }
  } else if (tag == "label") {
    c.type = "label";
    c.text = e.getStringAttribute("text");
    if (c.w <= 0) c.w = 200;
    if (c.h <= 0) c.h = 24;
  } else if (tag == "image") {
    c.type = "image";
    c.source = e.getStringAttribute("path");
    c.stretch = e.getStringAttribute("aspectRatioMode", "stretch").equalsIgnoreCase("stretch");
    if (c.w <= 0) c.w = 100;
    if (c.h <= 0) c.h = 100;
  } else if (tag == "multiFrameImage") {
    c.type = "strip";
    c.source = e.getStringAttribute("path");
    c.frames = juce::jmax(1, e.getIntAttribute("numFrames", 1));
    c.vertical = !e.getStringAttribute("sourceFormat").containsIgnoreCase("horizontal");
    c.frameRate = e.getStringAttribute("playbackMode").containsIgnoreCase("loop")
                    ? e.getDoubleAttribute("frameRate", 0.0) : 0.0;
    if (c.w <= 0) c.w = 100;
    if (c.h <= 0) c.h = 100;
  } else {
    return;
  }

  readBindings(e, c.bindings);
  ui.controls.push_back(std::move(c));
}

void readKeyboard(const juce::XmlElement& e, UiSpec& ui) {
  ui.hasKeyboard = true;
  ui.keyRanges.clear();
  for (auto* colour : e.getChildWithTagNameIterator("color")) {
    UiSpec::KeyRange r;
    r.lo = juce::jlimit(0, 127, colour->getIntAttribute("loNote", 0));
    r.hi = juce::jlimit(r.lo, 127, colour->getIntAttribute("hiNote", 127));
    r.colour = cssColour(colour->getStringAttribute("color"));
    if (r.colour.isNotEmpty()) ui.keyRanges.push_back(r);
  }
}

void readUi(const juce::XmlElement& ui, UiSpec& out) {
  out.valid = true;
  out.width = juce::jmax(240, ui.getIntAttribute("width", 812));
  out.height = juce::jmax(120, ui.getIntAttribute("height", 375));
  out.bgColor = cssColour(ui.getStringAttribute("bgColor"));
  out.bgImage = ui.getStringAttribute("bgImage");
  if (out.bgImage.isEmpty()) out.bgImage = ui.getStringAttribute("backgroundImage");
  out.bgTopLeft = ui.getStringAttribute("bgMode").equalsIgnoreCase("top_left");

  std::function<void(const juce::XmlElement&, const juce::String&, int)> walk =
    [&](const juce::XmlElement& parent, const juce::String& tab, int depth) {
      if (depth > 3) return;
      for (auto* child = parent.getFirstChildElement(); child; child = child->getNextElement()) {
        const auto tag = child->getTagName();
        if (tag == "tab") {
          auto name = child->getStringAttribute("name");
          if (name.isEmpty()) name = "main";
          out.tabs.addIfNotAlreadyThere(name);
          walk(*child, name, depth + 1);
        } else if (tag == "keyboard") {
          readKeyboard(*child, out);
        } else if (tag == "ui" || tag == "controls" || tag == "group") {
          walk(*child, tab, depth + 1);
        } else {
          readControl(*child, tab, out);
        }
      }
    };
  walk(ui, out.tabs.isEmpty() ? juce::String("main") : out.tabs[0], 0);
}

juce::String addWarning(juce::StringArray& warnings, const juce::String& text) {
  if (warnings.size() < 32 && !warnings.contains(text)) warnings.add(text);
  return {};
}

} // namespace

UiSpec Dspreset::readPanel(const juce::File& preset) {
  UiSpec out;
  if (auto xml = juce::XmlDocument::parse(preset))
    if (auto* ui = xml->getChildByName("ui")) readUi(*ui, out);
  return out;
}

Inspection Dspreset::inspect(const juce::File& preset, juce::String& error) {
  Inspection out;
  auto xml = juce::XmlDocument::parse(preset);
  if (xml == nullptr || !xml->hasTagName("DecentSampler")) {
    error = "not a Decent Sampler preset";
    return out;
  }
  auto* groups = xml->getChildByName("groups");
  if (groups == nullptr) {
    error = "no <groups> element";
    return out;
  }

  Ctx base;
  readGroups(base, *groups);
  out.env = base.env;
  out.volume = base.volume;
  out.background = findBackground(*xml);

  int low = 128, high = -1;
  for (auto* g = groups->getFirstChildElement(); g; g = g->getNextElement()) {
    if (!g->hasTagName("group")) continue;
    ++out.groups;
    Ctx ctx = base;
    readGroup(ctx, *g);
    for (auto* s = g->getFirstChildElement(); s; s = s->getNextElement()) {
      if (!s->hasTagName("sample")) continue;
      ++out.samples;
      const int lo = s->getIntAttribute("loNote", 0);
      const int hi = s->getIntAttribute("hiNote", 127);
      low = juce::jmin(low, lo);
      high = juce::jmax(high, hi);
    }
  }
  out.lowNote = low == 128 ? 0 : low;
  out.highNote = high;
  if (out.samples == 0) error = "no samples";
  return out;
}

std::shared_ptr<Program> Dspreset::parse(const juce::File& preset, SampleLoader& loader, juce::String& error) {
  auto xml = juce::XmlDocument::parse(preset);
  if (xml == nullptr || !xml->hasTagName("DecentSampler")) {
    error = "not a Decent Sampler preset";
    return {};
  }
  auto* groups = xml->getChildByName("groups");
  if (groups == nullptr) {
    error = "no <groups> element";
    return {};
  }

  auto program = std::make_shared<Program>();
  program->presetPath = preset.getFullPathName();
  program->rootDir = preset.getParentDirectory().getFullPathName();
  program->name = preset.getFileNameWithoutExtension();

  Ctx base;
  readGroups(base, *groups);
  program->env = base.env;
  program->volume = base.volume;

  auto readEffects = [](const juce::XmlElement& parent, std::vector<Effect>& into) {
    if (auto* list = parent.getChildByName("effects"))
      for (auto* fx = list->getFirstChildElement(); fx; fx = fx->getNextElement()) {
        if (!fx->hasTagName("effect")) continue;
        Effect out;
        out.type = fx->getStringAttribute("type").toLowerCase();
        for (int i = 0; i < fx->getNumAttributes(); ++i)
          if (fx->getAttributeName(i) != "type")
            out.attrs.emplace_back(fx->getAttributeName(i), fx->getAttributeValue(i));
        into.push_back(std::move(out));
      }
  };
  readEffects(*xml, program->effects);

  // <modulators> (a filter envelope, a drift LFO) and <midi><velocity>
  // bindings steer group filters and tunings while a note plays.
  auto readTarget = [](const juce::XmlElement& b, ModTarget& t) {
    t.group = b.hasAttribute("groupIndex") ? b.getIntAttribute("groupIndex", -1) : b.getIntAttribute("position", -1);
    t.effect = b.hasAttribute("effectIndex") ? b.getIntAttribute("effectIndex", -1)
             : b.hasAttribute("position") ? b.getIntAttribute("position", -1) : -1;
    t.parameter = b.getStringAttribute("parameter").toUpperCase();
    for (auto& pair : juce::StringArray::fromTokens(b.getStringAttribute("translationTable"), ";", "")) {
      const auto xy = juce::StringArray::fromTokens(pair, ",", "");
      if (xy.size() == 2) t.table.emplace_back((float)xy[0].getDoubleValue(), (float)xy[1].getDoubleValue());
    }
    std::sort(t.table.begin(), t.table.end());
    t.hasRange = b.hasAttribute("translationOutputMin") || b.hasAttribute("translationOutputMax");
    t.outMin = b.getDoubleAttribute("translationOutputMin", 0.0);
    t.outMax = b.getDoubleAttribute("translationOutputMax", 1.0);
  };
  if (auto* mods = xml->getChildByName("modulators"))
    for (auto* m = mods->getFirstChildElement(); m; m = m->getNextElement()) {
      const bool isEnv = m->hasTagName("envelope");
      if (!isEnv && !m->hasTagName("lfo")) continue;
      Modulator mod;
      mod.envelope = isEnv;
      mod.amount = m->getDoubleAttribute("modAmount", 1.0);
      mod.lfoHz = m->getDoubleAttribute("frequency", 1.0);
      mod.env.attack = m->getDoubleAttribute("attack", 0.0);
      mod.env.hold = 0.0;
      mod.env.decay = m->getDoubleAttribute("decay", 0.3);
      mod.env.sustain = m->getDoubleAttribute("sustain", 1.0);
      mod.env.release = m->getDoubleAttribute("release", 0.1);
      for (auto* b = m->getFirstChildElement(); b; b = b->getNextElement()) {
        if (!b->hasTagName("binding")) continue;
        ModTarget t;
        readTarget(*b, t);
        mod.targets.push_back(std::move(t));
      }
      program->modulators.push_back(std::move(mod));
    }
  if (auto* midi = xml->getChildByName("midi"))
    if (auto* vel = midi->getChildByName("velocity"))
      for (auto* b = vel->getFirstChildElement(); b; b = b->getNextElement()) {
        if (!b->hasTagName("binding")) continue;
        VelocityBinding v;
        readTarget(*b, v.target);
        v.amount = b->getDoubleAttribute("modAmount", 1.0);
        v.enabled = !b->hasAttribute("enabled") || b->getStringAttribute("enabled").trim().equalsIgnoreCase("1") ||
                    b->getStringAttribute("enabled").trim().equalsIgnoreCase("true");
        program->velocity.push_back(std::move(v));
      }

  std::vector<const juce::XmlElement*> groupList;
  for (auto* g = groups->getFirstChildElement(); g; g = g->getNextElement())
    if (g->hasTagName("group")) groupList.push_back(g);
  program->groups = (int)groupList.size();
  program->seqLen.assign((size_t)juce::jmax(1, program->groups), 1);
  program->groupOn.assign((size_t)juce::jmax(1, program->groups), 1);
  program->groupVolume.assign((size_t)juce::jmax(1, program->groups), 1.0);
  program->groupEffects.resize((size_t)juce::jmax(1, program->groups));

  auto tagIndex = [&](const juce::String& list, std::vector<int>& into) {
    for (auto& name : juce::StringArray::fromTokens(list, ",", "")) {
      const auto t = name.trim();
      if (t.isEmpty()) continue;
      program->tagNames.addIfNotAlreadyThere(t);
      const int id = program->tagNames.indexOf(t);
      if (std::find(into.begin(), into.end(), id) == into.end()) into.push_back(id);
    }
  };

  std::set<const Sample*> stored;

  for (int gi = 0; gi < (int)groupList.size(); ++gi) {
    const auto& group = *groupList[(size_t)gi];
    Ctx ctx = base;
    readGroup(ctx, group);

    // A group that starts switched off is still loaded: a button or menu on
    // the panel usually turns it on.
    program->groupOn[(size_t)gi] = group.getBoolAttribute("enabled", true) ? 1 : 0;
    {
      double own = 1.0;
      if (getGain(group, "volume", "groupVolume", own)) program->groupVolume[(size_t)gi] = own;
    }
    readEffects(group, program->groupEffects[(size_t)gi]);
    std::vector<int> groupTags;
    tagIndex(group.getStringAttribute("tags"), groupTags);
    for (auto* s = group.getFirstChildElement(); s; s = s->getNextElement()) {
      if (!s->hasTagName("sample")) {
        if (s->hasTagName("oscillator"))
          addWarning(program->warnings, "oscillator groups are not supported yet");
        continue;
      }

      Ctx z = ctx;
      readSample(z, *s);

      const auto rel = s->getStringAttribute("path");
      const auto file = SampleLoader::resolve(preset.getParentDirectory(), rel);
      auto sample = loader.load(file);
      if (sample == nullptr) {
        addWarning(program->warnings, "missing sample: " + rel);
        continue;
      }

      Zone zone;
      zone.sample = sample;
      zone.path = rel;
      zone.group = gi;
      zone.tags = groupTags;
      tagIndex(s->getStringAttribute("tags"), zone.tags);
      zone.rootNote = juce::jlimit(0, 127, s->getIntAttribute("rootNote", 60));
      zone.loNote = juce::jlimit(0, 127, s->getIntAttribute("loNote", 0));
      zone.hiNote = juce::jlimit(0, 127, s->getIntAttribute("hiNote", 127));
      zone.loVel = juce::jlimit(0, 127, s->getIntAttribute("loVel", 0));
      zone.hiVel = juce::jlimit(0, 127, s->getIntAttribute("hiVel", 127));
      if (zone.loNote > zone.hiNote) std::swap(zone.loNote, zone.hiNote);
      if (zone.loVel > zone.hiVel) std::swap(zone.loVel, zone.hiVel);

      zone.startFrame = juce::jmax(0.0, s->getDoubleAttribute("start", 0.0));
      zone.endFrame = s->hasAttribute("end") ? s->getDoubleAttribute("end", -1.0) : -1.0;
      zone.tuning = z.tuning;
      zone.tuningIgnoresKeyTrack = z.ignoresKeyTrack;
      zone.pitchKeyTrack = juce::jlimit(0.0, 1.0, z.keyTrack);
      zone.gain = z.volume;
      zone.pan = juce::jlimit(-1.0, 1.0, (z.pan + z.panOffset) / 100.0);
      zone.ampVelTrack = juce::jlimit(0.0, 1.0, z.ampVel);
      zone.glideTime = juce::jmax(0.0, z.glide);
      zone.delaySeconds = z.delayInSamples ? z.delay / sample->sourceRate : juce::jmax(0.0, z.delay);
      zone.ampEnvEnabled = z.ampEnvEnabled;
      zone.env = z.env;
      zone.trigger = z.trigger;
      zone.seqMode = z.seqMode;
      zone.seqPosition = juce::jmax(1, s->getIntAttribute("seqPosition", z.seqPosition));
      zone.seqLength = z.seqLength;
      zone.loopCrossfade = juce::jmax(0, (int)std::lround(z.crossfade));
      zone.loopDuringRelease = z.loopDuringRelease;

      if (z.trigger != "attack" && z.trigger != "first" && z.trigger != "legato")
        addWarning(program->warnings, "trigger=\"" + z.trigger + "\" not supported yet");

      if (s->hasAttribute("loopStart") || s->hasAttribute("loopEnd")) {
        zone.loopStart = juce::jmax(0.0, s->getDoubleAttribute("loopStart", 0.0));
        zone.loopEnd = s->getDoubleAttribute("loopEnd", (double)sample->frames);
        zone.loopEnabled = s->hasAttribute("loopEnabled")
          ? s->getBoolAttribute("loopEnabled", true)
          : true;
      } else if (sample->hasLoop) {
        zone.loopStart = (double)sample->loopStart;
        zone.loopEnd = (double)sample->loopEnd;
        zone.loopEnabled = true;
      }
      if (zone.loopEnabled && zone.loopEnd <= zone.loopStart + 2.0) zone.loopEnabled = false;

      if (zone.seqLength > 0)
        program->seqLen[(size_t)gi] = juce::jmax(program->seqLen[(size_t)gi], zone.seqLength);
      program->seqLen[(size_t)gi] = juce::jmax(program->seqLen[(size_t)gi], zone.seqPosition);

      if (stored.insert(sample.get()).second) program->samples.push_back(sample);
      program->zones.push_back(std::move(zone));
    }
  }

  if (auto* ui = xml->getChildByName("ui"))
    readUi(*ui, program->ui);

  program->slotTable.assign((size_t)juce::jmax(1, program->groups), {});
  auto keepTable = [&](const ModTarget& t) {
    if (t.parameter != "FX_FILTER_FREQUENCY" || t.table.empty()) return;
    if (t.group < 0 || t.group >= (int)program->slotTable.size() || t.effect < 0 || t.effect >= 4) return;
    auto& slot = program->slotTable[(size_t)t.group][(size_t)t.effect];
    if (slot.empty()) slot = t.table;
  };
  for (auto& m : program->modulators) for (auto& t : m.targets) keepTable(t);
  for (auto& v : program->velocity) keepTable(v.target);

  // A low-pass left in a group at a few dozen Hz that nothing on the panel
  // controls (Decent Sampler's own Basic Piano has one) would mute the group,
  // which Decent Sampler does not do. Treat those as inert.
  {
    std::set<std::pair<int, int>> controlled;
    auto note = [&](const UiBinding& b) {
      if (b.level == "group" && b.type == "effect" && b.groupIndex >= 0 && b.effectIndex >= 0)
        controlled.insert({ b.groupIndex, b.effectIndex });
    };
    for (auto& c : program->ui.controls) {
      for (auto& b : c.bindings) note(b);
      for (auto& entry : c.optionBindings) for (auto& b : entry) note(b);
      for (auto& st : c.states) for (auto& b : st.bindings) note(b);
    }
    for (int g = 0; g < (int)program->groupEffects.size(); ++g)
      for (int i = 0; i < (int)program->groupEffects[(size_t)g].size(); ++i) {
        auto& fx = program->groupEffects[(size_t)g][(size_t)i];
        if (controlled.count({ g, i }) == 0 && fx.type.startsWith("lowpass") && fx.number("frequency", 22000.0) < 100.0)
          for (auto& a : fx.attrs) if (a.first == "frequency") a.second = "22000";
      }
  }

  if (program->zones.empty()) {
    error = program->warnings.isEmpty() ? juce::String("no playable samples")
                                        : juce::String("no playable samples: ") + program->warnings[0];
    return {};
  }
  return program;
}

} // namespace trove
