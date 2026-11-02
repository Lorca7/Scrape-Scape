#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace scape
{

// The building blocks a user can place on the canvas. Sound elements generate
// audio; modifier elements (Shade, Spark) colour the elements around them.
enum class ElementType : int
{
    Drone = 0,
    Bowl,
    Pluck,
    Bass,
    Shimmer,
    Chimes,
    Pad,
    Shade,
    Spark,
    NumTypes
};

constexpr int kNumElementTypes = (int) ElementType::NumTypes;
constexpr int kMaxElements     = 32;

struct ElementInfo
{
    const char* name;
    const char* description;
    std::uint32_t colour;   // ARGB
    bool isModifier;
};

const ElementInfo& getElementInfo (ElementType type) noexcept;
inline const ElementInfo& getElementInfo (int type) noexcept { return getElementInfo ((ElementType) type); }

// Plain, copyable description of one element.
struct ElementSnapshot
{
    bool active = false;
    int type = 0;
    float x = 0.5f, y = 0.5f;   // normalised canvas position (0..1, y = 0 is the top)
    float size = 1.0f;          // 0.4 .. 1.8  (sound presence + glyph size)
    float scale = 1.0f;         // 0.3 .. 60   (picture size only, no effect on sound)
    std::uint32_t seed = 0;     // unique id + random seed for the voice
};

// Lock-free store shared between the UI (writes) and the audio engine (reads).
class ElementBank
{
public:
    struct Slot
    {
        std::atomic<bool> active { false };
        std::atomic<int> type { 0 };
        std::atomic<float> x { 0.5f }, y { 0.5f }, size { 1.0f }, scale { 1.0f };
        std::atomic<std::uint32_t> seed { 0 };

        // Written by the engine, read by the UI.
        std::atomic<float> activity { 0.0f };
        std::atomic<bool> sounding { false };
    };

    static constexpr float minSize = 0.4f, maxSize = 1.8f;
    static constexpr float minScale = 0.3f, maxScale = 60.0f;

    int add (ElementType type, float x, float y, float size = 1.0f) noexcept;
    void remove (int index) noexcept;
    void move (int index, float x, float y) noexcept;
    void setSize (int index, float size) noexcept;
    void setScale (int index, float scale) noexcept;
    void clear() noexcept;

    ElementSnapshot get (int index) const noexcept;
    void set (int index, const ElementSnapshot& s) noexcept;
    void copyFrom (const ElementBank& other) noexcept;
    int countActive() const noexcept;

    Slot& slot (int i) noexcept { return slots[(size_t) i]; }
    const Slot& slot (int i) const noexcept { return slots[(size_t) i]; }

private:
    std::array<Slot, kMaxElements> slots;
    std::atomic<std::uint32_t> nextSeed { 0x9e3779b9u };
};

} // namespace scape
