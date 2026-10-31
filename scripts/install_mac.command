#!/usr/bin/env bash
# Double-click (or run) this from the unzipped download folder to install Scrape-Scape.
set -euo pipefail
cd "$(dirname "$0")"

mkdir -p "$HOME/Applications" "$HOME/Library/Audio/Plug-Ins/VST3" "$HOME/Library/Audio/Plug-Ins/Components"

for item in Scrape-Scape.app Scrape-Scape.vst3 Scrape-Scape.component; do
  [ -e "$item" ] || { echo "Missing $item — run this from the unzipped Scrape-Scape folder."; exit 1; }
  xattr -dr com.apple.quarantine "$item" 2>/dev/null || true   # unsigned download: clear Gatekeeper flag
done

rm -rf "$HOME/Applications/Scrape-Scape.app" \
       "$HOME/Library/Audio/Plug-Ins/VST3/Scrape-Scape.vst3" \
       "$HOME/Library/Audio/Plug-Ins/Components/Scrape-Scape.component"
cp -R Scrape-Scape.app       "$HOME/Applications/"
cp -R Scrape-Scape.vst3      "$HOME/Library/Audio/Plug-Ins/VST3/"
cp -R Scrape-Scape.component "$HOME/Library/Audio/Plug-Ins/Components/"
killall -9 AudioComponentRegistrar 2>/dev/null || true

echo "Scrape-Scape installed. Opening the app..."
open "$HOME/Applications/Scrape-Scape.app"
