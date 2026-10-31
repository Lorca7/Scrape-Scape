#!/usr/bin/env bash
# Build Scrape-Scape on macOS and install the app + plugins for the current user.
# Requires: Xcode Command Line Tools (xcode-select --install) and CMake (brew install cmake).
set -euo pipefail
cd "$(dirname "$0")/.."

command -v cmake >/dev/null || { echo "CMake not found. Install it with: brew install cmake"; exit 1; }
xcode-select -p >/dev/null 2>&1 || { echo "Xcode Command Line Tools missing. Run: xcode-select --install"; exit 1; }

cmake -B build -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" \
      -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0
cmake --build build --config Release -j "$(sysctl -n hw.ncpu)"

ART=build/ScrapeScape_artefacts/Release
for b in "$ART/Standalone/Scrape-Scape.app" "$ART/VST3/Scrape-Scape.vst3" "$ART/AU/Scrape-Scape.component"; do
  codesign --force --deep --sign - "$b"
done

mkdir -p "$HOME/Applications" "$HOME/Library/Audio/Plug-Ins/VST3" "$HOME/Library/Audio/Plug-Ins/Components"
rm -rf "$HOME/Applications/Scrape-Scape.app" \
       "$HOME/Library/Audio/Plug-Ins/VST3/Scrape-Scape.vst3" \
       "$HOME/Library/Audio/Plug-Ins/Components/Scrape-Scape.component"
cp -R "$ART/Standalone/Scrape-Scape.app"   "$HOME/Applications/"
cp -R "$ART/VST3/Scrape-Scape.vst3"        "$HOME/Library/Audio/Plug-Ins/VST3/"
cp -R "$ART/AU/Scrape-Scape.component"     "$HOME/Library/Audio/Plug-Ins/Components/"
killall -9 AudioComponentRegistrar 2>/dev/null || true   # make Logic/GarageBand rescan AUs

echo
echo "Installed:"
echo "  ~/Applications/Scrape-Scape.app"
echo "  ~/Library/Audio/Plug-Ins/VST3/Scrape-Scape.vst3"
echo "  ~/Library/Audio/Plug-Ins/Components/Scrape-Scape.component"
echo
echo "Launch it with: open ~/Applications/Scrape-Scape.app"
