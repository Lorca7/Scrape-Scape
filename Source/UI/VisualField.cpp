#include "VisualField.h"

using namespace juce;
using namespace scape;

namespace
{
    constexpr int maxParticles = 2600;
    constexpr int maxRipples = 160;
    constexpr float trailScale = 0.5f;   // the trail layer is half resolution (soft, cheap)

    bool isSound (int type) noexcept { return type >= 0 && type < kNumSoundTypes; }
}

void VisualField::setBounds (int w, int h)
{
    w = jmax (1, w); h = jmax (1, h);
    if (w == width && h == height && trail.isValid()) return;
    width = w; height = h;
    trail = Image (Image::ARGB, jmax (1, (int) ((float) w * trailScale)), jmax (1, (int) ((float) h * trailScale)), true);
}

Colour VisualField::colourFor (int type, const VisualContext& ctx) const
{
    Colour c (getElementInfo (type).colour);
    if (isSound (type))
    {
        const float tone = ctx.voice[(size_t) type][1];
        c = c.withRotatedHue ((tone - 0.5f) * 0.18f);
    }
    // Brightness control: dull and dim when dark, saturated and luminous when bright.
    return c.withMultipliedSaturation (0.6f + 0.7f * ctx.brightness)
            .withMultipliedBrightness (0.7f + 0.5f * ctx.brightness);
}

void VisualField::spawnRipples (const ElementVisual& e, const VisualContext& ctx, float strength)
{
    // Only struck sounds ripple; continuous ones (drone, shimmer, wind) show as particles.
    if (e.type != (int) ElementType::Bowl && e.type != (int) ElementType::Pluck
        && e.type != (int) ElementType::Bass && e.type != (int) ElementType::Chimes)
        return;
    if (time - lastRippleTime[(size_t) e.index] < 0.3f) return;
    lastRippleTime[(size_t) e.index] = time;

    const auto& vp = ctx.voice[(size_t) e.type];
    const float space = vp[kRelease], echo = ctx.echo, level = vp[0];
    const auto col = colourFor (e.type, ctx);

    // Echo control -> repeated, fainter ripples (and the global echo adds a few more).
    const int repeats = 1 + (int) std::round (echo * 2.0f + ctx.echo * 1.0f);
    for (int k = 0; k < repeats && (int) ripples.size() < maxRipples; ++k)
    {
        Ripple r;
        r.c = e.pos;
        r.radius = e.radius * 0.8f;
        r.speed = (40.0f + 90.0f * strength) * (0.7f + 0.6f * level) * (e.type == (int) ElementType::Bass ? 0.6f : 1.0f);
        r.maxLife = r.life = 0.8f + 2.2f * space + 0.8f * ctx.reverb;
        r.width = e.type == (int) ElementType::Bass ? 6.0f : (e.type == (int) ElementType::Bowl ? 3.0f : 1.6f);
        r.delay = (float) k * (0.22f + 0.25f * echo);
        r.colour = col.withMultipliedAlpha (std::pow (0.6f, (float) k));
        r.owner = e.index;
        r.style = e.type;
        ripples.push_back (r);
    }
}

void VisualField::emit (const ElementVisual& e, const VisualContext& ctx, float dt)
{
    if (! isSound (e.type)) return;
    const auto& vp = ctx.voice[(size_t) e.type];
    static constexpr float baseRate[kNumSoundTypes] = { 16.0f, 3.5f, 5.0f, 5.0f, 34.0f, 40.0f, 5.0f };

    const float rate = baseRate[e.type] * (0.25f + e.activity) * (0.4f + ctx.activity) * (0.3f + 1.2f * vp[0])
                       * ctx.amount * (ctx.playing ? 1.0f : 0.0f) * (1.0f + ctx.grainMix * 1.5f);
    emitAccumulator[(size_t) e.index] += rate * dt;

    const auto col = colourFor (e.type, ctx);
    while (emitAccumulator[(size_t) e.index] >= 1.0f && (int) particles.size() < maxParticles)
    {
        emitAccumulator[(size_t) e.index] -= 1.0f;

        const float a = rng.nextFloat() * MathConstants<float>::twoPi;
        const Point<float> dir (std::cos (a), std::sin (a));
        const Point<float> tangent (-dir.y, dir.x);

        Particle p;
        p.p = e.pos + dir * e.radius * (0.8f + 0.4f * rng.nextFloat());
        p.colour = col;
        p.owner = e.index;
        p.style = e.type;
        p.size = 2.0f;
        p.maxLife = 3.0f;

        switch ((ElementType) e.type)
        {
            case ElementType::Drone:   p.v = dir * 12.0f + tangent * 22.0f; p.size = 2.5f + 2.0f * rng.nextFloat(); p.maxLife = 5.0f + 3.0f * vp[4]; break;
            case ElementType::Bowl:    p.v = tangent * (18.0f + 20.0f * rng.nextFloat()) + dir * 8.0f; p.size = 2.2f; p.maxLife = 3.0f + 4.0f * vp[4]; break;
            case ElementType::Pluck:   p.v = dir * (70.0f + 50.0f * vp[6]); p.size = 1.4f; p.maxLife = 1.2f + 2.0f * vp[4]; break;
            case ElementType::Bass:    p.v = dir * 10.0f; p.size = 7.0f + 6.0f * rng.nextFloat() * (0.5f + vp[4]); p.maxLife = 2.5f; break;
            case ElementType::Shimmer: p.v = dir * (20.0f + 40.0f * rng.nextFloat()); p.size = 1.0f + rng.nextFloat(); p.maxLife = 1.0f + 1.5f * vp[4]; break;
            case ElementType::Chimes:  p.v = Point<float> (dir.x * 25.0f, 30.0f + 40.0f * rng.nextFloat()); p.size = 1.5f; p.maxLife = 2.0f + 2.5f * vp[4]; break;
            case ElementType::Pad:     p.v = dir * 9.0f + tangent * 6.0f; p.size = 9.0f + 7.0f * rng.nextFloat(); p.maxLife = 4.0f + 3.0f * vp[4]; break;
            case ElementType::Shade:
            case ElementType::Spark:
            case ElementType::NumTypes: break;
        }

        // Granular: some particles become tiny fast fragments.
        if (ctx.grainMix > 0.0f && rng.nextFloat() < ctx.grainMix * 0.5f)
        {
            p.size *= 0.5f;
            p.v *= 1.8f;
            p.maxLife *= 0.6f;
        }

        p.life = p.maxLife;
        particles.push_back (p);
    }
}

void VisualField::update (float dt, const std::vector<ElementVisual>& elements, const VisualContext& ctx)
{
    dt = jlimit (0.0f, 0.1f, dt);
    time += dt;
    current = elements;
    lastCtx = ctx;

    // Triggers (sudden rises in activity) become ripples; continuous sound emits particles.
    for (const auto& e : elements)
    {
        const auto i = (size_t) e.index;
        if (prevSeed[i] != e.seed) { prevSeed[i] = e.seed; prevActivity[i] = e.activity; emitAccumulator[i] = 0.0f; }
        const float rise = e.activity - prevActivity[i];
        if (rise > 0.12f && ctx.amount > 0.01f)
            spawnRipples (e, ctx, jlimit (0.2f, 1.0f, e.activity));
        prevActivity[i] = e.activity;
        emit (e, ctx, dt);
    }

    // Particle dynamics: drift, swirl around other elements, blend colours on close passes,
    // slowed/tinted by Shade zones, energised by Spark zones.
    const float minDim = (float) jmin (width, height);
    const float drag = std::pow (0.985f, dt * 60.0f);
    const float warble = ctx.fxType == 3 ? ctx.fxMix : 0.0f;

    for (auto& p : particles)
    {
        p.life -= dt;
        Point<float> force;

        for (const auto& e : elements)
        {
            const auto d = e.pos - p.p;
            const float dist2 = d.x * d.x + d.y * d.y + 400.0f;
            const float dist = std::sqrt (dist2);

            if (e.type == (int) ElementType::Shade || e.type == (int) ElementType::Spark)
            {
                const float zone = 0.3f * e.size * minDim;
                if (dist < zone)
                {
                    const float k = 1.0f - dist / zone;
                    if (e.type == (int) ElementType::Shade)
                    {
                        p.v *= 1.0f - 0.04f * k;
                        p.colour = p.colour.interpolatedWith (Colour (getElementInfo (e.type).colour), 0.03f * k);
                    }
                    else
                    {
                        p.v *= 1.0f + 0.02f * k;
                        p.colour = p.colour.brighter (0.01f * k);
                    }
                }
                continue;
            }

            if (e.index == p.owner) continue;

            // Gravity towards other sound elements plus a sideways pull -> orbits and spirals.
            const float g = 9000.0f * e.size / dist2;
            const Point<float> n (d.x / dist, d.y / dist);
            force += n * g + Point<float> (-n.y, n.x) * g * 0.9f;

            if (dist < e.radius * 2.2f && isSound (e.type))
                p.colour = p.colour.interpolatedWith (colourFor (e.type, ctx), 0.04f);
            if (dist < e.radius * 0.9f)
                force -= n * g * 3.0f;   // don't fall in: bounce into orbit
        }

        // Gentle flowing field (stronger for wind), plus warble wobble from the XY pad.
        const float angle = std::sin (p.p.x * 0.006f + time * 0.3f) * 2.0f + std::cos (p.p.y * 0.008f - time * 0.23f) * 2.0f;
        const float flow = p.style == (int) ElementType::Pad ? 14.0f : 6.0f;
        force += Point<float> (std::cos (angle), std::sin (angle)) * flow;
        if (warble > 0.0f)
            force += Point<float> (0.0f, std::sin (time * 6.0f + p.p.x * 0.05f) * 60.0f * warble);

        p.v = (p.v + force * dt) * drag;
        const float speed = p.v.getDistanceFromOrigin();
        if (speed > 260.0f) p.v *= 260.0f / speed;
        p.p += p.v * dt;
    }

    particles.erase (std::remove_if (particles.begin(), particles.end(), [this] (const Particle& p)
                     {
                         return p.life <= 0.0f || p.p.x < -40.0f || p.p.y < -40.0f
                                || p.p.x > (float) width + 40.0f || p.p.y > (float) height + 40.0f;
                     }), particles.end());

    for (auto& r : ripples)
    {
        if (r.delay > 0.0f) { r.delay -= dt; continue; }
        r.radius += r.speed * dt;
        r.life -= dt;
    }
    ripples.erase (std::remove_if (ripples.begin(), ripples.end(), [] (const Ripple& r) { return r.life <= 0.0f; }),
                   ripples.end());

    // Where ripples from different elements cross, sparks appear: emergent interference patterns.
    sparks.clear();
    const int n = (int) ripples.size();
    for (int a = 0; a < n && (int) sparks.size() < 200; ++a)
    {
        const auto& ra = ripples[(size_t) a];
        if (ra.delay > 0.0f) continue;
        for (int b = a + 1; b < n; ++b)
        {
            const auto& rb = ripples[(size_t) b];
            if (rb.delay > 0.0f || rb.owner == ra.owner) continue;
            const float d = ra.c.getDistanceFrom (rb.c);
            if (d < 1.0f || d > ra.radius + rb.radius || d < std::abs (ra.radius - rb.radius)) continue;

            const float aa = (ra.radius * ra.radius - rb.radius * rb.radius + d * d) / (2.0f * d);
            const float h = std::sqrt (jmax (0.0f, ra.radius * ra.radius - aa * aa));
            const auto u = (rb.c - ra.c) / d;
            const auto mid = ra.c + u * aa;
            const Point<float> perp (-u.y, u.x);
            const auto c = ra.colour.interpolatedWith (rb.colour, 0.5f).brighter (0.3f);
            const float s = 3.5f + 5.0f * jmin (ra.life / ra.maxLife, rb.life / rb.maxLife);
            sparks.push_back ({ mid + perp * h, c, s });
            sparks.push_back ({ mid - perp * h, c, s });
        }
    }

    drawIntoTrail (ctx);
}

void VisualField::drawIntoTrail (const VisualContext& ctx)
{
    if (! trail.isValid()) return;

    // Persistence: longer trails with more reverb (and with Wash / Dub Echo on the pad).
    float keep = 0.80f + 0.16f * ctx.reverb;
    if (ctx.fxType == 1 || ctx.fxType == 2) keep += 0.03f * ctx.fxMix;
    trail.multiplyAllAlphas (jlimit (0.5f, 0.985f, keep));

    Graphics g (trail);
    g.addTransform (AffineTransform::scale (trailScale));
    const float amount = ctx.amount;

    // Crush on the pad: particles snap to a coarse grid (pixelated look).
    const float grid = ctx.fxType == 4 ? 2.0f + 22.0f * ctx.fxX * ctx.fxMix : 0.0f;

    for (const auto& p : particles)
    {
        const float lifeT = p.life / p.maxLife;
        const float alpha = jlimit (0.0f, 1.0f, lifeT * ((p.style == (int) ElementType::Bass || p.style == (int) ElementType::Pad) ? 0.14f : 0.95f) * amount);
        auto pos = p.p;
        if (grid > 0.5f) pos = { std::round (pos.x / grid) * grid, std::round (pos.y / grid) * grid };
        g.setColour (p.colour.withMultipliedAlpha (alpha));

        if (p.style == (int) ElementType::Pluck)
            g.drawLine (Line<float> (pos, pos - p.v * 0.06f), p.size);
        else if (grid > 0.5f)
            g.fillRect (Rectangle<float> (p.size * 1.6f, p.size * 1.6f).withCentre (pos));
        else
            g.fillEllipse (Rectangle<float> (p.size * 1.35f, p.size * 1.35f).withCentre (pos));
    }

    // Drones trace slowly rotating rose curves; with the trails these grow into spirographs.
    for (const auto& e : current)
    {
        if (e.type != (int) ElementType::Drone) continue;
        const auto& vp = ctx.voice[0];
        const int petals = 3 + (ctx.chordRoot % 5);
        const float rot = time * (0.05f + 0.25f * vp[4]) + (float) (e.seed % 100) * 0.06f;
        const float R = e.radius * (2.0f + 1.6f * e.activity) * (0.8f + 0.5f * vp[4]);
        Path rose;
        for (int k = 0; k <= 120; ++k)
        {
            const float th = (float) k / 120.0f * MathConstants<float>::twoPi;
            const float rr = R * (0.6f + 0.4f * std::cos ((float) petals * th));
            const auto pt = e.pos + Point<float> (std::cos (th + rot), std::sin (th + rot)) * rr;
            if (k == 0) rose.startNewSubPath (pt); else rose.lineTo (pt);
        }
        g.setColour (colourFor (0, ctx).withMultipliedAlpha (0.16f * amount * (0.4f + e.activity)));
        g.strokePath (rose, PathStrokeType (1.2f));
    }
}

void VisualField::drawRipples (Graphics& g) const
{
    const float amount = lastCtx.amount;
    for (const auto& r : ripples)
    {
        if (r.delay > 0.0f) continue;
        const float alpha = jlimit (0.0f, 1.0f, r.life / r.maxLife) * 0.85f * amount;
        g.setColour (r.colour.withMultipliedAlpha (alpha));
        if (r.style == (int) ElementType::Chimes)
        {
            // A ring of glinting dots, like light catching swinging tubes.
            for (int k = 0; k < 10; ++k)
            {
                const float ang = (float) k / 10.0f * MathConstants<float>::twoPi + r.radius * 0.01f;
                const auto pt = r.c + Point<float> (std::cos (ang), std::sin (ang)) * r.radius;
                g.fillEllipse (pt.x - 1.8f, pt.y - 1.8f, 3.6f, 3.6f);
            }
        }
        else if (r.style == (int) ElementType::Pluck)
        {
            for (int k = 0; k < 12; ++k)
            {
                const float a = (float) k / 12.0f * MathConstants<float>::twoPi + time * 0.2f;
                const Point<float> dir (std::cos (a), std::sin (a));
                g.drawLine (Line<float> (r.c + dir * r.radius, r.c + dir * (r.radius + 6.0f)), r.width);
            }
        }
        else
            g.drawEllipse (Rectangle<float> (r.radius * 2.0f, r.radius * 2.0f).withCentre (r.c), r.width);
    }

}

void VisualField::paintBehind (Graphics& g)
{
    if (trail.isValid() && lastCtx.amount > 0.0f)
    {
        Graphics::ScopedSaveState s (g);
        g.setOpacity (1.0f);
        g.setImageResamplingQuality (Graphics::mediumResamplingQuality);
        g.drawImage (trail, Rectangle<float> (0.0f, 0.0f, (float) width, (float) height));
    }

    // Glowing halos: each sounding element is wrapped in a light that pulses with its activity.
    for (const auto& e : current)
    {
        if (! isSound (e.type)) continue;
        const auto col = colourFor (e.type, lastCtx);
        const float R = e.radius * (2.2f + 2.5f * e.activity + 0.6f * std::sin (time * 1.7f + (float) e.index));
        const float a = lastCtx.amount * (0.12f + 0.4f * e.activity);
        g.setGradientFill (ColourGradient (col.withAlpha (a), e.pos.x, e.pos.y, col.withAlpha (0.0f), e.pos.x + R, e.pos.y, true));
        g.fillEllipse (e.pos.x - R, e.pos.y - R, R * 2.0f, R * 2.0f);
    }

    drawRipples (g);

    // Threads between neighbouring sound elements: their combined activity makes them vibrate.
    const float maxDist = 0.42f * (float) jmax (width, height);
    for (size_t a = 0; a < current.size(); ++a)
        for (size_t b = a + 1; b < current.size(); ++b)
        {
            const auto& ea = current[a];
            const auto& eb = current[b];
            if (! isSound (ea.type) || ! isSound (eb.type)) continue;
            const float d = ea.pos.getDistanceFrom (eb.pos);
            if (d > maxDist || d < 1.0f) continue;

            const float closeness = 1.0f - d / maxDist;
            const float energy = ea.activity + eb.activity;
            const auto dir = (eb.pos - ea.pos) / d;
            const Point<float> perp (-dir.y, dir.x);
            const float amp = (4.0f + 18.0f * energy) * closeness;
            const float waves = 2.0f + (float) ((ea.type + eb.type) % 4);

            Path thread;
            for (int k = 0; k <= 40; ++k)
            {
                const float t = (float) k / 40.0f;
                const float env = std::sin (t * MathConstants<float>::pi);
                const float off = std::sin (t * waves * MathConstants<float>::twoPi - time * 2.0f) * amp * env;
                const auto pt = ea.pos + (eb.pos - ea.pos) * t + perp * off;
                if (k == 0) thread.startNewSubPath (pt); else thread.lineTo (pt);
            }
            ColourGradient grad (colourFor (ea.type, lastCtx).withAlpha (0.35f * closeness * lastCtx.amount), ea.pos,
                                 colourFor (eb.type, lastCtx).withAlpha (0.35f * closeness * lastCtx.amount), eb.pos, false);
            g.setGradientFill (grad);
            g.strokePath (thread, PathStrokeType (1.0f + energy));
        }
}

void VisualField::paintFront (Graphics& g)
{
    if (lastCtx.amount <= 0.0f) return;
    for (const auto& s : sparks)
    {
        g.setColour (s.c.withMultipliedAlpha (1.0f * lastCtx.amount));
        Path star;
        star.addStar (s.p, 4, s.s * 0.3f, s.s, time);
        g.fillPath (star);
    }
}
