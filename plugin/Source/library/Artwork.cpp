#include "Artwork.h"

#include <algorithm>
#include "../format/SampleLoader.h"

namespace trove::Artwork {
namespace {

bool isImage(const juce::File& f) {
  const auto ext = f.getFileExtension().toLowerCase();
  return ext == ".jpg" || ext == ".jpeg" || ext == ".png" || ext == ".gif"
      || ext == ".bmp" || ext == ".webp";
}

int rank(const juce::String& stem) {
  static const char* names[] = {
    "cover", "folder", "front", "artwork", "art", "poster", "hero",
    "background", "bg", "thumbnail", "thumb", "icon", "logo", "image"
  };
  for (int i = 0; i < (int)std::size(names); ++i)
    if (stem.equalsIgnoreCase(names[i])) return i;
  return 1000;
}

} // namespace

juce::File findSource(const juce::File& dir, const juce::String& hint) {
  if (hint.isNotEmpty()) {
    auto hinted = SampleLoader::resolve(dir, hint);
    if (hinted.existsAsFile() && isImage(hinted)) return hinted;
  }

  juce::Array<juce::File> images;
  for (auto& f : dir.findChildFiles(juce::File::findFiles, false))
    if (isImage(f) && f.getSize() < 32 * 1024 * 1024) images.add(f);
  if (images.isEmpty()) return {};

  auto byRank = [](const juce::File& a, const juce::File& b) {
    const int ra = rank(a.getFileNameWithoutExtension());
    const int rb = rank(b.getFileNameWithoutExtension());
    if (ra != rb) return ra < rb;
    return a.getFileName().compareIgnoreCase(b.getFileName()) < 0;
  };
  std::sort(images.begin(), images.end(), byRank);
  return images[0];
}

bool makeThumb(const juce::File& source, const juce::File& destination, juce::String& tint) {
  auto image = juce::ImageFileFormat::loadFrom(source);
  if (!image.isValid() || image.getWidth() < 2 || image.getHeight() < 2) return false;

  const int longest = juce::jmax(image.getWidth(), image.getHeight());
  if (longest > 768)
    image = image.rescaled(juce::roundToInt(image.getWidth() * 768.0 / longest),
                          juce::roundToInt(image.getHeight() * 768.0 / longest),
                          juce::Graphics::highResamplingQuality);

  auto small = image.rescaled(8, 8);
  double r = 0, g = 0, b = 0;
  for (int y = 0; y < small.getHeight(); ++y)
    for (int x = 0; x < small.getWidth(); ++x) {
      const auto c = small.getPixelAt(x, y);
      r += c.getFloatRed();
      g += c.getFloatGreen();
      b += c.getFloatBlue();
    }
  const double n = (double)juce::jmax(1, small.getWidth() * small.getHeight());
  auto colour = juce::Colour::fromFloatRGBA((float)(r / n), (float)(g / n), (float)(b / n), 1.0f);
  const auto hsv = colour.withSaturation(juce::jmax(0.22f, colour.getSaturation()))
                         .withBrightness(juce::jlimit(0.28f, 0.62f, colour.getBrightness()));
  tint = hsv.toDisplayString(false);

  destination.getParentDirectory().createDirectory();
  juce::FileOutputStream out(destination);
  if (!out.openedOk()) return false;
  juce::JPEGImageFormat jpeg;
  jpeg.setQuality(0.84f);
  return jpeg.writeImageToStream(image, out);
}

} // namespace trove::Artwork
