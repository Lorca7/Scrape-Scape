#include "SceneCanvas.h"
#include "ScapeLookAndFeel.h"
#include "ModKnob.h"
#include "Engine/Modulation.h"

using namespace juce;
using namespace scape;

//==============================================================================
void drawElementGlyph (Graphics& g, int type, Point<float> c, float r, float activity, float time, bool highlighted,
                       const Image* customImage)
{
    const auto& info = getElementInfo (type);
    const Colour col (info.colour);
    const float a = jlimit (0.0f, 1.0f, activity);

    // Glow
    {
        const float glowR = r * (2.0f + 0.9f * a);
        ColourGradient glow (col.withAlpha (0.18f + 0.35f * a), c.x, c.y,
                             col.withAlpha (0.0f), c.x + glowR, c.y, true);
        g.setGradientFill (glow);
        g.fillEllipse (c.x - glowR, c.y - glowR, glowR * 2.0f, glowR * 2.0f);
    }

    if (customImage != nullptr && customImage->isValid())
    {
        // The user's cut-out: fit inside a box a little larger than the built-in glyph,
        // breathing slightly with the element's activity.
        const float box = r * 2.6f;   // drawn 1:1 from a pre-scaled cache, so it stays sharp
        const auto dest = Rectangle<float> (box, box).withCentre (c);
        Graphics::ScopedSaveState state (g);
        g.setImageResamplingQuality (Graphics::mediumResamplingQuality);
        g.setOpacity (0.85f + 0.15f * a);
        g.drawImage (*customImage, dest, RectanglePlacement::centred);

        if (highlighted)
        {
            g.setOpacity (1.0f);
            g.setColour (Colours::white.withAlpha (0.6f));
            const float hr = r * 1.55f;
            g.drawEllipse (c.x - hr, c.y - hr, hr * 2, hr * 2, 1.0f);
        }
        return;
    }

    const Colour fill = col.withAlpha (0.85f);
    const Colour edge = col.brighter (0.6f);
    const float stroke = jmax (1.2f, r * 0.09f);

    switch ((ElementType) type)
    {
        case ElementType::Drone:
        {
            ColourGradient grad (col.brighter (0.4f), c.x - r * 0.3f, c.y - r * 0.4f, col.darker (0.5f), c.x + r, c.y + r, true);
            g.setGradientFill (grad);
            g.fillEllipse (c.x - r, c.y - r, r * 2, r * 2);
            g.setColour (edge.withAlpha (0.5f));
            const float ir = r * (0.55f + 0.1f * std::sin (time * 0.8f));
            g.drawEllipse (c.x - ir, c.y - ir, ir * 2, ir * 2, stroke);
            break;
        }

        case ElementType::Bell:
        {
            Path p;
            p.startNewSubPath (c.x, c.y - r * 1.1f);
            p.lineTo (c.x + r * 0.8f, c.y);
            p.lineTo (c.x, c.y + r * 1.1f);
            p.lineTo (c.x - r * 0.8f, c.y);
            p.closeSubPath();
            g.setColour (fill);
            g.fillPath (p);
            g.setColour (edge);
            g.strokePath (p, PathStrokeType (stroke));
            break;
        }

        case ElementType::Pluck:
        {
            Path p;
            p.addTriangle (c.x, c.y - r, c.x + r * 0.95f, c.y + r * 0.7f, c.x - r * 0.95f, c.y + r * 0.7f);
            g.setColour (fill);
            g.fillPath (p);
            g.setColour (edge);
            g.strokePath (p, PathStrokeType (stroke, PathStrokeType::curved, PathStrokeType::rounded));
            for (int i = -1; i <= 1; ++i)
                g.drawLine (c.x + (float) i * r * 0.28f, c.y - r * 0.2f, c.x + (float) i * r * 0.28f, c.y + r * 0.55f, stroke * 0.6f);
            break;
        }

        case ElementType::Bass:
        {
            const auto rect = Rectangle<float> (r * 2.6f, r * 1.1f).withCentre (c);
            ColourGradient grad (col.brighter (0.3f), rect.getX(), rect.getY(), col.darker (0.6f), rect.getX(), rect.getBottom(), false);
            g.setGradientFill (grad);
            g.fillRoundedRectangle (rect, r * 0.55f);
            g.setColour (edge.withAlpha (0.7f));
            g.drawRoundedRectangle (rect, r * 0.55f, stroke);
            break;
        }

        case ElementType::Shimmer:
        {
            Path star;
            star.addStar (c, 6, r * 0.25f, r * 0.95f, time * 0.3f);
            g.setColour (fill);
            g.fillPath (star);
            for (int i = 0; i < 4; ++i)
            {
                const float ang = time * 0.9f + (float) i * MathConstants<float>::halfPi;
                const auto p = c.getPointOnCircumference (r * 1.35f, ang);
                g.fillEllipse (p.x - r * 0.12f, p.y - r * 0.12f, r * 0.24f, r * 0.24f);
            }
            break;
        }

        case ElementType::Wind:
        {
            g.setColour (edge.withAlpha (0.9f));
            for (int line = -1; line <= 1; ++line)
            {
                Path p;
                const float yy = c.y + (float) line * r * 0.5f;
                for (int i = 0; i <= 20; ++i)
                {
                    const float t = (float) i / 20.0f;
                    const float xx = c.x - r * 1.2f + t * r * 2.4f;
                    const float wy = yy + std::sin (t * 6.3f + time * 1.6f + (float) line) * r * 0.18f;
                    if (i == 0) p.startNewSubPath (xx, wy); else p.lineTo (xx, wy);
                }
                g.strokePath (p, PathStrokeType (stroke * 1.2f, PathStrokeType::curved, PathStrokeType::rounded));
            }
            break;
        }

        case ElementType::Pulse:
        {
            g.setColour (fill);
            g.fillEllipse (c.x - r * 0.4f, c.y - r * 0.4f, r * 0.8f, r * 0.8f);
            g.setColour (edge.withAlpha (0.8f));
            g.drawEllipse (c.x - r * 0.8f, c.y - r * 0.8f, r * 1.6f, r * 1.6f, stroke);
            const float wave = r * (0.9f + 0.7f * a);
            g.setColour (edge.withAlpha (0.6f * a));
            g.drawEllipse (c.x - wave, c.y - wave, wave * 2, wave * 2, stroke);
            break;
        }

        case ElementType::Shade:
        {
            Path moon;
            moon.addEllipse (c.x - r, c.y - r, r * 2, r * 2);
            Path crescent (moon);
            crescent.addEllipse (c.x - r * 0.45f, c.y - r * 1.15f, r * 2, r * 2);
            crescent.setUsingNonZeroWinding (false);

            Graphics::ScopedSaveState state (g);
            g.reduceClipRegion (moon);
            g.setColour (col.withAlpha (0.95f));
            g.fillPath (crescent);
            break;
        }

        case ElementType::Spark:
        {
            Path star;
            star.addStar (c, 4, r * 0.3f, r * 1.05f, time * 0.5f);
            g.setColour (fill);
            g.fillPath (star);
            g.setColour (Colours::white.withAlpha (0.8f));
            g.fillEllipse (c.x - r * 0.18f, c.y - r * 0.18f, r * 0.36f, r * 0.36f);
            break;
        }

        case ElementType::NumTypes:
            break;
    }

    if (highlighted)
    {
        g.setColour (Colours::white.withAlpha (0.6f));
        const float hr = r * 1.55f;
        g.drawEllipse (c.x - hr, c.y - hr, hr * 2, hr * 2, 1.0f);
    }
}

//==============================================================================
SceneCanvas::SceneCanvas (ElementBank& b, SkinBank& sk) : bank (b), skins (sk)
{
    Random rand (1234);
    for (int i = 0; i < 60; ++i)
        motes.push_back ({ rand.nextFloat(), rand.nextFloat(), 0.004f + rand.nextFloat() * 0.012f,
                           rand.nextFloat() * 6.28f, 0.6f + rand.nextFloat() * 1.6f });

    topColour = Colour (getMood (0).topColour);
    bottomColour = Colour (getMood (0).bottomColour);
    setRepaintsOnMouseActivity (false);
}

void SceneCanvas::flash (const String& message, float seconds)
{
    flashMessage = message;
    flashTime = seconds;
    repaint();
}

void SceneCanvas::advanceAnimation (float seconds)
{
    time += seconds;
    flashTime = jmax (0.0f, flashTime - seconds);

    for (auto& m : motes)
    {
        m.y -= m.speed * seconds;
        m.x += std::sin (time * 0.2f + m.phase) * 0.002f * seconds;
        if (m.y < -0.02f) { m.y = 1.02f; }
    }

    const bool stopped = isPlaying && ! isPlaying();
    stoppedDim = jlimit (0.0f, 1.0f, stoppedDim + (stopped ? seconds / 1.5f : -seconds / 0.8f));

    // Feed the visual field with where everything is and how active it sounds.
    if (getVisualContext)
    {
        std::vector<ElementVisual> list;
        for (int i = 0; i < kMaxElements; ++i)
        {
            const auto e = bank.get (i);
            if (! e.active) continue;
            ElementVisual v;
            v.index = i;
            v.type = e.type;
            v.pos = toScreen (e.x, e.y);
            v.radius = radiusFor (e.type, e.size) * e.scale;
            v.size = e.size;
            v.activity = bank.slot (i).activity.load();
            v.seed = e.seed;
            list.push_back (v);
        }
        field.setBounds (getWidth(), getHeight());
        field.update (seconds, list, getVisualContext());
    }

    // Ease the background towards the current mood's colours.
    const auto& mood = getMood (getMoodIndex ? getMoodIndex() : 0);
    const float k = jmin (1.0f, seconds * 1.5f);
    topColour = topColour.interpolatedWith (Colour (mood.topColour), k);
    bottomColour = bottomColour.interpolatedWith (Colour (mood.bottomColour), k);
}

Point<float> SceneCanvas::toScreen (float x, float y) const noexcept
{
    return { x * (float) getWidth(), y * (float) getHeight() };
}

Point<float> SceneCanvas::toNormalised (Point<float> p) const noexcept
{
    return { jlimit (0.0f, 1.0f, p.x / (float) jmax (1, getWidth())),
             jlimit (0.0f, 1.0f, p.y / (float) jmax (1, getHeight())) };
}

float SceneCanvas::radiusFor (int type, float size) const noexcept
{
    const float scale = (float) jmin (getWidth(), getHeight()) / 480.0f;
    float base = 15.0f;
    if (type == (int) ElementType::Drone) base = 24.0f;
    if (type == (int) ElementType::Bass)  base = 18.0f;
    return base * size * jlimit (0.7f, 1.8f, scale);
}

int SceneCanvas::hitTestElement (Point<float> p) const noexcept
{
    int best = -1;
    float bestDist = 1.0e9f;

    for (int i = 0; i < kMaxElements; ++i)
    {
        const auto e = bank.get (i);
        if (! e.active) continue;

        const auto c = toScreen (e.x, e.y);
        const float d = c.getDistanceFrom (p);
        if (d < radiusFor (e.type, e.size) * e.scale * 1.3f + 6.0f && d < bestDist)
        {
            best = i;
            bestDist = d;
        }
    }
    return best;
}

void SceneCanvas::paintBackground (Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    g.setGradientFill (ColourGradient (topColour, 0, 0, bottomColour, 0, bounds.getHeight(), false));
    g.fillRoundedRectangle (bounds, 12.0f);

    // A soft horizon glow that breathes slowly.
    const float glow = 0.08f + 0.04f * std::sin (time * 0.25f);
    g.setGradientFill (ColourGradient (Colours::white.withAlpha (glow), bounds.getCentreX(), bounds.getBottom(),
                                       Colours::transparentWhite, bounds.getCentreX(), bounds.getY() + bounds.getHeight() * 0.3f, true));
    g.fillRoundedRectangle (bounds, 12.0f);

    for (const auto& m : motes)
    {
        const float alpha = 0.12f + 0.12f * std::sin (time * 0.7f + m.phase);
        g.setColour (Colours::white.withAlpha (jmax (0.0f, alpha)));
        g.fillEllipse (m.x * bounds.getWidth(), m.y * bounds.getHeight(), m.size, m.size);
    }
}

void SceneCanvas::paint (Graphics& g)
{
    paintBackground (g);

    // Modifier influence zones first, so they sit beneath everything.
    for (int i = 0; i < kMaxElements; ++i)
    {
        const auto e = bank.get (i);
        if (! e.active || ! getElementInfo (e.type).isModifier) continue;

        const auto c = toScreen (e.x, e.y);
        const float rx = 0.3f * e.size * (float) getWidth();
        const float ry = 0.3f * e.size * (float) getHeight();
        const Colour col (getElementInfo (e.type).colour);
        g.setGradientFill (ColourGradient (col.withAlpha (0.14f), c.x, c.y, col.withAlpha (0.0f), c.x + rx, c.y, true));
        g.fillEllipse (c.x - rx, c.y - ry, rx * 2, ry * 2);

        Path ring;
        ring.addEllipse (c.x - rx, c.y - ry, rx * 2, ry * 2);
        Path dashed;
        const float dashes[] = { 4.0f, 6.0f };
        PathStrokeType (1.0f).createDashedStroke (dashed, ring, dashes, 2);
        g.setColour (col.withAlpha (0.25f));
        g.fillPath (dashed);
    }

    {
        Graphics::ScopedSaveState clip (g);
        Path rounded;
        rounded.addRoundedRectangle (getLocalBounds().toFloat(), 12.0f);
        g.reduceClipRegion (rounded);
        field.paintBehind (g);
    }

    const bool padMode = isPadMode && isPadMode();
    const float physicalScale = g.getInternalContext().getPhysicalPixelScaleFactor();
    if (padMode) g.beginTransparencyLayer (0.4f);

    for (int i = 0; i < kMaxElements; ++i)
    {
        const auto e = bank.get (i);
        if (! e.active) continue;

        const float bob = std::sin (time * 0.6f + (float) (e.seed % 628) * 0.01f) * 2.0f;
        const auto c = toScreen (e.x, e.y).translated (0.0f, bob);
        float act = bank.slot (i).activity.load();
        if (getElementInfo (e.type).isModifier)
            act = 0.4f + 0.3f * std::sin (time * 1.3f + (float) i);

        const float r = radiusFor (e.type, e.size) * e.scale;
        const auto img = glyphCache.get (skins, jlimit (0, kNumElementTypes - 1, e.type), roundToInt (r * 2.6f * physicalScale));
        drawElementGlyph (g, e.type, c, r, act, time + (float) i, ! padMode && (i == hoverIndex || i == dragIndex),
                          img.isValid() ? &img : nullptr);

        // Picture-size handle on the selected element.
        if (! padMode && (i == selectedIndex || i == resizeIndex))
        {
            const auto h = handlePosition (e).translated (0.0f, bob);
            g.setColour (Colours::black.withAlpha (0.5f));
            g.fillEllipse (h.x - 9.0f, h.y - 9.0f, 18.0f, 18.0f);
            g.setColour (Colours::white.withAlpha (0.9f));
            g.drawEllipse (h.x - 9.0f, h.y - 9.0f, 18.0f, 18.0f, 1.5f);
            Path arrow;
            arrow.startNewSubPath (h.x - 4.0f, h.y - 4.0f); arrow.lineTo (h.x + 4.0f, h.y + 4.0f);
            arrow.startNewSubPath (h.x + 4.0f, h.y); arrow.lineTo (h.x + 4.0f, h.y + 4.0f); arrow.lineTo (h.x, h.y + 4.0f);
            arrow.startNewSubPath (h.x - 4.0f, h.y); arrow.lineTo (h.x - 4.0f, h.y - 4.0f); arrow.lineTo (h.x, h.y - 4.0f);
            g.strokePath (arrow, PathStrokeType (1.4f));
        }
    }

    if (padMode) g.endTransparencyLayer();
    field.paintFront (g);
    paintPad (g);

    // Footer hints and status.
    auto footer = getLocalBounds().reduced (12, 8).removeFromBottom (18);
    g.setFont (FontOptions (12.0f));
    g.setColour (Colours::white.withAlpha (0.45f));
    if (padMode)
        g.drawText (String::fromUTF8 ("XY pad: drag anywhere to play the effect  \xc2\xb7  turn off XY Pad to edit elements"), footer, Justification::bottomLeft);
    else
    g.drawText (String::fromUTF8 ("Click to place  \xc2\xb7  drag to move  \xc2\xb7  scroll = presence  \xc2\xb7  corner handle = picture size  \xc2\xb7  double-click to remove"),
                footer, Justification::bottomLeft);

    if (getChordRoot)
    {
        const auto& mood = getMood (getMoodIndex ? getMoodIndex() : 0);
        const auto dot = String::fromUTF8 ("  \xc2\xb7  ");
        static const char* names[] = { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };
        const int note = noteForDegree (mood, getChordRoot());
        g.drawText (String (mood.name) + dot + "chord on " + names[note % 12] + dot
                        + String (bank.countActive()) + "/" + String (kMaxElements) + " elements",
                    footer, Justification::bottomRight);
    }

    if (stoppedDim > 0.0f)
    {
        g.setColour (Colours::black.withAlpha (0.35f * stoppedDim));
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 12.0f);
        g.setColour (Colours::white.withAlpha (0.7f * stoppedDim));
        g.setFont (FontOptions (16.0f));
        g.drawText (String::fromUTF8 ("Stopped  \xe2\x80\x94  press Play to start the music"), getLocalBounds().removeFromTop (getHeight() / 2 + 40)
                        .removeFromBottom (40), Justification::centred);
    }

    if (flashTime > 0.0f)
    {
        g.setColour (Colours::white.withAlpha (jmin (1.0f, flashTime)));
        g.setFont (FontOptions (15.0f));
        g.drawText (flashMessage, getLocalBounds().removeFromTop (40), Justification::centred);
    }

    g.setColour (Colours::white.withAlpha (0.08f));
    g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 12.0f, 1.0f);
}

//==============================================================================
Point<float> SceneCanvas::handlePosition (const ElementSnapshot& e) const noexcept
{
    const float half = radiusFor (e.type, e.size) * e.scale * 1.3f;
    return toScreen (e.x, e.y).translated (half * 0.85f, half * 0.85f);
}

void SceneCanvas::mouseDown (const MouseEvent& e)
{
    const auto p = e.position;

    if (isPadMode && isPadMode())
    {
        padDragging = true;
        if (onPadGesture) onPadGesture (true);
        const auto n = toNormalised (p);
        if (onPadMoved) onPadMoved (n.x, n.y);
        return;
    }

    if (selectedIndex >= 0 && ! e.mods.isPopupMenu())
    {
        const auto sel = bank.get (selectedIndex);
        if (sel.active && handlePosition (sel).getDistanceFrom (p) < 11.0f)
        {
            resizeIndex = selectedIndex;
            resizeStartScale = sel.scale;
            resizeStartDistance = jmax (4.0f, toScreen (sel.x, sel.y).getDistanceFrom (p));
            return;
        }
    }

    const int hit = hitTestElement (p);

    if (e.mods.isPopupMenu() || e.mods.isAltDown())
    {
        if (hit >= 0) bank.remove (hit);
        if (hit == selectedIndex) selectedIndex = -1;
        hoverIndex = -1;
        repaint();
        return;
    }

    if (hit >= 0)
    {
        dragIndex = hit;
        selectedIndex = hit;
        const auto el = bank.get (hit);
        if (onElementSelected) onElementSelected (el.type);
        dragOffset = toScreen (el.x, el.y) - p;
    }
    else
    {
        const int type = getSelectedType ? getSelectedType() : 0;
        const auto n = toNormalised (p);
        dragIndex = bank.add ((ElementType) type, n.x, n.y, 1.0f);
        selectedIndex = dragIndex;
        if (dragIndex >= 0 && onElementSelected) onElementSelected (type);
        dragOffset = {};

        if (dragIndex < 0)
        {
            flashMessage = "The scene is full (" + String (kMaxElements) + " elements). Remove one to add more.";
            flashTime = 2.5f;
        }
    }
    repaint();
}

void SceneCanvas::mouseDrag (const MouseEvent& e)
{
    if (padDragging)
    {
        const auto n = toNormalised (e.position);
        if (onPadMoved) onPadMoved (n.x, n.y);
        return;
    }

    if (resizeIndex >= 0)
    {
        const auto el = bank.get (resizeIndex);
        const float dist = toScreen (el.x, el.y).getDistanceFrom (e.position);
        bank.setScale (resizeIndex, resizeStartScale * dist / resizeStartDistance);
        repaint();
        return;
    }

    if (dragIndex >= 0)
    {
        const auto n = toNormalised (e.position + dragOffset);
        bank.move (dragIndex, n.x, n.y);
        repaint();
    }
}

void SceneCanvas::mouseUp (const MouseEvent&)
{
    if (padDragging)
    {
        padDragging = false;
        if (onPadGesture) onPadGesture (false);
    }
    dragIndex = -1;
    resizeIndex = -1;
}

void SceneCanvas::mouseMove (const MouseEvent& e)
{
    if (isPadMode && isPadMode())
    {
        setMouseCursor (MouseCursor::CrosshairCursor);
        return;
    }

    if (selectedIndex >= 0)
    {
        const auto sel = bank.get (selectedIndex);
        if (sel.active && handlePosition (sel).getDistanceFrom (e.position) < 11.0f)
        {
            setMouseCursor (MouseCursor::BottomRightCornerResizeCursor);
            return;
        }
    }

    const int hit = hitTestElement (e.position);
    if (hit != hoverIndex)
    {
        hoverIndex = hit;
        setMouseCursor (hit >= 0 ? MouseCursor::DraggingHandCursor : MouseCursor::CrosshairCursor);
        repaint();
    }
}

void SceneCanvas::mouseExit (const MouseEvent&)
{
    hoverIndex = -1;
}

void SceneCanvas::mouseDoubleClick (const MouseEvent& e)
{
    if (isPadMode && isPadMode()) return;
    const int hit = hitTestElement (e.position);
    if (hit >= 0)
    {
        bank.remove (hit);
        dragIndex = hoverIndex = selectedIndex = -1;
        repaint();
    }
}

void SceneCanvas::mouseWheelMove (const MouseEvent& e, const MouseWheelDetails& wheel)
{
    if (isPadMode && isPadMode()) return;
    const int hit = hitTestElement (e.position);
    if (hit >= 0)
    {
        const auto el = bank.get (hit);
        bank.setSize (hit, el.size * (1.0f + wheel.deltaY * 0.6f));
        repaint();
    }
}

//==============================================================================
void SceneCanvas::paintPad (Graphics& g)
{
    if (! getPadInfo) return;
    const auto info = getPadInfo();
    const bool padMode = isPadMode && isPadMode();
    if (! padMode && info.mix <= 0.0f && ! lfoDragHover) return;

    const auto bounds = getLocalBounds().toFloat();
    const auto puck = toScreen (info.x, info.y);
    const auto accent = ScapeColours::accent;

    if (padMode || lfoDragHover)
    {
        g.setColour (Colours::white.withAlpha (0.06f));
        for (int i = 1; i < 4; ++i)
        {
            g.drawVerticalLine (roundToInt (bounds.getWidth() * (float) i / 4.0f), 0.0f, bounds.getHeight());
            g.drawHorizontalLine (roundToInt (bounds.getHeight() * (float) i / 4.0f), 0.0f, bounds.getWidth());
        }

        g.setColour (accent.withAlpha (0.35f));
        g.drawVerticalLine (roundToInt (puck.x), 0.0f, bounds.getHeight());
        g.drawHorizontalLine (roundToInt (puck.y), 0.0f, bounds.getWidth());

        // Effect name and axis meanings
        auto label = getLocalBounds().reduced (14, 10).removeFromTop (22);
        g.setColour (Colours::white.withAlpha (0.85f));
        g.setFont (FontOptions (15.0f, Font::bold));
        g.drawText (info.name, label, Justification::topLeft);
        g.setFont (FontOptions (12.0f));
        g.setColour (Colours::white.withAlpha (0.6f));
        g.drawText (String::fromUTF8 ("X \xe2\x86\x92  ") + info.xLabel + String::fromUTF8 ("      Y \xe2\x86\x91  ") + info.yLabel
                        + "      Dry/Wet " + String (roundToInt (info.mix * 100.0f)) + "%",
                    label.translated (0, 20), Justification::topLeft);
    }

    // Base puck
    const float pr = padMode ? 15.0f : 9.0f;
    g.setGradientFill (ColourGradient (accent.withAlpha (0.45f * (padMode ? 1.0f : 0.6f)), puck.x, puck.y,
                                       accent.withAlpha (0.0f), puck.x + pr * 3.0f, puck.y, true));
    g.fillEllipse (puck.x - pr * 3.0f, puck.y - pr * 3.0f, pr * 6.0f, pr * 6.0f);
    g.setColour (Colours::white.withAlpha (padMode ? 0.95f : 0.5f));
    g.drawEllipse (puck.x - pr, puck.y - pr, pr * 2.0f, pr * 2.0f, 2.0f);

    // Where the LFOs are actually holding it right now
    if (info.modX >= 0.0f || info.modY >= 0.0f)
    {
        const auto m = toScreen (info.modX >= 0.0f ? info.modX : info.x, info.modY >= 0.0f ? info.modY : info.y);
        g.setColour (Colours::white.withAlpha (0.3f));
        g.drawLine (Line<float> (puck, m), 1.0f);
        g.setColour (accent.brighter (0.4f));
        g.fillEllipse (m.x - 6.0f, m.y - 6.0f, 12.0f, 12.0f);
    }

    if (lfoDragHover)
    {
        g.setColour (Colours::white.withAlpha (0.8f));
        g.drawRoundedRectangle (bounds.reduced (3.0f), 12.0f, 2.0f);
        g.setFont (FontOptions (16.0f));
        g.drawText ("Drop to modulate the XY pad", getLocalBounds(), Justification::centred);
    }
}

bool SceneCanvas::isInterestedInDragSource (const SourceDetails& d)
{
    return lfoIndexFromDrag (d.description) >= 0;
}

void SceneCanvas::itemDragEnter (const SourceDetails&) { lfoDragHover = true; repaint(); }
void SceneCanvas::itemDragExit (const SourceDetails&)  { lfoDragHover = false; repaint(); }

void SceneCanvas::itemDropped (const SourceDetails& d)
{
    lfoDragHover = false;
    repaint();
    const int lfo = lfoIndexFromDrag (d.description);
    if (lfo < 0) return;

    PopupMenu menu;
    menu.addSectionHeader ("LFO " + String (lfo + 1) + " moves the XY pad...");
    menu.addItem ((int) ModTarget::PadX, String::fromUTF8 ("Left \xe2\x86\x94 right (X)"));
    menu.addItem ((int) ModTarget::PadY, String::fromUTF8 ("Up \xe2\x86\x95 down (Y)"));
    menu.addItem ((int) ModTarget::FxMix, "Dry/Wet amount");
    menu.showMenuAsync (PopupMenu::Options().withTargetScreenArea (Rectangle<int> (1, 1).withPosition (localPointToGlobal (d.localPosition))),
                        [this, lfo, safe = Component::SafePointer<SceneCanvas> (this)] (int result)
                        {
                            if (safe != nullptr && result > 0 && onLfoDropped)
                                onLfoDropped (lfo, result);
                        });
}
