#!/usr/bin/env bash
# Builds "JLP Mix Fixer" (Mod Con DSP) for macOS: universal VST3 + AU + Standalone.
# Needs: Xcode 14+ (with command line tools), CMake 3.22+ and Git. First run downloads JUCE 9.0.5.
set -euo pipefail
cd "$(dirname "$0")"

cmake -B build -G Xcode
cmake --build build --config Release --target JlpMixFixer_VST3 JlpMixFixer_AU JlpMixFixer_Standalone

ART="build/JlpMixFixer_artefacts/Release"
mkdir -p dist
rm -rf "dist/JLP Mix Fixer.vst3" "dist/JLP Mix Fixer.component" "dist/JLP Mix Fixer.app"
cp -R "$ART/VST3/JLP Mix Fixer.vst3" dist/
cp -R "$ART/AU/JLP Mix Fixer.component" dist/
cp -R "$ART/Standalone/JLP Mix Fixer.app" dist/

echo
echo "Done:"
echo "  dist/JLP Mix Fixer.vst3       -> ~/Library/Audio/Plug-Ins/VST3/"
echo "  dist/JLP Mix Fixer.component  -> ~/Library/Audio/Plug-Ins/Components/"
echo "  dist/JLP Mix Fixer.app        (standalone)"
echo "Sign and notarise before distributing to other Macs."
