#include "SampleLoader.h"

namespace trove {

SampleLoader::SampleLoader() {
  formats.registerBasicFormats();
}

juce::File SampleLoader::resolve(const juce::File& base, const juce::String& relative) {
  auto rel = relative.trim().replaceCharacter('\\', '/');
  if (rel.isEmpty()) return {};
  if (juce::File::isAbsolutePath(rel)) return juce::File(rel);

  auto file = base;
  for (auto& part : juce::StringArray::fromTokens(rel, "/", "")) {
    if (part.isEmpty() || part == ".") continue;
    if (part == "..") { file = file.getParentDirectory(); continue; }

    auto exact = file.getChildFile(part);
    if (exact.exists()) { file = exact; continue; }

    juce::File hit;
    for (auto& child : file.findChildFiles(juce::File::findFilesAndDirectories, false))
      if (child.getFileName().equalsIgnoreCase(part)) { hit = child; break; }
    file = hit != juce::File() ? hit : exact;
  }
  return file;
}

std::shared_ptr<const Sample> SampleLoader::load(const juce::File& file) {
  if (!file.existsAsFile()) return {};

  const auto key = file.getFullPathName() + "|"
                 + juce::String(file.getLastModificationTime().toMilliseconds());
  {
    const juce::ScopedLock sl(lock);
    const auto it = cache.find(key);
    if (it != cache.end()) return it->second;
  }

  auto stream = std::make_unique<juce::FileInputStream>(file);
  if (!stream->openedOk()) return {};

  auto reader = formats.createReaderFor(std::move(stream));
  if (!reader) return {};

  const auto frames = (juce::int64)reader->lengthInSamples;
  if (frames < 2) return {};

  auto sample = std::make_shared<Sample>();
  sample->sourceRate = reader->sampleRate > 0 ? reader->sampleRate : 44100.0;

  // Some libraries keep every note in one huge WAV (Claustrophobic Piano is
  // 200 million frames). Keep up to 2^28 frames, and report only what is
  // really in memory: a voice that reads past it would crash the host.
  const int channels = juce::jlimit(1, 2, (int)reader->numChannels);
  const int length = (int)juce::jmin(frames, (juce::int64)1 << 28);
  sample->frames = length;
  sample->audio.setSize(channels, length);
  reader->read(&sample->audio, 0, length, 0, true, channels > 1);

  const auto& meta = reader->metadataValues;
  double loopStart = -1.0, loopEnd = -1.0;
  for (int i = 0; i < meta.size(); ++i) {
    const auto name = meta.getAllKeys()[i].toLowerCase().removeCharacters(" _");
    if (!name.contains("loop")) continue;
    if (name.contains("start") && loopStart < 0) loopStart = meta.getAllValues()[i].getDoubleValue();
    if ((name.contains("end") || name.contains("length")) && loopEnd < 0) loopEnd = meta.getAllValues()[i].getDoubleValue();
  }
  if (loopStart >= 0 && loopEnd > loopStart + 1) {
    sample->hasLoop = true;
    sample->loopStart = (juce::int64)loopStart;
    sample->loopEnd = (juce::int64)loopEnd;
  }

  {
    const juce::ScopedLock sl(lock);
    cache[key] = sample;
  }
  return sample;
}

juce::File SampleLoader::unpackZip(const juce::File& zip, const juce::File& targetDir, juce::String& error) {
  static juce::CriticalSection unpackLock;
  const juce::ScopedLock sl(unpackLock);

  const auto marker = targetDir.getChildFile(".unpacked");
  if (marker.existsAsFile()) return targetDir;

  targetDir.createDirectory();
  juce::ZipFile archive(zip);
  if (archive.getNumEntries() == 0) {
    error = "not a zip archive";
    return {};
  }
  if (!archive.uncompressTo(targetDir, true)) {
    error = "could not extract the archive";
    return {};
  }
  marker.replaceWithText("ok");
  return targetDir;
}

} // namespace trove
