// Finds libraries (a .dsbundle, a .dslibrary archive, or a folder) under the
// user's library roots, lists the presets inside each, keeps a small cached
// index so the shelf opens instantly, and makes the thumbnails the grid
// draws. All the walking happens off the message thread.
#pragma once

#include <juce_core/juce_core.h>
#include <atomic>
#include <map>
#include <vector>

namespace trove {

class Scanner : private juce::Thread {
public:
  struct Preset {
    juce::String id, name, path, error;
    int zones = 0, groups = 0, low = 0, high = 0;
    bool ok = true;
  };

  struct Library {
    juce::String id, name, base, kind;   // kind: folder | bundle | archive
    juce::String tint, error;
    juce::File art;
    bool hasArt = false;
    std::vector<Preset> presets;
  };

  struct Entry {                       // cache record for one preset file
    juce::String id, name, path, tint, error;
    juce::int64 mtime = 0;
    int zones = 0, groups = 0, low = 0, high = 0;
    bool ok = true;
    juce::File art;
    bool hasArt = false;
  };

  Scanner();
  ~Scanner() override;

  void startScan();
  bool busy() const { return working.load(); }
  int version() const { return ver.load(); }

  std::vector<Library> libraries() const;
  juce::StringArray roots() const;
  void addRoot(const juce::File& root);
  juce::File artFile(const juce::String& id) const;

  static juce::File appDir();

private:
  struct Target { juce::File file, root; bool archive = false; };

  void run() override;
  void scan();
  void publish(std::vector<Library> list, const std::map<juce::String, juce::File>& artById);
  Entry describe(const juce::File& preset) const;
  juce::StringArray loadRoots() const;
  void saveRoots(const juce::StringArray& roots) const;

  mutable juce::CriticalSection lock;
  std::vector<Library> list;
  std::map<juce::String, juce::File> artById;
  juce::StringArray rootList;
  std::atomic<int> ver { 0 };
  std::atomic<bool> working { false };
  std::atomic<bool> again { false };
  juce::WaitableEvent wake;
};

} // namespace trove
