#!/usr/bin/env bash
# Builds "Mod Con Fixer" (Mod Con DSP) for macOS: universal VST3 + AU + Standalone.
# Needs: Xcode 14+ (with command line tools), CMake 3.22+ and Git. First run downloads JUCE 9.0.5.
set -euo pipefail
cd "$(dirname "$0")"

cmake -B build -G Xcode
cmake --build build --config Release --target ModConFixer_VST3 ModConFixer_AU ModConFixer_Standalone

ART="build/ModConFixer_artefacts/Release"
mkdir -p dist
rm -rf "dist/Mod Con Fixer.vst3" "dist/Mod Con Fixer.component" "dist/Mod Con Fixer.app"
cp -R "$ART/VST3/Mod Con Fixer.vst3" dist/
cp -R "$ART/AU/Mod Con Fixer.component" dist/
cp -R "$ART/Standalone/Mod Con Fixer.app" dist/

echo
echo "Done:"
echo "  dist/Mod Con Fixer.vst3       -> ~/Library/Audio/Plug-Ins/VST3/"
echo "  dist/Mod Con Fixer.component  -> ~/Library/Audio/Plug-Ins/Components/"
echo "  dist/Mod Con Fixer.app        (standalone)"
echo "Sign and notarise before distributing to other Macs."
