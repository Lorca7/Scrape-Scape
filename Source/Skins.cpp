#include "Skins.h"

#if JUCE_MAC
namespace juce { Image juce_loadWithCoreImage (InputStream& input); } // provided by juce_graphics on macOS
#endif

using namespace juce;

namespace
{
    constexpr int maxSourceSize = 2048;
    constexpr int maxGlyphSize = 1024;

    bool hasTransparency (const Image& img)
    {
        if (! img.hasAlphaChannel()) return false;
        const Image::BitmapData data (img, Image::BitmapData::readOnly);
        for (int y = 0; y < img.getHeight(); y += 2)
            for (int x = 0; x < img.getWidth(); x += 2)
                if (data.getPixelColour (x, y).getAlpha() < 250)
                    return true;
        return false;
    }

    String encodeImage (const Image& img)
    {
        MemoryOutputStream out;
        if (hasTransparency (img))
        {
            PNGImageFormat png;
            png.writeImageToStream (img, out);
        }
        else
        {
            JPEGImageFormat jpeg;
            jpeg.setQuality (0.9f);
            jpeg.writeImageToStream (img, out);
        }
        return out.getMemoryBlock().toBase64Encoding();
    }

    Image decodeImage (const String& base64)
    {
        MemoryBlock block;
        if (! block.fromBase64Encoding (base64)) return {};
        return ImageFileFormat::loadFrom (block.getData(), block.getSize());
    }
}

//==============================================================================
String SkinTools::getImageWildcard()
{
   #if JUCE_MAC
    return "*.png;*.jpg;*.jpeg;*.tif;*.tiff;*.gif;*.bmp;*.heic;*.heif;*.webp;*.psd";
   #else
    return "*.png;*.jpg;*.jpeg;*.gif";
   #endif
}

bool SkinTools::isSupportedImageFile (const File& f)
{
   #if JUCE_MAC
    const char* extensions = "png;jpg;jpeg;tif;tiff;gif;bmp;heic;heif;webp;psd";
   #else
    const char* extensions = "png;jpg;jpeg;gif";
   #endif
    return f.existsAsFile() && f.hasFileExtension (extensions);
}

Image SkinTools::loadAnyImage (const File& f)
{
    auto img = ImageFileFormat::loadFrom (f);

   #if JUCE_MAC
    if (! img.isValid())
    {
        FileInputStream in (f);
        if (in.openedOk())
            img = juce_loadWithCoreImage (in);
    }
   #endif

    return img;
}

Image SkinTools::prepareSource (const Image& img)
{
    if (! img.isValid()) return {};

    auto argb = img.convertedToFormat (Image::ARGB);
    const int w = argb.getWidth(), h = argb.getHeight();
    const float scale = jmin (1.0f, (float) maxSourceSize / (float) jmax (w, h));
    if (scale >= 1.0f) return argb;

    const int nw = jmax (1, roundToInt ((float) w * scale)), nh = jmax (1, roundToInt ((float) h * scale));
    Image out (Image::ARGB, nw, nh, true);
    Graphics g (out);
    g.setImageResamplingQuality (Graphics::highResamplingQuality);
    g.drawImageTransformed (argb, AffineTransform::scale ((float) nw / (float) w, (float) nh / (float) h));
    return out;
}

Path SkinTools::outlinePath (const ElementSkin& skin, Rectangle<float> area)
{
    Path p;
    auto toArea = [&] (Point<float> n) { return Point<float> (area.getX() + n.x * area.getWidth(), area.getY() + n.y * area.getHeight()); };

    switch (skin.shape)
    {
        case ElementSkin::freehand:
            if (skin.outline.size() >= 3)
            {
                p.startNewSubPath (toArea (skin.outline.front()));
                for (size_t i = 1; i < skin.outline.size(); ++i)
                    p.lineTo (toArea (skin.outline[i]));
                p.closeSubPath();
                return p;
            }
            break;

        case ElementSkin::ellipse:
        case ElementSkin::rectangle:
            if (skin.outline.size() >= 2)
            {
                const auto r = Rectangle<float> (toArea (skin.outline[0]), toArea (skin.outline[1]));
                if (r.getWidth() > 2.0f && r.getHeight() > 2.0f)
                {
                    if (skin.shape == ElementSkin::ellipse) p.addEllipse (r);
                    else p.addRectangle (r);
                    return p;
                }
            }
            break;

        default:
            break;
    }

    p.addRectangle (area);
    return p;
}

Image SkinTools::renderGlyph (const ElementSkin& skin)
{
    if (! skin.source.isValid()) return {};

    const auto full = skin.source.getBounds().toFloat();
    const auto path = outlinePath (skin, full);
    auto bounds = path.getBounds().getIntersection (full);
    if (bounds.getWidth() < 2.0f || bounds.getHeight() < 2.0f)
        bounds = full;

    const float scale = jmin (1.0f, (float) maxGlyphSize / jmax (bounds.getWidth(), bounds.getHeight()));
    const int w = jmax (1, (int) std::ceil (bounds.getWidth() * scale));
    const int h = jmax (1, (int) std::ceil (bounds.getHeight() * scale));

    Image out (Image::ARGB, w, h, true);
    Graphics g (out);
    const auto t = AffineTransform::translation (-bounds.getX(), -bounds.getY()).scaled (scale);
    g.reduceClipRegion (path, t);
    g.setImageResamplingQuality (Graphics::highResamplingQuality);
    g.drawImageTransformed (skin.source, t);
    return out;
}

void SkinTools::finalise (ElementSkin& skin)
{
    skin.glyph = renderGlyph (skin);
    if (skin.encodedSource.isEmpty() && skin.source.isValid())
        skin.encodedSource = encodeImage (skin.source);
}

//==============================================================================
void SkinBank::set (int type, ElementSkin skin)
{
    if (type < 0 || type >= scape::kNumElementTypes) return;
    SkinTools::finalise (skin);
    const ScopedLock sl (lock);
    skins[(size_t) type] = std::move (skin);
    ++version;
}

void SkinBank::reset (int type)
{
    if (type < 0 || type >= scape::kNumElementTypes) return;
    const ScopedLock sl (lock);
    skins[(size_t) type] = {};
    ++version;
}

void SkinBank::resetAll()
{
    const ScopedLock sl (lock);
    for (auto& s : skins) s = {};
    ++version;
}

ElementSkin SkinBank::get (int type) const
{
    const ScopedLock sl (lock);
    return skins[(size_t) jlimit (0, scape::kNumElementTypes - 1, type)];
}

Image SkinBank::getGlyph (int type) const
{
    const ScopedLock sl (lock);
    return skins[(size_t) jlimit (0, scape::kNumElementTypes - 1, type)].glyph;
}

bool SkinBank::hasSkin (int type) const
{
    return getGlyph (type).isValid();
}

ValueTree SkinBank::toValueTree() const
{
    ValueTree tree ("Skins");
    const ScopedLock sl (lock);

    for (int t = 0; t < scape::kNumElementTypes; ++t)
    {
        const auto& s = skins[(size_t) t];
        if (! s.isValid() || s.encodedSource.isEmpty()) continue;

        StringArray pts;
        for (auto p : s.outline)
            pts.add (String (p.x, 5) + "," + String (p.y, 5));

        ValueTree node ("Skin");
        node.setProperty ("type", t, nullptr);
        node.setProperty ("shape", s.shape, nullptr);
        node.setProperty ("outline", pts.joinIntoString (";"), nullptr);
        node.setProperty ("image", s.encodedSource, nullptr);
        tree.appendChild (node, nullptr);
    }
    return tree;
}

void SkinBank::fromValueTree (const ValueTree& tree)
{
    std::array<ElementSkin, scape::kNumElementTypes> loaded;

    for (auto node : tree)
    {
        const int t = (int) node.getProperty ("type", -1);
        if (t < 0 || t >= scape::kNumElementTypes) continue;

        ElementSkin s;
        s.encodedSource = node.getProperty ("image").toString();
        s.source = SkinTools::prepareSource (decodeImage (s.encodedSource));
        if (! s.source.isValid()) continue;

        s.shape = jlimit (0, 3, (int) node.getProperty ("shape", (int) ElementSkin::wholeImage));
        for (auto& pair : StringArray::fromTokens (node.getProperty ("outline").toString(), ";", ""))
        {
            const auto xy = StringArray::fromTokens (pair, ",", "");
            if (xy.size() == 2)
                s.outline.push_back ({ xy[0].getFloatValue(), xy[1].getFloatValue() });
        }

        SkinTools::finalise (s);
        loaded[(size_t) t] = std::move (s);
    }

    const ScopedLock sl (lock);
    skins = std::move (loaded);
    ++version;
}

//==============================================================================
Image SkinTools::scaledHighQuality (const Image& img, int maxDimension)
{
    if (! img.isValid()) return {};
    const int w = img.getWidth(), h = img.getHeight();
    const float scale = (float) maxDimension / (float) jmax (w, h);
    if (scale >= 1.0f) return img;

    const int tw = jmax (1, roundToInt ((float) w * scale)), th = jmax (1, roundToInt ((float) h * scale));
    Image current = img;

    // Halve until within 2x of the target, so every pass averages neighbouring pixels.
    while (current.getWidth() / 2 >= tw && current.getHeight() / 2 >= th)
        current = current.rescaled (jmax (1, current.getWidth() / 2), jmax (1, current.getHeight() / 2),
                                    Graphics::mediumResamplingQuality);

    return current.rescaled (tw, th, Graphics::highResamplingQuality);
}

Image GlyphCache::get (const SkinBank& bank, int type, int pixelSize)
{
    if (bank.getVersion() != version || cache.size() > 256)
    {
        cache.clear();
        version = bank.getVersion();
    }

    const auto full = bank.getGlyph (type);
    if (! full.isValid()) return {};

    pixelSize = jlimit (8, 2048, (pixelSize + 3) / 4 * 4);   // quantise to keep the cache small
    const int key = type * 4096 + pixelSize;
    if (auto it = cache.find (key); it != cache.end())
        return it->second;

    auto img = SkinTools::scaledHighQuality (full, pixelSize);
    cache[key] = img;
    return img;
}
