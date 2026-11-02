#include "Backdrop.h"

using namespace juce;

Backdrop::Backdrop()
{
    Random r (777);
    for (int i = 0; i < 90; ++i)
        stars.push_back ({ r.nextFloat(), r.nextFloat(), 0.6f + r.nextFloat() * 1.8f, r.nextFloat() * 6.28f, 0.3f + r.nextFloat() * 1.2f });
}

void Backdrop::update (float dt, int newMood, int chordRoot, float newEnergy)
{
    time += dt;
    energy += (jlimit (0.0f, 1.0f, newEnergy) - energy) * jmin (1.0f, dt * 0.8f);   // slow breathing
    bloom = jmax (0.0f, bloom - dt * 0.35f);

    if (chordRoot != lastChord)
    {
        if (lastChord >= 0) bloom = 1.0f;
        lastChord = chordRoot;
    }
    if (newMood != mood)
    {
        previousMood = mood;
        mood = newMood;
        moodBlend = 0.0f;
    }
    moodBlend = jmin (1.0f, moodBlend + dt * 0.5f);
}

void Backdrop::paint (Graphics& g, Rectangle<float> b, Colour top, Colour bottom, float amount) const
{
    if (amount <= 0.001f) return;
    const float base = amount * (0.7f + 0.5f * energy + 0.35f * bloom);
    const auto tint = top.interpolatedWith (bottom, 0.5f).brighter (0.8f).withMultipliedSaturation (1.3f);

    paintAurora (g, b, base, tint);
    if (moodBlend < 1.0f)
        paintMood (g, b, previousMood, base * (1.0f - moodBlend), tint);
    paintMood (g, b, mood, base * moodBlend, tint);
}

void Backdrop::paintAurora (Graphics& g, Rectangle<float> b, float alpha, Colour tint) const
{
    // Three soft, slowly undulating bands of light.
    for (int band = 0; band < 3; ++band)
    {
        const float fb = (float) band;
        const float yBase = b.getY() + b.getHeight() * (0.22f + 0.17f * fb);
        const float thickness = b.getHeight() * (0.10f + 0.04f * std::sin (time * 0.07f + fb));
        Path p;
        const int steps = 48;
        for (int k = 0; k <= steps; ++k)
        {
            const float t = (float) k / (float) steps;
            const float x = b.getX() + t * b.getWidth();
            const float y = yBase + std::sin (t * 5.0f + time * (0.05f + 0.02f * fb) + fb * 2.0f) * b.getHeight() * 0.05f
                                  + std::sin (t * 11.0f - time * 0.09f) * b.getHeight() * 0.015f;
            if (k == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
        }
        for (int k = steps; k >= 0; --k)
        {
            const float t = (float) k / (float) steps;
            const float x = b.getX() + t * b.getWidth();
            const float y = yBase + thickness + std::sin (t * 4.0f + time * 0.06f + fb) * b.getHeight() * 0.04f;
            p.lineTo (x, y);
        }
        p.closeSubPath();

        const auto c = tint.withRotatedHue (0.08f * fb + 0.03f * std::sin (time * 0.05f));
        g.setGradientFill (ColourGradient (c.withAlpha (0.0f), 0.0f, yBase - thickness * 0.2f,
                                           c.withAlpha (jmin (1.0f, 0.045f * alpha)), 0.0f, yBase + thickness * 0.5f, false));
        g.fillPath (p);
    }
}

void Backdrop::paintMood (Graphics& g, Rectangle<float> b, int m, float alpha, Colour tint) const
{
    if (alpha <= 0.001f) return;
    const auto line = Colours::white.withAlpha (jmin (1.0f, 0.05f * alpha));

    switch (m)
    {
        case 0: // Dawn: sun rays fanning up from the horizon
        {
            const Point<float> sun (b.getCentreX(), b.getBottom() + b.getHeight() * 0.1f);
            for (int i = 0; i < 14; ++i)
            {
                const float a = -MathConstants<float>::pi * (0.12f + 0.76f * (float) i / 13.0f)
                                + std::sin (time * 0.04f + (float) i) * 0.03f;
                const float w = 0.025f + 0.015f * std::sin (time * 0.11f + (float) i * 1.7f);
                const float len = b.getHeight() * 1.4f;
                Path ray;
                ray.startNewSubPath (sun);
                ray.lineTo (sun + Point<float> (std::cos (a - w), std::sin (a - w)) * len);
                ray.lineTo (sun + Point<float> (std::cos (a + w), std::sin (a + w)) * len);
                ray.closeSubPath();
                g.setGradientFill (ColourGradient (tint.withAlpha (jmin (1.0f, 0.05f * alpha)), sun.x, sun.y,
                                                   tint.withAlpha (0.0f), sun.x, b.getY(), false));
                g.fillPath (ray);
            }
            break;
        }

        case 1: // Tide: slow parallel waves
        {
            g.setColour (line);
            for (int i = 0; i < 9; ++i)
            {
                Path wave;
                const float y0 = b.getY() + b.getHeight() * (0.15f + 0.09f * (float) i);
                for (int k = 0; k <= 60; ++k)
                {
                    const float t = (float) k / 60.0f;
                    const float y = y0 + std::sin (t * 8.0f + time * 0.3f + (float) i * 0.7f) * 6.0f
                                       + std::sin (t * 3.0f - time * 0.17f) * 10.0f;
                    if (k == 0) wave.startNewSubPath (b.getX(), y); else wave.lineTo (b.getX() + t * b.getWidth(), y);
                }
                g.strokePath (wave, PathStrokeType (1.0f));
            }
            break;
        }

        case 2: // Ember: warm sparks rising and swaying
        {
            for (size_t i = 0; i < stars.size(); i += 2)
            {
                const auto& s = stars[i];
                const float y = std::fmod (s.y - time * 0.015f * s.speed + 10.0f, 1.0f);
                const float x = s.x + std::sin (time * 0.4f * s.speed + s.phase) * 0.01f;
                const float flicker = 0.5f + 0.5f * std::sin (time * 3.0f * s.speed + s.phase);
                g.setColour (Colour (0xffffa060).withAlpha (jmin (1.0f, 0.12f * alpha * flicker * (1.0f - y))));
                g.fillEllipse (b.getX() + x * b.getWidth(), b.getY() + y * b.getHeight(), s.size * 1.4f, s.size * 1.4f);
            }
            break;
        }

        case 3: // Glacier: faint drifting crystal lattice with glinting vertices
        {
            const float cell = jmax (40.0f, b.getWidth() / 14.0f);
            const float h = cell * 0.866f;
            const float ox = std::fmod (time * 2.0f, cell), oy = std::fmod (time * 1.0f, h * 2.0f);
            g.setColour (line);
            for (float y = -h * 2.0f + oy; y < b.getHeight() + h; y += h)
            {
                const int row = (int) std::floor ((y - oy) / h + 0.5f);
                const float shift = (row & 1) ? cell * 0.5f : 0.0f;
                for (float x = -cell + ox + shift; x < b.getWidth() + cell; x += cell)
                {
                    const Point<float> p (b.getX() + x, b.getY() + y);
                    g.drawLine (Line<float> (p, p.translated (cell, 0.0f)), 0.7f);
                    g.drawLine (Line<float> (p, p.translated (cell * 0.5f, h)), 0.7f);
                    g.drawLine (Line<float> (p, p.translated (-cell * 0.5f, h)), 0.7f);
                    const float glint = std::sin (time * 0.9f + x * 0.05f + y * 0.03f);
                    if (glint > 0.97f)
                    {
                        g.setColour (Colours::white.withAlpha (jmin (1.0f, 0.4f * alpha * (glint - 0.97f) * 33.0f)));
                        g.fillEllipse (p.x - 2.0f, p.y - 2.0f, 4.0f, 4.0f);
                        g.setColour (line);
                    }
                }
            }
            break;
        }

        case 4: // Dusk: two sets of slow expanding ripples making a gentle moiré
        {
            const Point<float> c1 (b.getX() + b.getWidth() * (0.3f + 0.05f * std::sin (time * 0.03f)), b.getY() + b.getHeight() * 0.7f);
            const Point<float> c2 (b.getX() + b.getWidth() * (0.72f + 0.05f * std::cos (time * 0.025f)), b.getY() + b.getHeight() * 0.35f);
            const float spacing = 34.0f;
            const float phase = std::fmod (time * 6.0f, spacing);
            for (auto c : { c1, c2 })
                for (int k = 0; k < 24; ++k)
                {
                    const float r = phase + spacing * (float) k;
                    const float fade = 1.0f - (float) k / 24.0f;
                    g.setColour (Colours::white.withAlpha (jmin (1.0f, 0.04f * alpha * fade)));
                    g.drawEllipse (c.x - r, c.y - r, r * 2.0f, r * 2.0f, 0.8f);
                }
            break;
        }

        default: // Night: twinkling stars and faint constellations
        {
            for (size_t i = 0; i < stars.size(); ++i)
            {
                const auto& s = stars[i];
                const float tw = 0.5f + 0.5f * std::sin (time * s.speed + s.phase);
                const Point<float> p (b.getX() + s.x * b.getWidth(), b.getY() + s.y * b.getHeight() * 0.85f);
                g.setColour (Colours::white.withAlpha (jmin (1.0f, 0.35f * alpha * tw)));
                g.fillEllipse (p.x - s.size * 0.5f, p.y - s.size * 0.5f, s.size, s.size);

                if (i % 7 == 0 && i + 1 < stars.size())
                {
                    const auto& n = stars[i + 1];
                    const Point<float> q (b.getX() + n.x * b.getWidth(), b.getY() + n.y * b.getHeight() * 0.85f);
                    if (p.getDistanceFrom (q) < b.getWidth() * 0.25f)
                    {
                        g.setColour (Colours::white.withAlpha (jmin (1.0f, 0.04f * alpha * (0.5f + 0.5f * std::sin (time * 0.2f + s.phase)))));
                        g.drawLine (Line<float> (p, q), 0.6f);
                    }
                }
            }
            break;
        }
    }
}
