#include "PluginEditor.h"
#include "BinaryData.h"
#include "format/SampleLoader.h"
#include "format/UiJson.h"

using namespace trove;

namespace {

const char* mimeFor(const juce::String& file) {
  const auto ext = file.fromLastOccurrenceOf(".", false, false).toLowerCase();
  if (ext == "html") return "text/html";
  if (ext == "js") return "text/javascript";
  if (ext == "css") return "text/css";
  if (ext == "svg") return "image/svg+xml";
  if (ext == "png") return "image/png";
  if (ext == "jpg" || ext == "jpeg") return "image/jpeg";
  if (ext == "webp") return "image/webp";
  if (ext == "json") return "application/json";
  if (ext == "woff2") return "font/woff2";
  if (ext == "woff") return "font/woff";
  return "application/octet-stream";
}

std::optional<juce::WebBrowserComponent::Resource> bytesOf(const void* data, size_t size,
                                                           const juce::String& mime) {
  std::vector<std::byte> bytes(size);
  std::memcpy(bytes.data(), data, size);
  return juce::WebBrowserComponent::Resource { std::move(bytes), mime };
}

void webkitEnv() {
 #if JUCE_LINUX
  setenv("WEBKIT_DMABUF_RENDERER_FORCE_SHM", "1", 0);
 #endif
}

juce::var paramDefs() {
  juce::Array<juce::var> defs;
  for (int i = 0; i < NUM_PARAMS; ++i) {
    auto* o = new juce::DynamicObject();
    o->setProperty("id", PARAMS[i].id);
    o->setProperty("name", PARAMS[i].name);
    o->setProperty("min", PARAMS[i].min);
    o->setProperty("max", PARAMS[i].max);
    o->setProperty("def", PARAMS[i].def);
    o->setProperty("skew", PARAMS[i].skew);
    o->setProperty("unit", PARAMS[i].unit);
    defs.add(juce::var(o));
  }
  return juce::var(defs);
}

} // namespace

TroveEditor::TroveEditor(TroveProcessor& p)
  : AudioProcessorEditor(p), proc(p),
    web((webkitEnv(), juce::WebBrowserComponent::Options {})
          .withBackend(juce::WebBrowserComponent::Options::Backend::webview2)
          .withNativeIntegrationEnabled()
          .withKeepPageLoadedWhenBrowserIsHidden()
          .withResourceProvider([this](const juce::String& url) { return resource(url); })
          .withEventListener("trove", [this](const juce::var& msg) { onMessage(msg); })) {
  for (int i = 0; i < NUM_PARAMS; ++i) ids[PARAMS[i].id] = i;
  addAndMakeVisible(web);

  setResizable(true, true);
  setResizeLimits(UI_W / 2, UI_H / 2, UI_W * 2, UI_H * 2);

  double s = 1.0;
  if (auto* d = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
    s = std::min({ 1.0, d->userArea.getWidth() * 0.92 / UI_W, d->userArea.getHeight() * 0.86 / UI_H });
  setSize(juce::roundToInt(UI_W * s), juce::roundToInt(UI_H * s));
  sized = true;

  web.goToURL(juce::WebBrowserComponent::getResourceProviderRoot());
  startTimerHz(12);
}

void TroveEditor::paint(juce::Graphics& g) { g.fillAll(juce::Colour(0xff0b0e0c)); }

void TroveEditor::resized() { web.setBounds(getLocalBounds()); }

std::optional<juce::WebBrowserComponent::Resource> TroveEditor::resource(const juce::String& url) {
  auto path = url.upToFirstOccurrenceOf("?", false, false);

  if (path.startsWith("/ui/")) {
    auto rel = juce::URL::removeEscapeChars(path.fromFirstOccurrenceOf("/ui/", false, false));
    if (rel.isEmpty() || rel.contains("..") || rel.startsWithChar('/')) return std::nullopt;
    auto dir = loadedDir;
    for (int up = 0; up < 3 && dir.isDirectory(); ++up) {
      const auto file = SampleLoader::resolve(dir, rel);
      if (file.existsAsFile() && file.isAChildOf(dir)) {
        juce::MemoryBlock mb;
        if (file.loadFileAsData(mb))
          return bytesOf(mb.getData(), mb.getSize(), mimeFor(file.getFileName()));
      }
      dir = dir.getParentDirectory();
    }
    return std::nullopt;
  }

  if (path.startsWith("/art/")) {
    const auto id = path.fromFirstOccurrenceOf("/art/", false, false)
                      .upToFirstOccurrenceOf(".", false, false);
    const auto file = proc.scanner().artFile(id);
    if (file.existsAsFile()) {
      juce::MemoryBlock mb;
      if (file.loadFileAsData(mb))
        return bytesOf(mb.getData(), mb.getSize(), "image/jpeg");
    }
    return std::nullopt;
  }

  if (path.isEmpty() || path == "/") path = "index.html";
  const auto name = path.fromLastOccurrenceOf("/", false, false);
  for (int i = 0; i < BinaryData::namedResourceListSize; ++i) {
    if (name != BinaryData::originalFilenames[i]) continue;
    int size = 0;
    const char* data = BinaryData::getNamedResource(BinaryData::namedResourceList[i], size);
    if (data == nullptr) break;
    return bytesOf(data, (size_t)size, mimeFor(name));
  }
  return std::nullopt;
}

void TroveEditor::send(juce::DynamicObject::Ptr obj) {
  web.emitEventIfBrowserIsVisible("trove", juce::var(obj.get()));
}

void TroveEditor::sendHello() {
  for (int i = 0; i < NUM_PARAMS; ++i) shown[(size_t)i] = proc.paramValue(i);
  juce::DynamicObject::Ptr o = new juce::DynamicObject();
  o->setProperty("type", "hello");
  o->setProperty("params", paramDefs());
  juce::DynamicObject::Ptr values = new juce::DynamicObject();
  for (int i = 0; i < NUM_PARAMS; ++i) values->setProperty(PARAMS[i].id, shown[(size_t)i]);
  o->setProperty("values", juce::var(values.get()));
  send(o);
  seenState = proc.stateVersion.load();
  sendIndex();
}

void TroveEditor::sendIndex() {
  const auto libraries = proc.scanner().libraries();

  juce::Array<juce::var> libs;
  for (auto& lib : libraries) {
    juce::DynamicObject::Ptr o = new juce::DynamicObject();
    o->setProperty("id", lib.id);
    o->setProperty("name", lib.name);
    o->setProperty("kind", lib.kind);
    o->setProperty("source", lib.base);
    o->setProperty("tint", lib.tint);
    o->setProperty("error", lib.error);
    o->setProperty("count", (int)lib.presets.size());
    if (lib.hasArt) o->setProperty("art", "/art/" + lib.id + ".jpg");

    int zones = 0, low = 127, high = 0;
    juce::Array<juce::var> presets;
    for (auto& p : lib.presets) {
      juce::DynamicObject::Ptr po = new juce::DynamicObject();
      po->setProperty("id", p.id);
      po->setProperty("name", p.name);
      po->setProperty("preset", p.path);
      po->setProperty("zones", p.zones);
      po->setProperty("groups", p.groups);
      po->setProperty("low", p.low);
      po->setProperty("high", p.high);
      po->setProperty("ok", p.ok);
      po->setProperty("error", p.error);
      presets.add(juce::var(po.get()));
      if (p.ok) {
        zones += p.zones;
        low = juce::jmin(low, p.low);
        high = juce::jmax(high, p.high);
      }
    }
    o->setProperty("presets", presets);
    o->setProperty("zones", zones);
    o->setProperty("low", low == 127 ? 0 : low);
    o->setProperty("high", high);
    libs.add(juce::var(o.get()));
  }

  juce::Array<juce::var> roots;
  for (auto& r : proc.scanner().roots()) roots.add(r);

  juce::DynamicObject::Ptr o = new juce::DynamicObject();
  o->setProperty("type", "index");
  o->setProperty("libs", libs);
  o->setProperty("roots", roots);
  o->setProperty("busy", proc.scanner().busy());
  send(o);

  seenIndex = proc.scanner().version();
  seenBusy = proc.scanner().busy();
}

void TroveEditor::sendParams() {
  for (int i = 0; i < NUM_PARAMS; ++i) shown[(size_t)i] = proc.paramValue(i);
  juce::DynamicObject::Ptr values = new juce::DynamicObject();
  for (int i = 0; i < NUM_PARAMS; ++i) values->setProperty(PARAMS[i].id, shown[(size_t)i]);
  juce::DynamicObject::Ptr o = new juce::DynamicObject();
  o->setProperty("type", "params");
  o->setProperty("values", juce::var(values.get()));
  send(o);
  seenState = proc.stateVersion.load();
}

void TroveEditor::sendLoaded(const LoadedInfo& info) {
  if (info.preset.isNotEmpty()) loadedDir = juce::File(info.preset).getParentDirectory();

  juce::DynamicObject::Ptr o = new juce::DynamicObject();
  o->setProperty("type", "loaded");
  o->setProperty("id", info.id);
  o->setProperty("name", info.name);
  o->setProperty("preset", info.preset);
  o->setProperty("dir", loadedDir.getFullPathName());
  if (info.ui.valid) o->setProperty("ui", trove::uiJson(info.ui));
  juce::DynamicObject::Ptr values = new juce::DynamicObject();
  for (int i = 0; i < NUM_PARAMS; ++i) values->setProperty(PARAMS[i].id, proc.paramValue(i));
  o->setProperty("values", juce::var(values.get()));
  o->setProperty("zones", info.zones);
  o->setProperty("groups", info.groups);
  o->setProperty("attack", info.attack);
  o->setProperty("decay", info.decay);
  o->setProperty("sustain", info.sustain);
  o->setProperty("release", info.release);
  o->setProperty("gainDb", info.gainDb);
  o->setProperty("error", info.error);
  send(o);
  sendParams();
}

void TroveEditor::addRootChooser() {
  chooser = std::make_unique<juce::FileChooser>(
    "Add a folder that holds your sample libraries",
    juce::File::getSpecialLocation(juce::File::userHomeDirectory), "*");
  chooser->launchAsync(juce::FileBrowserComponent::openMode
                         | juce::FileBrowserComponent::canSelectDirectories,
    [this](const juce::FileChooser& fc) {
      const auto result = fc.getResult();
      if (result.isDirectory()) {
        proc.scanner().addRoot(result);
        sendIndex();
      }
    });
}

void TroveEditor::onMessage(const juce::var& msg) {
  const auto type = msg["type"].toString();

  if (type == "ready") {
    ready = true;
    sendHello();
  } else if (type == "scan") {
    proc.scanner().startScan();
    sendIndex();
  } else if (type == "addRoot") {
    addRootChooser();
  } else if (type == "load") {
    const auto id = msg["id"].toString();
    for (auto& lib : proc.scanner().libraries()) {
      for (auto& p : lib.presets) {
        if (p.id != id) continue;
        if (!p.ok) {
          juce::DynamicObject::Ptr o = new juce::DynamicObject();
          o->setProperty("type", "loaded");
          o->setProperty("id", id);
          o->setProperty("name", p.name);
          o->setProperty("error", p.error);
          send(o);
        } else {
          proc.requestLibrary(p.id, juce::File(p.path), p.name);
        }
        return;
      }
    }
  } else if (type == "param") {
    const auto it = ids.find(msg["id"].toString().toStdString());
    if (it == ids.end()) return;
    proc.setParamFromUi(it->second, (double)msg["value"]);
    shown[(size_t)it->second] = proc.paramValue(it->second);
  } else if (type == "noteOn") {
    proc.uiNoteOn((int)msg["note"], (float)(double)msg["velocity"]);
  } else if (type == "noteOff") {
    proc.uiNoteOff((int)msg["note"]);
  } else if (type == "sustain") {
    proc.uiSustain((bool)msg["down"]);
  } else if (type == "group") {
    proc.uiGroup((int)msg["group"], (bool)msg["enabled"]);
  } else if (type == "bind") {
    applyBinding(msg);
  } else if (type == "panic") {
    proc.uiPanic();
  }
}

// A panel control, already turned into a plain target by the page. Everything
// here is thread-safe on the engine side, so it runs straight from the message.
void TroveEditor::applyBinding(const juce::var& msg) {
  auto& engine = proc.engineRef();
  const auto kind = msg["kind"].toString();
  const double value = (double)msg["value"];
  const int group = (int)msg["group"];

  if (kind == "groupOn") engine.setGroupEnabled(group, value >= 0.5);
  else if (kind == "groupVolume") engine.setGroupVolume(group, value);
  else if (kind == "groupPan") engine.setGroupPan(group, value);
  else if (kind == "tagVolume") engine.setTagVolume(msg["tag"].toString(), value);
  else if (kind == "tuning") engine.setGlobalTuning(value);
  else if (kind == "modParam") engine.setModParam((int)msg["index"], msg["param"].toString(), value);
  else if (kind == "velEnabled") engine.setVelocityEnabled((int)msg["index"], value >= 0.5);
  else if (kind == "groupFxPos") engine.setGroupFxPosition(group, (int)msg["index"], value);
  else if (kind == "fx" || kind == "groupFx") {
    FxParam param;
    if (!fxParamFromName(msg["param"].toString(), param)) return;
    if (kind == "fx") engine.setFx((int)msg["index"], param, value);
    else engine.setGroupFx(group, (int)msg["index"], param, value);
  } else if (kind == "fxIr") {
    const auto file = SampleLoader::resolve(loadedDir, msg["file"].toString());
    if (file.existsAsFile()) engine.loadImpulse((int)msg["index"], file);
  }
}

void TroveEditor::timerCallback() {
  if (!ready) return;

  if (proc.scanner().version() != seenIndex || proc.scanner().busy() != seenBusy)
    sendIndex();

  LoadedInfo info;
  if (proc.takeLoaded(info)) sendLoaded(info);

  if (proc.stateVersion.load() != seenState) sendParams();

  std::array<juce::uint32, 4> held {};
  for (size_t i = 0; i < held.size(); ++i) held[i] = proc.heldNotes[i].load();
  if (held != seenHeld) {
    seenHeld = held;
    juce::Array<juce::var> notes;
    for (int n = 0; n < 128; ++n)
      if (held[(size_t)(n >> 5)] & (1u << (n & 31))) notes.add(n);
    juce::DynamicObject::Ptr o = new juce::DynamicObject();
    o->setProperty("type", "held");
    o->setProperty("notes", notes);
    send(o);
  }

  if (++tick % 3 == 0) {
    juce::DynamicObject::Ptr o = new juce::DynamicObject();
    o->setProperty("type", "stats");
    o->setProperty("voices", proc.engineRef().activeVoices());
    o->setProperty("peak", proc.engineRef().peak());
    send(o);
  }
}
