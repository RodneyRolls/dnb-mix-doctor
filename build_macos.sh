#!/usr/bin/env bash
# Builds "Rolls Mix Fixer" (Mod Con DSP) for macOS: universal VST3 + AU + Standalone.
# Needs: Xcode 14+ (with command line tools), CMake 3.22+ and Git. First run downloads JUCE 9.0.5.
set -euo pipefail
cd "$(dirname "$0")"

cmake -B build -G Xcode
cmake --build build --config Release --target RollsMixFixer_VST3 RollsMixFixer_AU RollsMixFixer_Standalone

ART="build/RollsMixFixer_artefacts/Release"
mkdir -p dist
rm -rf "dist/Rolls Mix Fixer.vst3" "dist/Rolls Mix Fixer.component" "dist/Rolls Mix Fixer.app"
cp -R "$ART/VST3/Rolls Mix Fixer.vst3" dist/
cp -R "$ART/AU/Rolls Mix Fixer.component" dist/
cp -R "$ART/Standalone/Rolls Mix Fixer.app" dist/

echo
echo "Done:"
echo "  dist/Rolls Mix Fixer.vst3       -> ~/Library/Audio/Plug-Ins/VST3/"
echo "  dist/Rolls Mix Fixer.component  -> ~/Library/Audio/Plug-Ins/Components/"
echo "  dist/Rolls Mix Fixer.app        (standalone)"
echo "Sign and notarise before distributing to other Macs."
