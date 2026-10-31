# Scrape·Scape

A generative ambient instrument for macOS. You arrange elements on a canvas, and they play endlessly
evolving music together. You can **record** what you hear and **export it as a WAV file**, or
**render** a scene straight to WAV faster than realtime.

It runs as a **standalone app** and as a **VST3** and **Audio Unit** instrument in your DAW.
It's a universal binary (Apple Silicon + Intel) for macOS 11 or later, including **Sonoma 14.6**.

> Inspired by the idea of "place-the-pieces" generative music apps such as Brian Eno & Peter Chilvers'
> *Scape*. This is an independent project with its own sound engine and visuals. It isn't affiliated
> with or endorsed by them, and it doesn't use any of their material.

![editor](docs/editor.png)

---

## Install (pre-built)

1. Open the repo on GitHub → **Actions** → **Build macOS** → the latest successful run → download
   the **Scrape-Scape-macOS** artifact. (If a `v*` tag has been pushed, it's also on the **Releases** page.)
2. Unzip it. You'll get a `Scrape-Scape` folder.
3. Open **Terminal** and run:
   ```bash
   bash ~/Downloads/Scrape-Scape/install_mac.command
   ```
   This copies the app to `~/Applications`, the plugins to `~/Library/Audio/Plug-Ins/`, clears
   macOS's "downloaded from the internet" flag, and opens the app.

The build is ad-hoc signed, not notarized by Apple. If macOS still says it "can't be opened",
right-click the app → **Open** → **Open**, or go to **System Settings → Privacy & Security** and click
**Open Anyway**.

## Build from source (on your Mac)

```bash
xcode-select --install          # once, if you don't have the command line tools
brew install cmake              # once
./scripts/build_mac.sh          # builds and installs app + VST3 + AU
```

The first build downloads JUCE 8 automatically, so it takes a few minutes. The outputs are in
`build/ScrapeScape_artefacts/Release/`.

---

## Using it

| Action | How |
|---|---|
| Start / stop the music | **▶ Play / ■ Stop**. Stop fades out gently and pauses; Play picks up where it left off |
| Place an element | Choose it in the palette, then click the canvas |
| Move | Drag it. Height sets the register (higher = higher pitch), left/right sets the stereo position |
| Resize (louder/softer, wider influence) | Scroll over it |
| Remove | Double-click, right-click or ⌥-click |
| Change the whole feel | **Mood**: Dawn, Tide, Ember, Glacier, Dusk or Night (key, scale, pace, colour) |
| Random scene / start over | **Scatter** / **Clear** |
| Use your own picture for an element | Drop an image on its palette slot, or right-click the slot |
| Save / load everything | **Presets ▾** |

**Elements**

- **Drone**: a breathing chord-tone pad. Its horizontal position picks root, third or fifth.
- **Bell**: sparse FM bells.
- **Pluck**: plucked-string figures that wander through the scale.
- **Bass**: soft root notes that follow the chord changes.
- **Shimmer**: a cloud of high sine grains.
- **Wind**: tuned noise that sings between chord tones.
- **Pulse**: a gentle euclidean rhythm, sent through the echo.
- **Shade** (modifier): darkens, calms and adds reverb to everything inside its dashed circle.
- **Spark** (modifier): brightens and energises everything inside its circle.

A shared conductor moves through a slow chord progression, so every element stays in harmony. Each
element has its own random seed, so no two placements behave the same way.

**Knobs:** Activity, Brightness, Tempo, Reverb, Echo and Volume. All of them can be automated in a DAW.

## Your own images

Any element can be drawn with your own picture instead of its built-in icon.

1. **Drag an image file onto a palette slot** (Drone, Bell, Shade, ...). If you drop it anywhere else
   in the window, it goes to the currently selected slot. You can also right-click a slot and choose
   **Use an image...**.
2. In the outline editor, pick how to select the part you want:
   - **Freehand**: draw around it like a lasso
   - **Ellipse** / **Rectangle**: drag out a shape
   - **Whole image**: use all of it

   The preview on the right shows how it will look on the canvas. Click **Use this** (or press Return).
3. Every element of that type on the canvas now uses your cut-out. It glows and pulses with the sound.

Right-click a slot to **Re-outline** the image or go **Back to the built-in icon**.

Supported formats on macOS: **JPEG, PNG, TIFF, HEIC, GIF, BMP, WebP, PSD**. Images are stored
inside your presets and DAW projects, so they travel with them.

![outliner](docs/outliner.png)

## Presets

**Presets ▾** saves and loads *everything*: the elements and their positions, mood, all knobs, and
your custom images.

- **Save preset...** writes a `.scapepreset` file (by default to `~/Music/Scrape-Scape/Presets/`).
  Saved presets appear in the menu under **My presets**.
- **Load preset from file...** loads one from anywhere, or drag a `.scapepreset` file onto the window.
- **Factory scenes** gives you five starting points. They change the scene and knobs but keep
  your images.

## Recording & exporting WAV

- **Record / Stop**: records exactly what you hear. Audio streams to disk as **24-bit WAV** at the
  current sample rate, so takes can be any length. Takes are saved automatically to
  `~/Music/Scrape-Scape/Takes/`. Click **Takes** to open that folder.
- **Export WAV…**: saves the last take wherever you choose.
- **Render WAV…**: choose a length (1–60 minutes) and a file name. The current scene is rendered
  offline to a 24-bit stereo WAV with a smooth fade at the end. This runs far faster than realtime
  (a 30-minute piece takes well under a minute) and doesn't interrupt what you're hearing.

In the **standalone app**, use **Options → Audio/MIDI Settings** (top-left) to choose your output
device and sample rate.

---

## Project layout

```
Source/Engine/     DSP: elements, moods/harmony, voices, FDN reverb, delay, limiter, engine
Source/UI/         Canvas, element glyphs, look & feel
Source/            Plugin processor/editor, WAV recorder, offline renderer
Tests/             Headless smoke test (record → export → render → state round-trip)
scripts/           macOS build and install scripts
.github/workflows/ macOS CI build that produces the downloadable zip
```

Run the tests: `cmake -B build -DSCAPE_BUILD_TESTS=ON && cmake --build build && ./build/ScrapeScapeTests_artefacts/Release/ScrapeScapeTests /tmp`

## License note

Built with [JUCE 8](https://juce.com), which is dual-licensed: AGPLv3 or a commercial JUCE license.
If you distribute this plugin, you must follow one of those licenses.
