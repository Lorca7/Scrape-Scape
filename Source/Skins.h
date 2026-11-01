#pragma once

#include "Engine/Elements.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <map>
#include <atomic>
#include <vector>

// A user image that replaces an element's built-in glyph, plus the outline that
// selects which part of the image is used.
struct ElementSkin
{
    enum Shape { freehand = 0, ellipse, rectangle, wholeImage };

    juce::Image source;                          // downscaled copy of the uploaded image
    int shape = wholeImage;
    std::vector<juce::Point<float>> outline;     // normalised (0..1) image coordinates
    juce::Image glyph;                           // rendered cut-out shown on the canvas
    juce::String encodedSource;                  // cached base64 image for saving

    bool isValid() const noexcept { return glyph.isValid(); }
};

namespace SkinTools
{
    // File types offered in choosers / accepted by drag and drop.
    juce::String getImageWildcard();
    bool isSupportedImageFile (const juce::File& f);

    // PNG, JPEG and GIF everywhere; on macOS also TIFF, HEIC, BMP, WebP, PSD... via ImageIO.
    juce::Image loadAnyImage (const juce::File& f);

    // Downscales to a sensible working size and converts to ARGB.
    juce::Image prepareSource (const juce::Image& img);

    // The outline as a path inside `imageArea` (where the whole source image is drawn).
    juce::Path outlinePath (const ElementSkin& skin, juce::Rectangle<float> imageArea);

    // Cuts the outlined region out of the source (transparent outside the outline).
    juce::Image renderGlyph (const ElementSkin& skin);

    // Fills in glyph + encodedSource from source/shape/outline.
    void finalise (ElementSkin& skin);

    // High-quality resize (progressive halving, then a final smooth pass).
    juce::Image scaledHighQuality (const juce::Image& img, int maxDimension);
}

class SkinBank;

// UI-thread cache of glyphs pre-scaled to the exact on-screen pixel size, so
// pictures stay sharp at any size (including Retina screens).
class GlyphCache
{
public:
    juce::Image get (const SkinBank& bank, int type, int pixelSize);

private:
    int version = -1;
    std::map<int, juce::Image> cache;
};

// Thread-safe holder of one optional skin per element type.
class SkinBank
{
public:
    void set (int type, ElementSkin skin);
    void reset (int type);
    void resetAll();

    ElementSkin get (int type) const;
    juce::Image getGlyph (int type) const;
    bool hasSkin (int type) const;
    int getVersion() const noexcept { return version.load(); }

    juce::ValueTree toValueTree() const;
    void fromValueTree (const juce::ValueTree& tree);

private:
    mutable juce::CriticalSection lock;
    std::array<ElementSkin, scape::kNumElementTypes> skins;
    std::atomic<int> version { 0 };
};
