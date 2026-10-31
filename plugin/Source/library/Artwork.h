// Finds a library's cover image and turns it into a small cached thumbnail
// plus a tint the shelf can use for placeholders and glows.
#pragma once

#include <juce_graphics/juce_graphics.h>

namespace trove::Artwork {

juce::File findSource(const juce::File& libraryDir, const juce::String& hint);

bool makeThumb(const juce::File& source, const juce::File& destination, juce::String& tint);

} // namespace trove::Artwork
