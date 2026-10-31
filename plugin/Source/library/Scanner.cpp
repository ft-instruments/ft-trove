#include "Scanner.h"
#include "../format/Dspreset.h"
#include "../format/SampleLoader.h"
#include "Artwork.h"
#include <algorithm>
#include <map>
#include <set>

namespace trove {
namespace {

juce::String idFor(const juce::String& text) {
  return juce::String::toHexString((juce::int64)juce::String(text).hashCode64()).paddedLeft('0', 16);
}

bool isPresetFile(const juce::File& f) {
  return f.getFileExtension().equalsIgnoreCase(".dspreset");
}

bool isArchiveFile(const juce::File& f) {
  return f.getFileExtension().equalsIgnoreCase(".dslibrary");
}

bool isJunk(const juce::File& f) {
  const auto name = f.getFileName();
  return name.startsWith(".") || name.equalsIgnoreCase("__MACOSX")
      || name.equalsIgnoreCase("node_modules");
}

bool isGenericDir(const juce::String& name) {
  static const char* names[] = {
    "presets", "preset", "instruments", "instrument", "patches", "dspresets",
    "samples", "library", "libraries", "resources", "data", "files"
  };
  for (auto* n : names)
    if (name.equalsIgnoreCase(n)) return true;
  return false;
}

bool isGenericStem(const juce::String& name) {
  static const char* names[] = {
    "default", "main", "preset", "instrument", "patch", "library", "the instrument", "untitled"
  };
  for (auto* n : names)
    if (name.equalsIgnoreCase(n)) return true;
  return false;
}

int naturalCompare(const juce::String& a, const juce::String& b) {
  size_t i = 0, j = 0;
  while (i < (size_t)a.length() && j < (size_t)b.length()) {
    const auto ca = a[i], cb = b[j];
    if (juce::CharacterFunctions::isDigit(ca) && juce::CharacterFunctions::isDigit(cb)) {
      size_t ei = i, ej = j;
      while (ei < (size_t)a.length() && juce::CharacterFunctions::isDigit(a[ei])) ++ei;
      while (ej < (size_t)b.length() && juce::CharacterFunctions::isDigit(b[ej])) ++ej;
      const auto na = a.substring((int)i, (int)ei).getLargeIntValue();
      const auto nb = b.substring((int)j, (int)ej).getLargeIntValue();
      if (na != nb) return na < nb ? -1 : 1;
      i = ei;
      j = ej;
      continue;
    }
    const auto la = juce::CharacterFunctions::toLowerCase(ca);
    const auto lb = juce::CharacterFunctions::toLowerCase(cb);
    if (la != lb) return la < lb ? -1 : 1;
    ++i;
    ++j;
  }
  if (i < (size_t)a.length()) return 1;
  if (j < (size_t)b.length()) return -1;
  return 0;
}

juce::String presetName(const juce::File& preset) {
  const auto stem = preset.getFileNameWithoutExtension().trim();
  const auto parent = preset.getParentDirectory().getFileName();
  if (stem.isEmpty() || isGenericStem(stem)) return parent;
  if (stem.equalsIgnoreCase(parent) || isGenericDir(parent)) return parent;
  return stem;
}

juce::String libraryName(const juce::File& base, const juce::String& kind) {
  if (kind == "folder") return base.getFileName();
  return base.getFileNameWithoutExtension();
}

void collect(const juce::File& dir, int depth, const juce::File& root,
             std::vector<std::pair<juce::File, juce::File>>& presets,
             std::vector<juce::File>& archives) {
  if (depth > 6 || !dir.isDirectory()) return;
  for (auto& child : dir.findChildFiles(juce::File::findFilesAndDirectories, false, "*")) {
    if (isJunk(child)) continue;
    if (child.isDirectory()) {
      collect(child, depth + 1, root, presets, archives);
    } else if (isPresetFile(child)) {
      presets.emplace_back(child, root);
    } else if (isArchiveFile(child)) {
      archives.push_back(child);
    }
  }
}

juce::File libraryBaseFor(const juce::File& preset, const juce::File& root) {
  for (auto dir = preset.getParentDirectory();; dir = dir.getParentDirectory()) {
    if (dir.getFileExtension().equalsIgnoreCase(".dsbundle")) return dir;
    if (dir.getFullPathName() == root.getFullPathName()) break;
    if (dir.getParentDirectory() == dir) break;
  }

  const auto rel = preset.getRelativePathFrom(root);
  const auto parts = juce::StringArray::fromTokens(rel, "/", "");
  if (parts.size() >= 2) return root.getChildFile(parts[0]);
  return root;
}

juce::var entryToVar(const Scanner::Entry& e) {
  auto* o = new juce::DynamicObject();
  o->setProperty("id", e.id);
  o->setProperty("name", e.name);
  o->setProperty("path", e.path);
  o->setProperty("tint", e.tint);
  o->setProperty("mtime", (juce::int64)e.mtime);
  o->setProperty("zones", e.zones);
  o->setProperty("groups", e.groups);
  o->setProperty("low", e.low);
  o->setProperty("high", e.high);
  o->setProperty("ok", e.ok);
  o->setProperty("error", e.error);
  return juce::var(o);
}

Scanner::Entry varToEntry(const juce::var& v) {
  Scanner::Entry e;
  e.id = v["id"].toString();
  e.name = v["name"].toString();
  e.path = v["path"].toString();
  e.tint = v["tint"].toString();
  e.mtime = (juce::int64)v["mtime"];
  e.zones = (int)v["zones"];
  e.groups = (int)v["groups"];
  e.low = (int)v["low"];
  e.high = (int)v["high"];
  e.ok = (bool)v["ok"];
  e.error = v["error"].toString();
  return e;
}

juce::Colour tintFromName(const juce::String& name) {
  auto rnd = juce::Random((juce::int64)name.hashCode64());
  return juce::Colour::fromHSV((float)rnd.nextDouble(), 0.28f, 0.42f, 1.0f);
}

} // namespace

juce::File Scanner::appDir() {
  auto base = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory);
 #if JUCE_MAC
  base = base.getChildFile("Application Support");
 #endif
  auto dir = base.getChildFile("Trove");
  dir.createDirectory();
  return dir;
}

Scanner::Scanner() : juce::Thread("trove scanner") {
  rootList = loadRoots();
}

Scanner::~Scanner() {
  signalThreadShouldExit();
  wake.signal();
  stopThread(3000);
}

juce::StringArray Scanner::loadRoots() const {
  juce::StringArray roots;
  const auto settings = appDir().getChildFile("settings.json");
  if (settings.existsAsFile()) {
    const auto v = juce::JSON::parse(settings);
    if (auto* arr = v["roots"].getArray())
      for (auto& r : *arr)
        if (juce::File(r.toString()).isDirectory()) roots.add(r.toString());
  }

  if (roots.isEmpty()) {
    auto docs = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                  .getChildFile("Samplers");
    auto local = appDir().getChildFile("Libraries");
    docs.createDirectory();
    local.createDirectory();
    roots.add(docs.getFullPathName());
    roots.add(local.getFullPathName());
    saveRoots(roots);
  }
  return roots;
}

void Scanner::saveRoots(const juce::StringArray& roots) const {
  auto* o = new juce::DynamicObject();
  juce::Array<juce::var> arr;
  for (auto& r : roots) arr.add(r);
  o->setProperty("roots", arr);
  appDir().getChildFile("settings.json").replaceWithText(juce::JSON::toString(juce::var(o)));
}

juce::StringArray Scanner::roots() const {
  const juce::ScopedLock sl(lock);
  return rootList;
}

void Scanner::addRoot(const juce::File& root) {
  if (!root.isDirectory()) return;
  {
    const juce::ScopedLock sl(lock);
    if (rootList.contains(root.getFullPathName())) return;
    rootList.add(root.getFullPathName());
    saveRoots(rootList);
  }
  startScan();
}

void Scanner::startScan() {
  again = true;
  if (!isThreadRunning()) startThread(juce::Thread::Priority::low);
  wake.signal();
}

void Scanner::run() {
  while (!threadShouldExit()) {
    wake.wait(500);
    if (threadShouldExit()) break;
    if (!again.exchange(false)) continue;
    do {
      scan();
    } while (again.exchange(false) && !threadShouldExit());
  }
}

std::vector<Scanner::Library> Scanner::libraries() const {
  const juce::ScopedLock sl(lock);
  return list;
}

juce::File Scanner::artFile(const juce::String& id) const {
  const juce::ScopedLock sl(lock);
  const auto it = artById.find(id);
  return it == artById.end() ? juce::File() : it->second;
}

Scanner::Entry Scanner::describe(const juce::File& preset) const {
  Entry e;
  e.path = preset.getFullPathName();
  e.mtime = preset.getLastModificationTime().toMilliseconds();
  e.id = idFor(e.path + "|" + juce::String(e.mtime));
  e.name = presetName(preset);

  juce::String error;
  const auto info = Dspreset::inspect(preset, error);
  e.zones = info.samples;
  e.groups = info.groups;
  e.low = info.lowNote;
  e.high = info.highNote;
  e.ok = error.isEmpty();
  e.error = error;

  const auto thumb = appDir().getChildFile("thumbnails").getChildFile(e.id + ".jpg");
  const auto source = Artwork::findSource(preset.getParentDirectory(), info.background);
  if (source.existsAsFile() && Artwork::makeThumb(source, thumb, e.tint)) {
    e.art = thumb;
    e.hasArt = true;
  } else {
    e.tint = tintFromName(e.name).toDisplayString(false);
  }

  if (e.ok && !info.warnings.isEmpty()) e.error = info.warnings.joinIntoString(" · ");
  return e;
}

void Scanner::scan() {
  working = true;

  juce::StringArray rootsCopy;
  {
    const juce::ScopedLock sl(lock);
    rootList = loadRoots();
    rootsCopy = rootList;
  }

  std::vector<std::pair<juce::File, juce::File>> presetFiles;
  std::vector<juce::File> archives;
  for (auto& r : rootsCopy) collect(juce::File(r), 0, juce::File(r), presetFiles, archives);

  struct Item { juce::File preset; juce::File base; juce::String kind; };
  std::vector<Item> items;
  std::vector<Library> broken;

  for (auto& archive : archives) {
    const auto archiveId = idFor(archive.getFullPathName() + "|" +
                                 juce::String(archive.getLastModificationTime().toMilliseconds()));
    const auto target = appDir().getChildFile("unpacked").getChildFile(archiveId);
    juce::String unpackError;
    const auto dir = SampleLoader::unpackZip(archive, target, unpackError);
    if (!dir.isDirectory()) {
      Library lib;
      lib.id = "lib" + idFor("lib|" + archive.getFullPathName());
      lib.name = libraryName(archive, "archive");
      lib.base = archive.getFullPathName();
      lib.kind = "archive";
      lib.error = unpackError.isNotEmpty() ? unpackError : "could not open the archive";
      broken.push_back(std::move(lib));
      continue;
    }

    const auto inside = dir.findChildFiles(juce::File::findFiles, true, "*.dspreset");
    int added = 0;
    for (auto& f : inside)
      if (!isJunk(f)) { items.push_back({ f, archive, "archive" }); ++added; }
    if (added == 0) {
      Library lib;
      lib.id = "lib" + idFor("lib|" + archive.getFullPathName());
      lib.name = libraryName(archive, "archive");
      lib.base = archive.getFullPathName();
      lib.kind = "archive";
      lib.error = "no presets inside the archive";
      broken.push_back(std::move(lib));
    }
  }

  for (auto& pair : presetFiles) {
    const auto base = libraryBaseFor(pair.first, pair.second);
    const auto kind = base.getFileExtension().equalsIgnoreCase(".dsbundle") ? "bundle" : "folder";
    items.push_back({ pair.first, base, kind });
  }

  std::map<juce::String, Entry> cache;
  const auto indexPath = appDir().getChildFile("index.json");
  if (indexPath.existsAsFile()) {
    const auto v = juce::JSON::parse(indexPath);
    if (auto* arr = v.getArray())
      for (auto& item : *arr) {
        auto e = varToEntry(item);
        if (e.path.isNotEmpty()) cache[e.path] = e;
      }
  }

  std::map<juce::String, Library> byBase;
  std::map<juce::String, juce::var> cacheOut;
  std::map<juce::String, juce::File> arts;

  for (auto& item : items) {
    const auto path = item.preset.getFullPathName();
    const auto mtime = item.preset.getLastModificationTime().toMilliseconds();

    Entry e;
    const auto it = cache.find(path);
    if (it != cache.end() && it->second.mtime == mtime) {
      e = it->second;
      const auto thumb = appDir().getChildFile("thumbnails").getChildFile(e.id + ".jpg");
      if (thumb.existsAsFile()) {
        e.art = thumb;
        e.hasArt = true;
      } else if (e.ok) {
        e = describe(item.preset);
      }
    } else {
      e = describe(item.preset);
    }
    e.hasArt = e.art.existsAsFile();
    cacheOut[path] = entryToVar(e);

    const auto base = item.base.getFullPathName();
    auto& lib = byBase[base];
    if (lib.id.isEmpty()) {
      lib.id = "lib" + idFor("lib|" + base);
      lib.name = libraryName(item.base, item.kind);
      lib.base = base;
      lib.kind = item.kind;
      lib.tint = e.tint;
    }
    if (lib.tint.isEmpty()) lib.tint = e.tint;
    if (lib.art == juce::File() && e.hasArt) {
      lib.art = e.art;
      lib.hasArt = true;
    }

    Preset p;
    p.id = e.id;
    p.name = e.name;
    p.path = e.path;
    p.error = e.error;
    p.zones = e.zones;
    p.groups = e.groups;
    p.low = e.low;
    p.high = e.high;
    p.ok = e.ok;
    lib.presets.push_back(std::move(p));
  }

  std::vector<Library> built = std::move(broken);
  for (auto& pair : byBase) {
    auto& lib = pair.second;

    if (!lib.hasArt) {
      juce::String tint;
      const auto cover = Artwork::findSource(juce::File(lib.base), {});
      const auto thumb = appDir().getChildFile("thumbnails").getChildFile(lib.id + ".jpg");
      if (cover.existsAsFile() && Artwork::makeThumb(cover, thumb, tint)) {
        lib.art = thumb;
        lib.hasArt = true;
        if (lib.tint.isEmpty()) lib.tint = tint;
      }
    }

    std::sort(lib.presets.begin(), lib.presets.end(), [](const Preset& a, const Preset& b) {
      return naturalCompare(a.name, b.name) < 0;
    });
    if (lib.hasArt) arts[lib.id] = lib.art;
    built.push_back(std::move(lib));
  }

  std::sort(built.begin(), built.end(), [](const Library& a, const Library& b) {
    return naturalCompare(a.name, b.name) < 0;
  });

  juce::Array<juce::var> out;
  for (auto& pair : cacheOut) out.add(pair.second);
  indexPath.replaceWithText(juce::JSON::toString(juce::var(out)));

  publish(std::move(built), arts);
  working = false;
  ver++;
}

void Scanner::publish(std::vector<Library> newList, const std::map<juce::String, juce::File>& art) {
  const juce::ScopedLock sl(lock);
  list = std::move(newList);
  artById = art;
}

} // namespace trove
