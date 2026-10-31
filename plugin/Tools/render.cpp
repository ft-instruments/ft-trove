// Offline smoke test: trove-render <preset.dspreset> <out.wav>
// Prints what the parser found, plays a chord, and reports the peak so a
// broken mapping or a silent library shows up without opening a DAW.
#include <juce_gui_basics/juce_gui_basics.h>
#include <cmath>
#include <cstdio>
#include <vector>
#include "dsp/Engine.h"
#include "format/Dspreset.h"
#include "format/SampleLoader.h"
#include "format/UiJson.h"
#include "library/Scanner.h"

namespace {

void writeWav(const juce::File& file, const juce::AudioBuffer<float>& buffer, double sampleRate) {
  const int channels = buffer.getNumChannels();
  const int frames = buffer.getNumSamples();
  const int dataBytes = frames * channels * 2;

  juce::FileOutputStream out(file);
  if (!out.openedOk()) return;

  auto put32 = [&out](juce::uint32 v) { out.write(&v, 4); };
  auto put16 = [&out](juce::uint16 v) { out.write(&v, 2); };
  out.write("RIFF", 4);
  put32((juce::uint32)(36 + dataBytes));
  out.write("WAVEfmt ", 8);
  put32(16);
  put16(1);
  put16((juce::uint16)channels);
  put32((juce::uint32)sampleRate);
  put32((juce::uint32)(sampleRate * channels * 2));
  put16((juce::uint16)(channels * 2));
  put16(16);
  out.write("data", 4);
  put32((juce::uint32)dataBytes);

  std::vector<juce::int16> interleaved((size_t)frames * channels);
  for (int i = 0; i < frames; ++i)
    for (int c = 0; c < channels; ++c)
      interleaved[(size_t)i * channels + c] =
        (juce::int16)juce::jlimit(-32768, 32767, (int)std::lround(buffer.getSample(c, i) * 32767.0f));
  out.write(interleaved.data(), (size_t)dataBytes);
}

} // namespace

int main(int argc, char** argv) {
  juce::ScopedJuceInitialiser_GUI init;

  if (argc < 2) {
    std::printf("usage: trove-render <preset.dspreset|library.dslibrary> [out.wav]\n"
                "       trove-render --scan [folder ...]\n       trove-render --ui <preset.dspreset>\n");
    return 2;
  }

  if (juce::String::fromUTF8(argv[1]) == "--ui" && argc > 2) {
    const auto ui = trove::Dspreset::readPanel(juce::File(juce::String::fromUTF8(argv[2])));
    if (!ui.valid) {
      std::printf("no <ui> in that preset\n");
      return 1;
    }
    std::printf("%s\n", juce::JSON::toString(trove::uiJson(ui), true).toRawUTF8());
    return 0;
  }

  if (juce::String::fromUTF8(argv[1]) == "--scan") {
    trove::Scanner scanner;
    for (int i = 2; i < argc; ++i) scanner.addRoot(juce::File(juce::String::fromUTF8(argv[i])));
    scanner.startScan();
    const auto deadline = juce::Time::getMillisecondCounter() + 60000;
    while (scanner.version() == 0 && juce::Time::getMillisecondCounter() < deadline)
      juce::Thread::sleep(40);

    const auto libs = scanner.libraries();
    int presets = 0, bad = 0, art = 0;
    std::printf("roots     %s\n", scanner.roots().joinIntoString(", ").toRawUTF8());
    std::printf("libraries %d\n", (int)libs.size());
    for (auto& l : libs) {
      presets += (int)l.presets.size();
      for (auto& p : l.presets) if (!p.ok) ++bad;
      const bool hasArt = l.hasArt && scanner.artFile(l.id).existsAsFile();
      if (hasArt) ++art;
      std::printf("  %-30s %-8s %3d presets  %s%s\n", l.name.toRawUTF8(), l.kind.toRawUTF8(),
                  (int)l.presets.size(), hasArt ? "art " : "    ", l.error.toRawUTF8());
    }
    std::printf("presets   %d, unreadable %d, artwork %d/%d\n",
                presets, bad, art, (int)libs.size());
    return (libs.empty() || presets == 0) ? 1 : 0;
  }

  // --rr <preset> <note> <groups,..>: play one key six times with only those
  // groups switched on and print how many voices each press started.
  const bool rrTest = juce::String::fromUTF8(argv[1]) == "--rr" && argc > 4;
  // --run <preset> <note> "<step>;<step>;..": press the key and apply panel
  // controls as the page would, printing the level after each "r" step.
  //   r | tag:NAME:V | gvol:G:V | gpan:G:V | gon:G:0/1 | fx:I:PARAM:V | gfx:G:I:PARAM:V | tune:V
  const bool runTest = juce::String::fromUTF8(argv[1]) == "--run" && argc > 4;
  juce::File preset(juce::String::fromUTF8(argv[(rrTest || runTest) ? 2 : 1]));
  trove::SampleLoader loader;

  if (preset.getFileExtension().equalsIgnoreCase(".dslibrary")) {
    auto target = juce::File::getSpecialLocation(juce::File::tempDirectory)
                      .getChildFile("trove-render")
                      .getChildFile(preset.getFileNameWithoutExtension() + "-" +
                                    juce::String(preset.getLastModificationTime().toMilliseconds()));
    juce::String zipError;
    const auto dir = trove::SampleLoader::unpackZip(preset, target, zipError);
    if (!dir.isDirectory()) {
      std::printf("unpack failed: %s\n", zipError.toRawUTF8());
      return 1;
    }
    const auto found = dir.findChildFiles(juce::File::findFiles, true, "*.dspreset");
    if (found.isEmpty()) {
      std::printf("no .dspreset inside the archive\n");
      return 1;
    }
    preset = found[0];
  }

  juce::String error;
  auto program = trove::Dspreset::parse(preset, loader, error);
  if (program == nullptr) {
    std::printf("parse failed: %s\n", error.toRawUTF8());
    return 1;
  }

  std::printf("program   %s\n", program->name.toRawUTF8());
  std::printf("zones     %d in %d groups, %d sample files\n",
              program->zoneCount(), program->groups, (int)program->samples.size());
  std::printf("envelope  a %.3f d %.3f s %.3f r %.3f, volume %.3f\n",
              program->env.attack, program->env.decay, program->env.sustain,
              program->env.release, program->volume);
  for (auto& w : program->warnings) std::printf("warning   %s\n", w.toRawUTF8());

  int low = 127, high = 0;
  for (auto& z : program->zones) { low = juce::jmin(low, z.loNote); high = juce::jmax(high, z.hiNote); }
  std::printf("range     %d..%d\n", low, high);

  const double sr = 48000.0;
  const int seconds = 3;
  juce::AudioBuffer<float> audio(2, (int)(sr * seconds));
  audio.clear();

  trove::Engine engine;
  engine.prepare(sr);
  engine.setProgram(program);
  std::array<double, trove::NUM_PARAMS> params {};
  for (int i = 0; i < trove::NUM_PARAMS; ++i) params[(size_t)i] = trove::PARAMS[i].def;
  params[trove::pAttack] = program->env.attack;
  params[trove::pDecay] = program->env.decay;
  params[trove::pSustain] = program->env.sustain;
  params[trove::pRelease] = program->env.release;
  params[trove::pGain] = 0.0;
  engine.setParams(params);

  if (runTest) {
    const int note = juce::String::fromUTF8(argv[3]).getIntValue();
    juce::StringArray steps;
    steps.addTokens(juce::String::fromUTF8(argv[4]), ";", "");
    bool held = false;
    juce::AudioBuffer<float> chunk(2, (int)(sr * 0.3));
    for (auto& raw : steps) {
      const auto parts = juce::StringArray::fromTokens(raw.trim(), ":", "");
      if (parts.isEmpty()) continue;
      const auto op = parts[0];
      auto num = [&](int i) { return i < parts.size() ? parts[i].getDoubleValue() : 0.0; };
      trove::FxParam fp;
      if (op == "r") {
        if (!held) { engine.noteOn(note, 100.0f); held = true; }
        chunk.clear();
        engine.process(chunk);
        double sum = 0.0;
        for (int c = 0; c < 2; ++c)
          for (int i = 0; i < chunk.getNumSamples(); ++i) sum += (double)chunk.getSample(c, i) * chunk.getSample(c, i);
        const double rms = std::sqrt(sum / (2.0 * chunk.getNumSamples()));
        std::printf("  level %7.1f dB   voices %d\n", juce::Decibels::gainToDecibels(rms, -120.0), engine.activeVoices());
      } else if (op == "tag") engine.setTagVolume(parts[1], num(2));
      else if (op == "gvol") engine.setGroupVolume((int)num(1), num(2));
      else if (op == "gpan") engine.setGroupPan((int)num(1), num(2));
      else if (op == "gon") engine.setGroupEnabled((int)num(1), num(2) >= 0.5);
      else if (op == "probe") {
        int shown = 0;
        for (auto& z : program->zones) {
          if (!z.matches(note, 100) || shown >= 4) continue;
          ++shown;
          float peak = 0.0f;
          const auto* smp = z.sample.get();
          const int from = (int)juce::jlimit(0.0, (double)smp->frames - 1, z.startFrame);
          const int to = (int)juce::jmin((double)smp->frames, z.startFrame + 48000.0);
          for (int i = from; i < to; ++i) peak = juce::jmax(peak, std::abs(smp->left()[i]));
          std::printf("  zone group %d start %.0f end %.0f of %d frames  gain %.3f env a %.3f d %.3f s %.3f  peak %.4f\n",
                      z.group, z.startFrame, z.endFrame, (int)smp->frames, z.gain, z.env.attack, z.env.decay, z.env.sustain, peak);
        }
      }
      else if (op == "tune") engine.setGlobalTuning(num(1));
      else if (op == "mod") engine.setModParam((int)num(1), parts[2], num(3));
      else if (op == "velon") engine.setVelocityEnabled((int)num(1), num(2) >= 0.5);
      else if (op == "gpos") engine.setGroupFxPosition((int)num(1), (int)num(2), num(3));
      else if (op == "fx" && trove::fxParamFromName(parts[2], fp)) engine.setFx((int)num(1), fp, num(3));
      else if (op == "gfx" && trove::fxParamFromName(parts[3], fp)) engine.setGroupFx((int)num(1), (int)num(2), fp, num(4));
      else std::printf("  ? %s\n", raw.toRawUTF8());
      if (op != "r") std::printf(" %s\n", raw.trim().toRawUTF8());
    }
    return 0;
  }

  if (rrTest) {
    const int note = juce::String::fromUTF8(argv[3]).getIntValue();
    juce::StringArray on;
    on.addTokens(juce::String::fromUTF8(argv[4]), ",", "");
    for (int g = 0; g < 256; ++g) engine.setGroupEnabled(g, on.contains(juce::String(g)));
    juce::AudioBuffer<float> blockBuf(2, 256);
    int previous = 0;
    for (int press = 1; press <= 6; ++press) {
      engine.noteOn(note, 100.0f);
      blockBuf.clear();
      engine.process(blockBuf);
      const int now = engine.activeVoices();
      std::printf("press %d: +%d voices (now %d)\n", press, now - previous, now);
      previous = now;
      engine.noteOff(note);
    }
    return 0;
  }

  engine.noteOn(60, 100.0f);
  engine.noteOn(64, 82.0f);
  engine.noteOn(67, 64.0f);

  const int block = 512;
  juce::AudioBuffer<float> slice(2, block);
  float peak = 0.0f;
  double sum = 0.0;
  int count = 0;
  for (int pos = 0; pos < audio.getNumSamples(); pos += block) {
    const int n = juce::jmin(block, audio.getNumSamples() - pos);
    slice.setSize(2, n, false, false, true);
    slice.clear();
    engine.process(slice);
    for (int c = 0; c < 2; ++c) {
      audio.copyFrom(c, pos, slice, c, 0, n);
      for (int i = 0; i < n; ++i) {
        const float s = slice.getSample(c, i);
        peak = juce::jmax(peak, std::abs(s));
        sum += (double)s * s;
        ++count;
      }
    }
    if (pos > (int)(sr * 1.6) && engine.activeVoices() > 0) engine.noteOff(64);
  }

  const double rms = count > 0 ? std::sqrt(sum / count) : 0.0;
  std::printf("output    peak %.4f  rms %.4f  voices %d\n", peak, rms, engine.activeVoices());

  if (argc >= 3) {
    const juce::File out(juce::String::fromUTF8(argv[2]));
    writeWav(out, audio, sr);
    std::printf("wrote     %s\n", out.getFullPathName().toRawUTF8());
  }

  if (peak < 0.0005f) {
    std::printf("FAIL: silence\n");
    return 1;
  }
  std::printf("OK\n");
  return 0;
}
