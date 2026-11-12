#!/usr/bin/env bash
# Builds "Dr Mix" (Mod Con DSP) for macOS: universal VST3 + AU + Standalone.
# Needs: Xcode 14+ (with command line tools), CMake 3.22+ and Git. First run downloads JUCE 9.0.5.
set -euo pipefail
cd "$(dirname "$0")"

cmake -B build -G Xcode
cmake --build build --config Release --target DrMix_VST3 DrMix_AU DrMix_Standalone

ART="build/DrMix_artefacts/Release"
mkdir -p dist
rm -rf "dist/Dr Mix.vst3" "dist/Dr Mix.component" "dist/Dr Mix.app"
cp -R "$ART/VST3/Dr Mix.vst3" dist/
cp -R "$ART/AU/Dr Mix.component" dist/
cp -R "$ART/Standalone/Dr Mix.app" dist/

echo
echo "Done:"
echo "  dist/Dr Mix.vst3       -> ~/Library/Audio/Plug-Ins/VST3/"
echo "  dist/Dr Mix.component  -> ~/Library/Audio/Plug-Ins/Components/"
echo "  dist/Dr Mix.app        (standalone)"
echo "Sign and notarise before distributing to other Macs."
