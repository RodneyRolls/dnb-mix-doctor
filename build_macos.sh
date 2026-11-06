#!/usr/bin/env bash
# Builds "The Mix Fixer" (Mod Con DSP) for macOS: universal VST3 + AU + Standalone.
# Needs: Xcode 14+ (with command line tools), CMake 3.22+ and Git. First run downloads JUCE 9.0.5.
set -euo pipefail
cd "$(dirname "$0")"

cmake -B build -G Xcode
cmake --build build --config Release --target TheMixFixer_VST3 TheMixFixer_AU TheMixFixer_Standalone

ART="build/TheMixFixer_artefacts/Release"
mkdir -p dist
rm -rf "dist/The Mix Fixer.vst3" "dist/The Mix Fixer.component" "dist/The Mix Fixer.app"
cp -R "$ART/VST3/The Mix Fixer.vst3" dist/
cp -R "$ART/AU/The Mix Fixer.component" dist/
cp -R "$ART/Standalone/The Mix Fixer.app" dist/

echo
echo "Done:"
echo "  dist/The Mix Fixer.vst3       -> ~/Library/Audio/Plug-Ins/VST3/"
echo "  dist/The Mix Fixer.component  -> ~/Library/Audio/Plug-Ins/Components/"
echo "  dist/The Mix Fixer.app        (standalone)"
echo "Sign and notarise before distributing to other Macs."
