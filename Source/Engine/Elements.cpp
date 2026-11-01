#include "Elements.h"

#include <algorithm>

namespace scape
{

const ElementInfo& getElementInfo (ElementType type) noexcept
{
    static const ElementInfo infos[kNumElementTypes] = {
        { "Drone",   "A slow, breathing chord-tone pad. Higher on the canvas = higher register.", 0xff7fb8ffu, false },
        { "Bell",    "Sparse FM bells that ring out over the harmony.",                         0xffffd27fu, false },
        { "Pluck",   "Plucked-string figures that wander up and down the scale.",               0xff9cf2c0u, false },
        { "Bass",    "Deep, soft root notes that follow the chord changes.",                    0xffb38cffu, false },
        { "Shimmer", "A cloud of tiny high sine grains.",                                       0xffe6f7ffu, false },
        { "Wind",    "Tuned, singing noise that drifts between chord tones.",                   0xff8fd6d6u, false },
        { "Pulse",   "A gentle repeating pattern, echoed through the delay.",                   0xffff9a8cu, false },
        { "Shade",   "Modifier: darkens, calms and drowns nearby elements in reverb.",          0xff7a74a8u, true  },
        { "Spark",   "Modifier: brightens nearby elements and makes them more active.",         0xffffef5au, true  },
    };

    const int i = std::clamp ((int) type, 0, kNumElementTypes - 1);
    return infos[i];
}

int ElementBank::add (ElementType type, float x, float y, float size) noexcept
{
    int index = -1;

    // Prefer a slot whose previous voice has fully faded out, so we never cut a tail.
    for (int i = 0; i < kMaxElements && index < 0; ++i)
        if (! slots[(size_t) i].active.load() && ! slots[(size_t) i].sounding.load())
            index = i;

    for (int i = 0; i < kMaxElements && index < 0; ++i)
        if (! slots[(size_t) i].active.load())
            index = i;

    if (index < 0)
        return -1;

    auto& s = slots[(size_t) index];
    s.type.store ((int) type);
    s.x.store (std::clamp (x, 0.0f, 1.0f));
    s.y.store (std::clamp (y, 0.0f, 1.0f));
    s.size.store (std::clamp (size, minSize, maxSize));
    s.scale.store (1.0f);
    s.seed.store (nextSeed.fetch_add (0x6d2b79f5u) * 2654435761u + 1u);
    s.activity.store (0.0f);
    s.active.store (true, std::memory_order_release);
    return index;
}

void ElementBank::remove (int index) noexcept
{
    if (index >= 0 && index < kMaxElements)
        slots[(size_t) index].active.store (false);
}

void ElementBank::move (int index, float x, float y) noexcept
{
    if (index >= 0 && index < kMaxElements)
    {
        slots[(size_t) index].x.store (std::clamp (x, 0.0f, 1.0f));
        slots[(size_t) index].y.store (std::clamp (y, 0.0f, 1.0f));
    }
}

void ElementBank::setSize (int index, float size) noexcept
{
    if (index >= 0 && index < kMaxElements)
        slots[(size_t) index].size.store (std::clamp (size, minSize, maxSize));
}

void ElementBank::setScale (int index, float scale) noexcept
{
    if (index >= 0 && index < kMaxElements)
        slots[(size_t) index].scale.store (std::clamp (scale, minScale, maxScale));
}

void ElementBank::clear() noexcept
{
    for (auto& s : slots)
        s.active.store (false);
}

ElementSnapshot ElementBank::get (int index) const noexcept
{
    const auto& s = slots[(size_t) index];
    ElementSnapshot e;
    e.active = s.active.load (std::memory_order_acquire);
    e.type = s.type.load();
    e.x = s.x.load();
    e.y = s.y.load();
    e.size = s.size.load();
    e.scale = s.scale.load();
    e.seed = s.seed.load();
    return e;
}

void ElementBank::set (int index, const ElementSnapshot& e) noexcept
{
    auto& s = slots[(size_t) index];
    s.active.store (false);
    s.type.store (std::clamp (e.type, 0, kNumElementTypes - 1));
    s.x.store (std::clamp (e.x, 0.0f, 1.0f));
    s.y.store (std::clamp (e.y, 0.0f, 1.0f));
    s.size.store (std::clamp (e.size, minSize, maxSize));
    s.scale.store (std::clamp (e.scale, minScale, maxScale));
    s.seed.store (e.seed != 0 ? e.seed : nextSeed.fetch_add (0x6d2b79f5u) * 2654435761u + 1u);
    s.active.store (e.active, std::memory_order_release);
}

void ElementBank::copyFrom (const ElementBank& other) noexcept
{
    for (int i = 0; i < kMaxElements; ++i)
        set (i, other.get (i));
}

int ElementBank::countActive() const noexcept
{
    int n = 0;
    for (auto& s : slots)
        n += s.active.load() ? 1 : 0;
    return n;
}

} // namespace scape
